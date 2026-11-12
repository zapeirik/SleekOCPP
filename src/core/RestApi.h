#pragma once

#include <QHostAddress>
#include <QHttpServer>
#include <QObject>
#include <functional>

#include "core/ChargePointCommands.h"

class CentralSystem;
class QTcpServer;

/**
 * JSON REST API on top of CentralSystem, for scripts and tools.
 *
 * Read endpoints (GET):
 *   /api/charge_points                         all charge points with connector status
 *   /api/charge_points/<id>                    details, connectors and last transaction
 *   /api/charge_points/<id>/messages?limit=N   message log, newest first
 *   /api/charge_points/<id>/meter_values?limit=N
 *   /api/charge_points/<id>/configuration      cached configuration
 *
 * Command endpoints (POST with a JSON body) at /api/charge_points/<id>/<command>:
 *   remote_start {idTag, connectorId?, chargingProfile?}   remote_stop {transactionId?}
 *   get_configuration {keys?}                              change_configuration {key, value}
 *   set_charging_profile {connectorId, csChargingProfiles} clear_charging_profile {id?, connectorId?, purpose?, stackLevel?}
 *   get_composite_schedule {connectorId, duration, chargingRateUnit?}
 *   reset {type}   trigger_message {requestedMessage, connectorId?}
 *   unlock_connector {connectorId}   change_availability {connectorId, type}
 *   send {action, payload?}        any OCPP action
 *
 * Command responses: {"outcome": "result"|"error"|"timeout"|"disconnected", "status": ..., "payload": {...}}.
 * HTTP codes: 200 result or CallError from the charger, 400 bad request, 404 unknown charge point
 * (GET), 409 charge point not connected, 504 no answer.
 *
 * There is no authentication: by default the API only listens on localhost.
 * To add a command: add one addCommand() call in the constructor.
 */
class RestApi : public QObject
{
    Q_OBJECT
public:
    explicit RestApi(CentralSystem *centralSystem, QObject *parent = nullptr);

    /// Starts listening. Port 0 picks a free port; see port().
    bool listen(quint16 port, const QHostAddress &address = QHostAddress::LocalHost);
    quint16 port() const;

private:
    /// Builds and sends the commands. Returns an error text if the body is invalid (nothing is sent then).
    using CommandFn = std::function<QString(const QString &cpId, const QJsonObject &body,
                                            ChargePointCommands::Callback callback)>;

    void addReadRoutes();
    /// Registers POST /api/charge_points/<id>/<name>.
    void addCommand(const QString &name, CommandFn fn);

    CentralSystem *m_cs;
    QHttpServer m_http;
    QTcpServer *m_tcp = nullptr;
};
