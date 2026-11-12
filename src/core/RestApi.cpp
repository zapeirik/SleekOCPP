#include "RestApi.h"

#include <QHttpServerRequest>
#include <QHttpServerResponder>
#include <QHttpServerResponse>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTcpServer>
#include <QUrlQuery>
#include <memory>

#include "core/CentralSystem.h"
#include "core/db/Database.h"
#include "core/ocpp/OcppServer.h"

namespace {

using StatusCode = QHttpServerResponder::StatusCode;

QJsonValue timeJson(const QDateTime &t)
{
    return t.isValid() ? QJsonValue(Database::toDb(t)) : QJsonValue(QJsonValue::Null);
}

QJsonObject toJson(const ConnectorStatusRecord &c)
{
    return {{"connectorId", c.connectorId}, {"status", c.status}, {"errorCode", c.errorCode},
            {"info", c.info}, {"updatedAt", timeJson(c.updatedAt)}};
}

QJsonObject toJson(const TransactionRecord &t)
{
    return {{"id", static_cast<double>(t.id)}, {"chargePointId", t.chargePointId}, {"connectorId", t.connectorId},
            {"idTag", t.idTag},
            {"active", t.isActive()}, {"meterStart", static_cast<double>(t.meterStart)},
            {"meterStop", t.meterStop ? QJsonValue(static_cast<double>(*t.meterStop)) : QJsonValue(QJsonValue::Null)},
            {"energyWh", t.energyWh() ? QJsonValue(static_cast<double>(*t.energyWh())) : QJsonValue(QJsonValue::Null)},
            {"startTime", timeJson(t.startTime)}, {"stopTime", timeJson(t.stopTime)},
            {"stopReason", t.stopReason}};
}

QJsonObject toJson(const MessageRecord &m)
{
    return {{"id", static_cast<double>(m.id)}, {"timestamp", timeJson(m.timestamp)},
            {"direction", m.incoming ? "in" : "out"}, {"type", m.type}, {"action", m.action},
            {"uniqueId", m.uniqueId}, {"raw", m.raw}};
}

QJsonObject toJson(const MeterValueRecord &v)
{
    return {{"timestamp", timeJson(v.timestamp)}, {"connectorId", v.connectorId},
            {"transactionId", v.transactionId ? QJsonValue(static_cast<double>(*v.transactionId)) : QJsonValue(QJsonValue::Null)},
            {"measurand", v.measurand}, {"value", v.value}, {"unit", v.unit},
            {"context", v.context}, {"phase", v.phase}, {"location", v.location}};
}

QJsonObject toJson(const ConfigKeyRecord &k)
{
    return {{"key", k.key}, {"value", k.value}, {"readonly", k.readonly}, {"updatedAt", timeJson(k.updatedAt)}};
}

/// Summary of a charge point for the list and detail endpoints.
QJsonObject toJson(const ChargePointRecord &r, const QList<ConnectorStatusRecord> &connectors)
{
    QJsonArray conns;
    for (const auto &c : connectors)
        conns.append(toJson(c));
    return {{"id", r.id}, {"connected", r.connected}, {"remoteAddress", r.remoteAddress},
            {"vendor", r.vendor}, {"model", r.model}, {"serial", r.serial}, {"firmware", r.firmware},
            {"firstSeen", timeJson(r.firstSeen)}, {"lastConnected", timeJson(r.lastConnected)},
            {"lastDisconnected", timeJson(r.lastDisconnected)}, {"lastHeartbeat", timeJson(r.lastHeartbeat)},
            {"lastMeterValue", timeJson(r.lastMeterValue)}, {"connectors", conns}};
}

QHttpServerResponse jsonResponse(const QJsonObject &o, StatusCode code = StatusCode::Ok)
{
    return QHttpServerResponse(o, code);
}

QHttpServerResponse errorResponse(const QString &message, StatusCode code)
{
    return jsonResponse({{"error", message}}, code);
}

/// Maps a command outcome to the documented JSON body and HTTP status.
QHttpServerResponse outcomeResponse(const CallOutcome &o)
{
    QJsonObject body{{"action", o.action}, {"status", o.summary()}};
    StatusCode code = StatusCode::Ok;
    switch (o.status) {
    case CallOutcome::Status::Result:
        body["outcome"] = "result";
        body["payload"] = o.payload;
        break;
    case CallOutcome::Status::Error:
        body["outcome"] = "error";
        body["errorCode"] = o.errorCode;
        body["errorDescription"] = o.errorDescription;
        break;
    case CallOutcome::Status::Timeout:
        body["outcome"] = "timeout";
        code = StatusCode::GatewayTimeout;
        break;
    case CallOutcome::Status::Disconnected:
        body["outcome"] = "disconnected";
        code = StatusCode::Conflict;
        break;
    }
    return jsonResponse(body, code);
}

int limitParam(const QHttpServerRequest &req, int def)
{
    const int v = QUrlQuery(req.url()).queryItemValue("limit").toInt();
    return v > 0 ? qMin(v, 1000) : def;
}

std::optional<int> optInt(const QJsonObject &o, const QString &key)
{
    if (!o.contains(key) || o[key].isNull())
        return std::nullopt;
    return o[key].toInt();
}

} // namespace

