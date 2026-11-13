#include "CentralSystem.h"

#include <QDateTime>
#include <QDebug>
#include <QJsonArray>

#include "core/db/Database.h"
#include "core/ChargePointCommands.h"
#include "core/ocpp/ChargePointSession.h"
#include "core/ocpp/OcppServer.h"

CentralSystem::CentralSystem(OcppServer *server, Database *db, QObject *parent)
    : QObject(parent), m_server(server), m_db(db), m_commands(new ChargePointCommands(server, db, this))
{
    // Handler table: OCPP action name -> member function. Add new incoming actions here.
    m_handlers = {
        {"BootNotification", &CentralSystem::handleBootNotification},
        {"Heartbeat", &CentralSystem::handleHeartbeat},
        {"StatusNotification", &CentralSystem::handleStatusNotification},
        {"Authorize", &CentralSystem::handleAuthorize},
        {"StartTransaction", &CentralSystem::handleStartTransaction},
        {"StopTransaction", &CentralSystem::handleStopTransaction},
        {"MeterValues", &CentralSystem::handleMeterValues},
        {"DataTransfer", &CentralSystem::handleDataTransfer},
        {"DiagnosticsStatusNotification", &CentralSystem::handleEmptyNotification},
        {"FirmwareStatusNotification", &CentralSystem::handleEmptyNotification},
    };

    // Anything left "connected" in the database is from a previous run.
    m_db->markAllDisconnected();

    m_server->setCallHandler([this](ChargePointSession *s, const OcppMessage &call) { onCall(s, call); });
    connect(m_server, &OcppServer::chargePointConnected, this, &CentralSystem::onConnected);
    connect(m_server, &OcppServer::chargePointDisconnected, this, [this](const QString &id) {
        m_db->chargePointDisconnected(id);
        emit chargePointChanged(id);
    });
}

void CentralSystem::onConnected(ChargePointSession *session)
{
    const QString id = session->id();
    m_db->chargePointConnected(id, session->peerAddress());
    connect(session, &ChargePointSession::messageTraced, this,
            [this, id](bool incoming, const QString &raw) { logFrame(id, incoming, raw); });
    emit chargePointChanged(id);
}

/// Writes one frame to the message log. Frames that fail to parse are logged as type "Invalid".
void CentralSystem::logFrame(const QString &cpId, bool incoming, const QString &raw)
{
    const auto msg = OcppMessage::parse(raw);
    QString type = "Invalid", action, uid;
    if (msg) {
        uid = msg->uniqueId;
        action = msg->action;
        switch (msg->type) {
        case OcppMessage::Type::Call: type = "Call"; break;
        case OcppMessage::Type::CallResult: type = "CallResult"; break;
        case OcppMessage::Type::CallError: type = "CallError"; break;
        }
    }
    m_db->logMessage(cpId, incoming, type, action, uid, raw);
    emit messageLogged(cpId);
}

/// Dispatches a Call from a charge point to its handler and sends the reply.
void CentralSystem::onCall(ChargePointSession *session, const OcppMessage &call)
{
    const QString id = session->id();
    const Handler handler = m_handlers.value(call.action, nullptr);
    if (!handler) {
        qInfo().noquote() << id << "unsupported action" << call.action;
        session->sendError(call.uniqueId, "NotImplemented", call.action + " is not supported");
        return;
    }

    const Reply reply = (this->*handler)(id, call.payload);
    if (reply.errorCode.isEmpty())
        session->sendResult(call.uniqueId, reply.payload);
    else
        session->sendError(call.uniqueId, reply.errorCode, reply.errorDescription);
    emit chargePointChanged(id);
}

QString CentralSystem::missingField(const QJsonObject &payload, const QStringList &keys)
{
    for (const QString &k : keys) {
        if (!payload.contains(k))
            return k;
    }
    return QString();
}

QJsonObject CentralSystem::idTagInfo(const QString &status)
{
    return {{"status", status}};
}

QString CentralSystem::currentTime()
{
    return Database::toDb(QDateTime::currentDateTimeUtc());
}

/// Accepts every charge point and stores its identity.
CentralSystem::Reply CentralSystem::handleBootNotification(const QString &cpId, const QJsonObject &p)
{
    const QString missing = missingField(p, {"chargePointVendor", "chargePointModel"});
    if (!missing.isEmpty())
        return Reply::error("FormationViolation", "Missing " + missing);

    m_db->updateBootInfo(cpId, p["chargePointVendor"].toString(), p["chargePointModel"].toString(),
                         p["chargePointSerialNumber"].toString(), p["firmwareVersion"].toString());
    qInfo().noquote() << cpId << "boot:" << p["chargePointVendor"].toString() << p["chargePointModel"].toString();
    return Reply::ok({{"status", "Accepted"}, {"currentTime", currentTime()}, {"interval", m_heartbeatInterval}});
}

