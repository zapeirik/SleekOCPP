#include "ChargePointCommands.h"

#include <QJsonArray>

#include "core/db/Database.h"
#include "core/ocpp/OcppServer.h"

ChargePointCommands::ChargePointCommands(OcppServer *server, Database *db, QObject *parent)
    : QObject(parent), m_server(server), m_db(db)
{
}

void ChargePointCommands::send(const QString &cpId, const QString &action, const QJsonObject &payload,
                               Callback callback)
{
    ChargePointSession *session = m_server->session(cpId);
    if (!session) {
        if (callback) {
            CallOutcome o;
            o.status = CallOutcome::Status::Disconnected;
            o.action = action;
            callback(o);
        }
        return;
    }
    session->sendCall(action, payload, std::move(callback));
}

/// Sends GetConfiguration and caches the keys in the database when the answer arrives.
void ChargePointCommands::getConfiguration(const QString &cpId, const QStringList &keys, Callback callback)
{
    QJsonObject payload;
    if (!keys.isEmpty())
        payload["key"] = QJsonArray::fromStringList(keys);

    const bool all = keys.isEmpty();
    send(cpId, "GetConfiguration", payload, [this, cpId, all, callback](const CallOutcome &o) {
        if (o.ok()) {
            QList<ConfigKeyRecord> list;
            for (const QJsonValue &v : o.payload["configurationKey"].toArray()) {
                const QJsonObject k = v.toObject();
                ConfigKeyRecord r;
                r.key = k["key"].toString();
                r.value = k["value"].toString();
                r.readonly = k["readonly"].toBool();
                list.append(r);
            }
            m_db->saveConfiguration(cpId, list, all);
            emit configurationChanged(cpId);
        }
        if (callback)
            callback(o);
    });
}

/// Sends ChangeConfiguration; the cache is updated when the charge point accepted the value.
void ChargePointCommands::changeConfiguration(const QString &cpId, const QString &key, const QString &value,
                                              Callback callback)
{
    send(cpId, "ChangeConfiguration", {{"key", key}, {"value", value}},
         [this, cpId, key, value, callback](const CallOutcome &o) {
             const QString status = o.payload["status"].toString();
             // RebootRequired means the value was stored but only takes effect after a reset.
             if (o.ok() && (status == "Accepted" || status == "RebootRequired")) {
                 m_db->setConfigurationValue(cpId, key, value);
                 emit configurationChanged(cpId);
             }
             if (callback)
                 callback(o);
         });
}

void ChargePointCommands::remoteStartTransaction(const QString &cpId, const QString &idTag,
                                                 std::optional<int> connectorId,
                                                 const QJsonObject &chargingProfile, Callback callback)
{
    QJsonObject p{{"idTag", idTag}};
    if (connectorId)
        p["connectorId"] = *connectorId;
    if (!chargingProfile.isEmpty())
        p["chargingProfile"] = chargingProfile;
    send(cpId, "RemoteStartTransaction", p, std::move(callback));
}

void ChargePointCommands::remoteStopTransaction(const QString &cpId, int transactionId, Callback callback)
{
    send(cpId, "RemoteStopTransaction", {{"transactionId", transactionId}}, std::move(callback));
}

void ChargePointCommands::setChargingProfile(const QString &cpId, int connectorId,
                                             const QJsonObject &csChargingProfiles, Callback callback)
{
    send(cpId, "SetChargingProfile", {{"connectorId", connectorId}, {"csChargingProfiles", csChargingProfiles}},
         std::move(callback));
}

void ChargePointCommands::clearChargingProfile(const QString &cpId, std::optional<int> profileId,
                                               std::optional<int> connectorId, const QString &purpose,
                                               std::optional<int> stackLevel, Callback callback)
{
    QJsonObject p;
    if (profileId)
        p["id"] = *profileId;
    if (connectorId)
        p["connectorId"] = *connectorId;
    if (!purpose.isEmpty())
        p["chargingProfilePurpose"] = purpose;
    if (stackLevel)
        p["stackLevel"] = *stackLevel;
    send(cpId, "ClearChargingProfile", p, std::move(callback));
}

void ChargePointCommands::getCompositeSchedule(const QString &cpId, int connectorId, int durationSeconds,
                                               const QString &chargingRateUnit, Callback callback)
{
    QJsonObject p{{"connectorId", connectorId}, {"duration", durationSeconds}};
    if (!chargingRateUnit.isEmpty())
        p["chargingRateUnit"] = chargingRateUnit;
    send(cpId, "GetCompositeSchedule", p, std::move(callback));
}

void ChargePointCommands::reset(const QString &cpId, const QString &type, Callback callback)
{
    send(cpId, "Reset", {{"type", type}}, std::move(callback));
}

void ChargePointCommands::triggerMessage(const QString &cpId, const QString &requestedMessage,
                                         std::optional<int> connectorId, Callback callback)
{
    QJsonObject p{{"requestedMessage", requestedMessage}};
    if (connectorId)
        p["connectorId"] = *connectorId;
    send(cpId, "TriggerMessage", p, std::move(callback));
}

void ChargePointCommands::unlockConnector(const QString &cpId, int connectorId, Callback callback)
{
    send(cpId, "UnlockConnector", {{"connectorId", connectorId}}, std::move(callback));
}

void ChargePointCommands::changeAvailability(const QString &cpId, int connectorId, const QString &type,
                                             Callback callback)
{
    send(cpId, "ChangeAvailability", {{"connectorId", connectorId}, {"type", type}}, std::move(callback));
}