RestApi::RestApi(CentralSystem *centralSystem, QObject *parent)
    : QObject(parent), m_cs(centralSystem)
{
    addReadRoutes();

    using C = ChargePointCommands::Callback;
    ChargePointCommands *cmd = m_cs->commands();
    Database *db = m_cs->database();

    addCommand("remote_start", [cmd](const QString &id, const QJsonObject &b, C cb) -> QString {
        if (!b["idTag"].isString() || b["idTag"].toString().isEmpty())
            return "idTag is required";
        cmd->remoteStartTransaction(id, b["idTag"].toString(), optInt(b, "connectorId"),
                                    b["chargingProfile"].toObject(), cb);
        return {};
    });
    // Without transactionId the charge point's running transaction is stopped.
    addCommand("remote_stop", [cmd, db](const QString &id, const QJsonObject &b, C cb) -> QString {
        std::optional<int> txId = optInt(b, "transactionId");
        if (!txId) {
            const auto tx = db->lastTransaction(id);
            if (!tx || !tx->isActive())
                return "No active transaction; give transactionId";
            txId = static_cast<int>(tx->id);
        }
        cmd->remoteStopTransaction(id, *txId, cb);
        return {};
    });
    addCommand("get_configuration", [cmd](const QString &id, const QJsonObject &b, C cb) -> QString {
        QStringList keys;
        for (const QJsonValue &k : b["keys"].toArray())
            keys << k.toString();
        cmd->getConfiguration(id, keys, cb);
        return {};
    });
    addCommand("change_configuration", [cmd](const QString &id, const QJsonObject &b, C cb) -> QString {
        if (!b["key"].isString() || !b.contains("value"))
            return "key and value are required";
        cmd->changeConfiguration(id, b["key"].toString(), b["value"].toVariant().toString(), cb);
        return {};
    });
    addCommand("set_charging_profile", [cmd](const QString &id, const QJsonObject &b, C cb) -> QString {
        if (!b["connectorId"].isDouble() || !b["csChargingProfiles"].isObject())
            return "connectorId and csChargingProfiles are required";
        cmd->setChargingProfile(id, b["connectorId"].toInt(), b["csChargingProfiles"].toObject(), cb);
        return {};
    });
    addCommand("clear_charging_profile", [cmd](const QString &id, const QJsonObject &b, C cb) -> QString {
        cmd->clearChargingProfile(id, optInt(b, "id"), optInt(b, "connectorId"),
                                  b["purpose"].toString(), optInt(b, "stackLevel"), cb);
        return {};
    });
    addCommand("get_composite_schedule", [cmd](const QString &id, const QJsonObject &b, C cb) -> QString {
        if (!b["connectorId"].isDouble() || !b["duration"].isDouble())
            return "connectorId and duration are required";
        cmd->getCompositeSchedule(id, b["connectorId"].toInt(), b["duration"].toInt(),
                                  b["chargingRateUnit"].toString(), cb);
        return {};
    });
    addCommand("reset", [cmd](const QString &id, const QJsonObject &b, C cb) -> QString {
        const QString type = b["type"].toString();
        if (type != "Soft" && type != "Hard")
            return "type must be Soft or Hard";
        cmd->reset(id, type, cb);
        return {};
    });
    addCommand("trigger_message", [cmd](const QString &id, const QJsonObject &b, C cb) -> QString {
        if (!b["requestedMessage"].isString())
            return "requestedMessage is required";
        cmd->triggerMessage(id, b["requestedMessage"].toString(), optInt(b, "connectorId"), cb);
        return {};
    });
    addCommand("unlock_connector", [cmd](const QString &id, const QJsonObject &b, C cb) -> QString {
        if (!b["connectorId"].isDouble())
            return "connectorId is required";
        cmd->unlockConnector(id, b["connectorId"].toInt(), cb);
        return {};
    });
    addCommand("change_availability", [cmd](const QString &id, const QJsonObject &b, C cb) -> QString {
        const QString type = b["type"].toString();
        if (!b["connectorId"].isDouble() || (type != "Operative" && type != "Inoperative"))
            return "connectorId and type (Operative or Inoperative) are required";
        cmd->changeAvailability(id, b["connectorId"].toInt(), type, cb);
        return {};
    });
    addCommand("send", [cmd](const QString &id, const QJsonObject &b, C cb) -> QString {
        if (!b["action"].isString() || b["action"].toString().isEmpty())
            return "action is required";
        cmd->send(id, b["action"].toString(), b["payload"].toObject(), cb);
        return {};
    });
}

