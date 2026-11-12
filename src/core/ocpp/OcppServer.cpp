#include "OcppServer.h"

#include <QDebug>
#include <QUrl>
#include <QWebSocket>
#include <QWebSocketServer>

OcppServer::OcppServer(QObject *parent)
    : QObject(parent),
      m_server(new QWebSocketServer("SleekOCPP", QWebSocketServer::NonSecureMode, this))
{
    m_server->setSupportedSubprotocols({"ocpp1.6"});
    connect(m_server, &QWebSocketServer::newConnection, this, &OcppServer::onNewConnection);
}

OcppServer::~OcppServer()
{
    stop();
}

bool OcppServer::listen(quint16 port, const QHostAddress &address)
{
    if (!m_server->listen(address, port)) {
        qCritical().noquote() << "OCPP server cannot listen on port" << port << ":" << m_server->errorString();
        return false;
    }
    qInfo().noquote() << "OCPP server listening on port" << m_server->serverPort();
    return true;
}

quint16 OcppServer::port() const { return m_server->serverPort(); }
QString OcppServer::errorString() const { return m_server->errorString(); }

void OcppServer::stop()
{
    m_server->close();
    const auto sessions = m_sessions.values();
    for (ChargePointSession *s : sessions)
        s->close();
}

ChargePointSession *OcppServer::session(const QString &chargePointId) const
{
    return m_sessions.value(chargePointId, nullptr);
}

QStringList OcppServer::connectedIds() const
{
    return m_sessions.keys();
}

QString OcppServer::idFromPath(const QString &path)
{
    const QStringList parts = path.split('/', Qt::SkipEmptyParts);
    return parts.isEmpty() ? QString() : QUrl::fromPercentEncoding(parts.last().toUtf8());
}

/// Wraps each new WebSocket in a ChargePointSession and wires it to the call handler.
void OcppServer::onNewConnection()
{
    QWebSocket *socket = m_server->nextPendingConnection();
    const QString id = idFromPath(socket->requestUrl().path());
    if (id.isEmpty()) {
        qWarning().noquote() << "Rejecting connection without charge point id in URL:" << socket->requestUrl().toString();
        socket->close(QWebSocketProtocol::CloseCodePolicyViolated, "Missing charge point id");
        socket->deleteLater();
        return;
    }
    if (socket->subprotocol() != QLatin1String("ocpp1.6"))
        qWarning().noquote() << id << "connected without the ocpp1.6 subprotocol";

    // A reconnect with the same id replaces the old (probably stale) connection.
    if (ChargePointSession *old = m_sessions.take(id)) {
        qInfo().noquote() << id << "reconnected, closing previous session";
        old->close();
        old->deleteLater();
    }

    auto *session = new ChargePointSession(socket, id, this);
    m_sessions.insert(id, session);

    connect(session, &ChargePointSession::callReceived, this, [this, session](const OcppMessage &call) {
        if (m_handler) {
            m_handler(session, call);
        } else {
            session->sendError(call.uniqueId, "NotImplemented", call.action + " is not supported");
        }
    });
    connect(session, &ChargePointSession::disconnected, this, [this, session, id] {
        // Only report if this is still the current session for the id.
        if (m_sessions.value(id) == session) {
            m_sessions.remove(id);
            qInfo().noquote() << id << "disconnected";
            emit chargePointDisconnected(id);
        }
        session->deleteLater();
    });

    qInfo().noquote() << id << "connected from" << socket->peerAddress().toString();
    emit chargePointConnected(session);
}