CentralSystem::Reply CentralSystem::handleHeartbeat(const QString &cpId, const QJsonObject &)
{
    m_db->touchHeartbeat(cpId);
    return Reply::ok({{"currentTime", currentTime()}});
}

/// Stores the connector's current status; this is what the GUI shows as the charger status.
CentralSystem::Reply CentralSystem::handleStatusNotification(const QString &cpId, const QJsonObject &p)
{
    const QString missing = missingField(p, {"connectorId", "errorCode", "status"});
    if (!missing.isEmpty())
        return Reply::error("FormationViolation", "Missing " + missing);

    m_db->saveConnectorStatus(cpId, p["connectorId"].toInt(), p["status"].toString(),
                              p["errorCode"].toString(), p["info"].toString(),
                              Database::parseOcppTime(p["timestamp"].toString()));
    return Reply::ok();
}

/// Accepts every idTag (local development setup).
CentralSystem::Reply CentralSystem::handleAuthorize(const QString &, const QJsonObject &p)
{
    if (!p.contains("idTag"))
        return Reply::error("FormationViolation", "Missing idTag");
    return Reply::ok({{"idTagInfo", idTagInfo("Accepted")}});
}

/// Creates a transaction and returns its id to the charge point.
CentralSystem::Reply CentralSystem::handleStartTransaction(const QString &cpId, const QJsonObject &p)
{
    const QString missing = missingField(p, {"connectorId", "idTag", "meterStart", "timestamp"});
    if (!missing.isEmpty())
        return Reply::error("FormationViolation", "Missing " + missing);

    const qint64 txId = m_db->startTransaction(cpId, p["connectorId"].toInt(), p["idTag"].toString(),
                                               static_cast<qint64>(p["meterStart"].toDouble()),
                                               Database::parseOcppTime(p["timestamp"].toString()));
    if (txId == 0)
        return Reply::error("InternalError", "Could not store transaction");
    qInfo().noquote() << cpId << "transaction" << txId << "started on connector" << p["connectorId"].toInt();
    return Reply::ok({{"transactionId", static_cast<int>(txId)}, {"idTagInfo", idTagInfo("Accepted")}});
}

/// Closes the transaction. Unknown ids are still accepted so the charge point can drop its copy.
CentralSystem::Reply CentralSystem::handleStopTransaction(const QString &cpId, const QJsonObject &p)
{
    const QString missing = missingField(p, {"transactionId", "meterStop", "timestamp"});
    if (!missing.isEmpty())
        return Reply::error("FormationViolation", "Missing " + missing);

    const qint64 txId = static_cast<qint64>(p["transactionId"].toDouble());
    if (!m_db->stopTransaction(txId, static_cast<qint64>(p["meterStop"].toDouble()),
                               Database::parseOcppTime(p["timestamp"].toString()), p["reason"].toString()))
        qWarning().noquote() << cpId << "StopTransaction for unknown transaction" << txId;

    // Transaction data (meter values taken during the transaction) is stored like normal meter values,
    // on the connector the transaction ran on.
    const QJsonArray data = p["transactionData"].toArray();
    if (!data.isEmpty()) {
        const auto tx = m_db->transaction(txId);
        m_db->saveMeterValues(cpId, tx ? tx->connectorId : 0, txId, data, "StopTransaction");
    }
    return Reply::ok({{"idTagInfo", idTagInfo("Accepted")}});
}

CentralSystem::Reply CentralSystem::handleMeterValues(const QString &cpId, const QJsonObject &p)
{
    const QString missing = missingField(p, {"connectorId", "meterValue"});
    if (!missing.isEmpty())
        return Reply::error("FormationViolation", "Missing " + missing);

    std::optional<qint64> txId;
    if (p.contains("transactionId"))
        txId = static_cast<qint64>(p["transactionId"].toDouble());
    m_db->saveMeterValues(cpId, p["connectorId"].toInt(), txId, p["meterValue"].toArray());
    return Reply::ok();
}

/// No vendor extensions are known, so every DataTransfer is rejected politely.
CentralSystem::Reply CentralSystem::handleDataTransfer(const QString &, const QJsonObject &p)
{
    if (!p.contains("vendorId"))
        return Reply::error("FormationViolation", "Missing vendorId");
    return Reply::ok({{"status", "UnknownVendorId"}});
}

/// For notifications whose reply is an empty object.
CentralSystem::Reply CentralSystem::handleEmptyNotification(const QString &, const QJsonObject &)
{
    return Reply::ok();
}