bool RestApi::listen(quint16 port, const QHostAddress &address)
{
    m_tcp = new QTcpServer(this);
    if (!m_tcp->listen(address, port) || !m_http.bind(m_tcp)) {
        qCritical().noquote() << "REST API cannot listen on port" << port << ":" << m_tcp->errorString();
        return false;
    }
    qInfo().noquote() << "REST API listening on" << address.toString() << "port" << m_tcp->serverPort();
    return true;
}

quint16 RestApi::port() const
{
    return m_tcp ? m_tcp->serverPort() : 0;
}

void RestApi::addReadRoutes()
{
    Database *db = m_cs->database();

    m_http.route("/api/charge_points", QHttpServerRequest::Method::Get, [db] {
        QJsonArray list;
        for (const auto &r : db->chargePoints())
            list.append(toJson(r, db->connectorStatuses(r.id)));
        return jsonResponse({{"chargePoints", list}});
    });

    m_http.route("/api/charge_points/<arg>", QHttpServerRequest::Method::Get, [db](const QString &id) {
        const auto r = db->chargePoint(id);
        if (!r)
            return errorResponse("Unknown charge point", StatusCode::NotFound);
        QJsonObject o = toJson(*r, db->connectorStatuses(id));
        if (const auto tx = db->lastTransaction(id))
            o["lastTransaction"] = toJson(*tx);
        return jsonResponse(o);
    });

    // Transaction history; without ?limit= the whole history is returned.
    m_http.route("/api/transactions", QHttpServerRequest::Method::Get, [db](const QHttpServerRequest &req) {
        QJsonArray list;
        for (const auto &t : db->transactions({}, limitParam(req, 0)))
            list.append(toJson(t));
        return jsonResponse({{"transactions", list}});
    });

    m_http.route("/api/charge_points/<arg>/transactions", QHttpServerRequest::Method::Get,
                 [db](const QString &id, const QHttpServerRequest &req) {
        if (!db->chargePoint(id))
            return errorResponse("Unknown charge point", StatusCode::NotFound);
        QJsonArray list;
        for (const auto &t : db->transactions(id, limitParam(req, 0)))
            list.append(toJson(t));
        return jsonResponse({{"transactions", list}});
    });

    m_http.route("/api/charge_points/<arg>/messages", QHttpServerRequest::Method::Get,
                 [db](const QString &id, const QHttpServerRequest &req) {
        QJsonArray list;
        for (const auto &m : db->recentMessages(id, limitParam(req, 100)))
            list.append(toJson(m));
        return jsonResponse({{"messages", list}});
    });

    m_http.route("/api/charge_points/<arg>/meter_values", QHttpServerRequest::Method::Get,
                 [db](const QString &id, const QHttpServerRequest &req) {
        QJsonArray list;
        for (const auto &v : db->recentMeterValues(id, limitParam(req, 50)))
            list.append(toJson(v));
        return jsonResponse({{"meterValues", list}});
    });

    m_http.route("/api/charge_points/<arg>/configuration", QHttpServerRequest::Method::Get,
                 [db](const QString &id) {
        QJsonArray list;
        for (const auto &k : db->configuration(id))
            list.append(toJson(k));
        return jsonResponse({{"configuration", list}});
    });
}

/// Registers one POST command route. The HTTP response is sent when the charge point answers.
void RestApi::addCommand(const QString &name, CommandFn fn)
{
    m_http.route("/api/charge_points/<arg>/" + name, QHttpServerRequest::Method::Post,
                 [fn](const QString &id, const QHttpServerRequest &req, QHttpServerResponder &responder) {
        // The responder is move-only, but the callback must be copyable, hence the shared_ptr.
        auto out = std::make_shared<QHttpServerResponder>(std::move(responder));

        QJsonObject body;
        if (!req.body().trimmed().isEmpty()) {
            QJsonParseError err;
            const QJsonDocument doc = QJsonDocument::fromJson(req.body(), &err);
            if (err.error != QJsonParseError::NoError || !doc.isObject()) {
                out->sendResponse(errorResponse("Body must be a JSON object", StatusCode::BadRequest));
                return;
            }
            body = doc.object();
        }

        const QString problem = fn(id, body, [out](const CallOutcome &o) { out->sendResponse(outcomeResponse(o)); });
        if (!problem.isEmpty())
            out->sendResponse(errorResponse(problem, StatusCode::BadRequest));
    });
}
