#pragma once

#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QStringList>
#include <functional>

#include "ChargePointSession.h"

class QWebSocketServer;

/**
 * WebSocket server that chargers connect to: ws://host:port/<chargePointId>
 *
 * - Accepts any charge point (no authentication yet) that offers the "ocpp1.6" subprotocol.
 * - The charge point id is the last segment of the URL path.
 * - Creates one ChargePointSession per connection. If the same id connects again,
 *   the old session is closed and replaced.
 * - Incoming Calls are passed to the call handler (see setCallHandler()). Without a
 *   handler every Call is answered with CallError NotImplemented.
 */
class OcppServer : public QObject
{
    Q_OBJECT
public:
    /// Called for every Call from a charge point. Must answer via session->sendResult/sendError.
    using CallHandler = std::function<void(ChargePointSession *session, const OcppMessage &call)>;

    explicit OcppServer(QObject *parent = nullptr);
    ~OcppServer() override;

    /// Starts listening. Port 0 picks a free port (useful in tests); see port().
    bool listen(quint16 port, const QHostAddress &address = QHostAddress::Any);
    quint16 port() const;
    QString errorString() const;
    void stop();

    void setCallHandler(CallHandler handler) { m_handler = std::move(handler); }

    /// The live session for a charge point id, or nullptr if it is not connected.
    ChargePointSession *session(const QString &chargePointId) const;
    QStringList connectedIds() const;

signals:
    /// A charge point connected. Connect to the session's signals here if needed.
    void chargePointConnected(ChargePointSession *session);
    /// A charge point's connection closed (not emitted for a session replaced by a reconnect).
    void chargePointDisconnected(const QString &chargePointId);

private slots:
    void onNewConnection();

private:
    /// Extracts the charge point id from a request path such as "/ocpp/CP001".
    static QString idFromPath(const QString &path);

    QWebSocketServer *m_server;
    QHash<QString, ChargePointSession *> m_sessions;
    CallHandler m_handler;
};
