#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <functional>

#include "OcppMessage.h"

class QWebSocket;
class QTimer;

/// Result of an outgoing Call, delivered to the callback given to ChargePointSession::sendCall().
struct CallOutcome
{
    enum class Status {
        Result,        ///< Charge point answered with a CallResult (see payload)
        Error,         ///< Charge point answered with a CallError (see errorCode/errorDescription)
        Timeout,       ///< No answer within the timeout
        Disconnected   ///< Connection closed before an answer arrived
    };

    Status status = Status::Result;
    QString action;              ///< The action that was sent
    QJsonObject payload;         ///< Valid when status == Result
    QString errorCode;           ///< Valid when status == Error
    QString errorDescription;    ///< Valid when status == Error

    bool ok() const { return status == Status::Result; }

    /// One-line text for the UI/API: the "status" field of a result (e.g. "Accepted"),
    /// "Error: <code> <description>", "Timeout" or "Disconnected".
    QString summary() const
    {
        switch (status) {
        case Status::Result: return payload.contains("status") ? payload["status"].toString() : QStringLiteral("OK");
        case Status::Error: return QStringLiteral("Error: ") + errorCode + " " + errorDescription;
        case Status::Timeout: return QStringLiteral("Timeout");
        default: return QStringLiteral("Disconnected");
        }
    }
};

/**
 * One connected charge point: owns its WebSocket and speaks OCPP-J over it.
 *
 * Responsibilities:
 *  - parse incoming frames and emit callReceived() for Calls from the charge point
 *  - send Calls to the charge point and match the answers by unique id (with timeout)
 *  - send CallResult / CallError replies to the charge point's Calls
 *  - emit messageTraced() for every frame in either direction (used for the message log)
 *
 * The session does not know what any action means; that is the job of the handler
 * registered on OcppServer.
 */
class ChargePointSession : public QObject
{
    Q_OBJECT
public:
    using CallCallback = std::function<void(const CallOutcome &)>;

    /// Takes ownership of @p socket.
    ChargePointSession(QWebSocket *socket, const QString &chargePointId, QObject *parent = nullptr);

    QString id() const { return m_id; }
    bool isConnected() const;

    /// Remote IP address of the charge point.
    QString peerAddress() const;

    /// How long to wait for an answer to our Calls (default 30 s).
    void setCallTimeoutMs(int ms) { m_timeoutMs = ms; }

    /**
     * Sends a Call to the charge point.
     * @param callback Invoked exactly once with the outcome (result, error, timeout or disconnect).
     * @return the unique id used on the wire, or an empty string if not connected
     *         (the callback is then invoked immediately with Disconnected).
     */
    QString sendCall(const QString &action, const QJsonObject &payload, CallCallback callback = {});

    /// Replies to a Call from the charge point.
    void sendResult(const QString &uniqueId, const QJsonObject &payload = QJsonObject());
    void sendError(const QString &uniqueId, const QString &code, const QString &description = QString());

    /// Closes the WebSocket.
    void close();

signals:
    /// A Call from the charge point arrived and must be answered with sendResult()/sendError().
    void callReceived(const OcppMessage &message);
    /// Every raw frame, @p incoming is true for charge point -> central system.
    void messageTraced(bool incoming, const QString &rawText);
    /// The WebSocket closed. All pending calls have already been failed with Disconnected.
    void disconnected();

private slots:
    void onTextMessage(const QString &text);
    void onSocketDisconnected();

private:
    struct Pending
    {
        QString action;
        CallCallback callback;
        QTimer *timer = nullptr;
    };

    void sendRaw(const OcppMessage &message);
    void finishPending(const QString &uniqueId, const CallOutcome &outcome);

    QPointer<QWebSocket> m_socket;
    QString m_id;
    int m_timeoutMs = 30000;
    QHash<QString, Pending> m_pending;
};
