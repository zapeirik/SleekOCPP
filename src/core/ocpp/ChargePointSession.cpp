#include "ChargePointSession.h"

#include <QAbstractSocket>
#include <QDebug>
#include <QTimer>
#include <QUuid>
#include <QWebSocket>

ChargePointSession::ChargePointSession(QWebSocket *socket, const QString &chargePointId, QObject *parent)
    : QObject(parent), m_socket(socket), m_id(chargePointId)
{
    socket->setParent(this);
    connect(socket, &QWebSocket::textMessageReceived, this, &ChargePointSession::onTextMessage);
    connect(socket, &QWebSocket::disconnected, this, &ChargePointSession::onSocketDisconnected);
}

bool ChargePointSession::isConnected() const
{
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

QString ChargePointSession::peerAddress() const
{
    return m_socket ? m_socket->peerAddress().toString() : QString();
}

void ChargePointSession::close()
{
    if (m_socket)
        m_socket->close();
}

QString ChargePointSession::sendCall(const QString &action, const QJsonObject &payload, CallCallback callback)
{
    if (!isConnected()) {
        if (callback) {
            CallOutcome o;
            o.status = CallOutcome::Status::Disconnected;
            o.action = action;
            callback(o);
        }
        return QString();
    }

    const QString uid = QUuid::createUuid().toString(QUuid::WithoutBraces);

    // The timer is a child of this session, so it disappears with it.
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, uid, action] {
        CallOutcome o;
        o.status = CallOutcome::Status::Timeout;
        o.action = action;
        finishPending(uid, o);
    });
    m_pending.insert(uid, Pending{action, std::move(callback), timer});
    timer->start(m_timeoutMs);

    sendRaw(OcppMessage::makeCall(uid, action, payload));
    return uid;
}

void ChargePointSession::sendResult(const QString &uniqueId, const QJsonObject &payload)
{
    sendRaw(OcppMessage::makeResult(uniqueId, payload));
}

void ChargePointSession::sendError(const QString &uniqueId, const QString &code, const QString &description)
{
    sendRaw(OcppMessage::makeError(uniqueId, code, description));
}

void ChargePointSession::sendRaw(const OcppMessage &message)
{
    if (!isConnected())
        return;
    const QString text = message.toJson();
    m_socket->sendTextMessage(text);
    emit messageTraced(false, text);
}

/// Parses one incoming frame and routes it: Calls go to callReceived(), answers complete pending calls.
void ChargePointSession::onTextMessage(const QString &text)
{
    emit messageTraced(true, text);

    QString error;
    const auto parsed = OcppMessage::parse(text, &error);
    if (!parsed) {
        qWarning().noquote() << m_id << "invalid frame:" << error;
        return;
    }
    const OcppMessage &msg = *parsed;

    switch (msg.type) {
    case OcppMessage::Type::Call:
        emit callReceived(msg);
        break;
    case OcppMessage::Type::CallResult:
    case OcppMessage::Type::CallError: {
        auto it = m_pending.constFind(msg.uniqueId);
        if (it == m_pending.constEnd()) {
            qWarning().noquote() << m_id << "answer for unknown call id" << msg.uniqueId;
            return;
        }
        CallOutcome o;
        o.action = it->action;
        if (msg.type == OcppMessage::Type::CallResult) {
            o.status = CallOutcome::Status::Result;
            o.payload = msg.payload;
        } else {
            o.status = CallOutcome::Status::Error;
            o.errorCode = msg.errorCode;
            o.errorDescription = msg.errorDescription;
        }
        finishPending(msg.uniqueId, o);
        break;
    }
    }
}

void ChargePointSession::onSocketDisconnected()
{
    // Fail all outstanding calls so callers are never left waiting.
    const QStringList ids = m_pending.keys();
    for (const QString &uid : ids) {
        CallOutcome o;
        o.status = CallOutcome::Status::Disconnected;
        o.action = m_pending.value(uid).action;
        finishPending(uid, o);
    }
    emit disconnected();
}

/// Removes the pending entry (if still present) and invokes its callback once.
void ChargePointSession::finishPending(const QString &uniqueId, const CallOutcome &outcome)
{
    auto it = m_pending.find(uniqueId);
    if (it == m_pending.end())
        return;
    Pending p = std::move(it.value());
    m_pending.erase(it);
    p.timer->deleteLater();
    if (p.callback)
        p.callback(outcome);
}
