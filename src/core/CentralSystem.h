#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>

#include "core/ocpp/OcppMessage.h"

class ChargePointSession;
class ChargePointCommands;
class Database;
class OcppServer;

/**
 * The central system logic: connects the OCPP server to the database and answers the
 * messages that charge points send (OCPP 1.6 "Core" profile).
 *
 * Handled actions: BootNotification, Heartbeat, StatusNotification, Authorize,
 * StartTransaction, StopTransaction, MeterValues, DataTransfer,
 * DiagnosticsStatusNotification, FirmwareStatusNotification.
 * Everything else is answered with CallError NotImplemented.
 *
 * Also records every frame in the message log and keeps connect/disconnect times.
 * The GUI and REST API listen to the signals below to know when to refresh.
 *
 * To support a new incoming action: write a `Reply handleXxx(...)` method and register it
 * in the constructor's handler table.
 */
class CentralSystem : public QObject
{
    Q_OBJECT
public:
    /// @p server and @p db must outlive this object. Installs itself as the server's call handler.
    CentralSystem(OcppServer *server, Database *db, QObject *parent = nullptr);

    /// Heartbeat interval (seconds) announced in BootNotification replies. Default 60.
    void setHeartbeatInterval(int seconds) { m_heartbeatInterval = seconds; }

    OcppServer *server() const { return m_server; }
    Database *database() const { return m_db; }
    /// Commands to send to charge points (RemoteStart, ChangeConfiguration, ...).
    ChargePointCommands *commands() const { return m_commands; }

signals:
    /// Something about this charge point changed (connection, status, transaction, meter values ...).
    void chargePointChanged(const QString &chargePointId);
    /// A frame was added to the message log of this charge point.
    void messageLogged(const QString &chargePointId);

private:
    /// What a handler returns: either a CallResult payload or a CallError.
    struct Reply
    {
        QJsonObject payload;
        QString errorCode;           ///< Non-empty = send CallError
        QString errorDescription;

        static Reply ok(const QJsonObject &payload = QJsonObject()) { return {payload, {}, {}}; }
        static Reply error(const QString &code, const QString &description) { return {{}, code, description}; }
    };
    using Handler = Reply (CentralSystem::*)(const QString &cpId, const QJsonObject &payload);

    void onConnected(ChargePointSession *session);
    void onCall(ChargePointSession *session, const OcppMessage &call);
    void logFrame(const QString &cpId, bool incoming, const QString &raw);

    Reply handleBootNotification(const QString &cpId, const QJsonObject &p);
    Reply handleHeartbeat(const QString &cpId, const QJsonObject &p);
    Reply handleStatusNotification(const QString &cpId, const QJsonObject &p);
    Reply handleAuthorize(const QString &cpId, const QJsonObject &p);
    Reply handleStartTransaction(const QString &cpId, const QJsonObject &p);
    Reply handleStopTransaction(const QString &cpId, const QJsonObject &p);
    Reply handleMeterValues(const QString &cpId, const QJsonObject &p);
    Reply handleDataTransfer(const QString &cpId, const QJsonObject &p);
    Reply handleEmptyNotification(const QString &cpId, const QJsonObject &p);

    /// Returns the first of @p keys missing in @p payload, or an empty string if all are present.
    static QString missingField(const QJsonObject &payload, const QStringList &keys);
    static QJsonObject idTagInfo(const QString &status);
    static QString currentTime();

    OcppServer *m_server;
    Database *m_db;
    ChargePointCommands *m_commands;
    QHash<QString, Handler> m_handlers;
    int m_heartbeatInterval = 60;
};
