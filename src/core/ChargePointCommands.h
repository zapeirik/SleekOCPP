#pragma once

#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <functional>
#include <optional>

#include "core/ocpp/ChargePointSession.h"

class Database;
class OcppServer;

/**
 * Commands the central system sends to charge points (OCPP 1.6 central system -> charge point).
 *
 * Every method takes the charge point id and a callback that is invoked exactly once with the
 * CallOutcome: a result (check outcome.summary() / outcome.payload), an error, a timeout, or
 * "Disconnected" if the charge point is not connected. Callbacks run on the main thread.
 *
 * GetConfiguration and ChangeConfiguration also keep the database's configuration cache
 * up to date and emit configurationChanged().
 *
 * To add a new command: add a method that builds the payload and calls send().
 */
class ChargePointCommands : public QObject
{
    Q_OBJECT
public:
    using Callback = ChargePointSession::CallCallback;

    /// @p server and @p db must outlive this object.
    ChargePointCommands(OcppServer *server, Database *db, QObject *parent = nullptr);

    /// Generic sender used by all commands below; also used for "raw" messages from the GUI/API.
    void send(const QString &cpId, const QString &action, const QJsonObject &payload, Callback callback = {});

    /// Reads configuration. An empty @p keys list requests all keys.
    void getConfiguration(const QString &cpId, const QStringList &keys, Callback callback = {});
    void changeConfiguration(const QString &cpId, const QString &key, const QString &value, Callback callback = {});

    /// Asks the charge point to start charging. @p connectorId empty = charge point chooses.
    /// @p chargingProfile is an optional OCPP ChargingProfile object (TxProfile).
    void remoteStartTransaction(const QString &cpId, const QString &idTag, std::optional<int> connectorId,
                                const QJsonObject &chargingProfile = QJsonObject(), Callback callback = {});
    void remoteStopTransaction(const QString &cpId, int transactionId, Callback callback = {});

    /// @p csChargingProfiles is the OCPP `csChargingProfiles` object.
    void setChargingProfile(const QString &cpId, int connectorId, const QJsonObject &csChargingProfiles,
                            Callback callback = {});
    /// All filters are optional; an empty request clears all profiles.
    /// @p purpose: ChargePointMaxProfile, TxDefaultProfile or TxProfile.
    void clearChargingProfile(const QString &cpId, std::optional<int> profileId, std::optional<int> connectorId,
                              const QString &purpose, std::optional<int> stackLevel, Callback callback = {});
    /// @p chargingRateUnit: "A" or "W" (empty = charge point default).
    void getCompositeSchedule(const QString &cpId, int connectorId, int durationSeconds,
                              const QString &chargingRateUnit = QString(), Callback callback = {});

    /// @p type: "Soft" or "Hard".
    void reset(const QString &cpId, const QString &type, Callback callback = {});
    /// @p requestedMessage e.g. "StatusNotification", "MeterValues", "Heartbeat", "BootNotification".
    void triggerMessage(const QString &cpId, const QString &requestedMessage, std::optional<int> connectorId = {},
                        Callback callback = {});
    void unlockConnector(const QString &cpId, int connectorId, Callback callback = {});
    /// @p type: "Operative" or "Inoperative".
    void changeAvailability(const QString &cpId, int connectorId, const QString &type, Callback callback = {});

signals:
    /// The cached configuration of this charge point was updated.
    void configurationChanged(const QString &chargePointId);

private:
    OcppServer *m_server;
    Database *m_db;
};
