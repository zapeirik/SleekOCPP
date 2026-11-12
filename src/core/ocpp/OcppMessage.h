#pragma once

#include <QJsonObject>
#include <QString>
#include <optional>

/**
 * One OCPP-J message (OCPP 1.6 over WebSocket), either received or about to be sent.
 *
 * Wire formats:
 *   Call:       [2, "<uniqueId>", "<action>", {payload}]
 *   CallResult: [3, "<uniqueId>", {payload}]
 *   CallError:  [4, "<uniqueId>", "<errorCode>", "<errorDescription>", {errorDetails}]
 *
 * Use the static factory functions to build messages and parse() for incoming text.
 */
struct OcppMessage
{
    enum class Type { Call = 2, CallResult = 3, CallError = 4 };

    Type type = Type::Call;
    QString uniqueId;
    QString action;              ///< Call only, e.g. "BootNotification"
    QJsonObject payload;         ///< Call and CallResult
    QString errorCode;           ///< CallError only, e.g. "NotImplemented"
    QString errorDescription;    ///< CallError only
    QJsonObject errorDetails;    ///< CallError only

    /// Parses OCPP-J text. Returns nullopt and fills @p error if the frame is malformed.
    static std::optional<OcppMessage> parse(const QString &text, QString *error = nullptr);

    /// Serializes to the compact OCPP-J JSON array.
    QString toJson() const;

    static OcppMessage makeCall(const QString &uniqueId, const QString &action, const QJsonObject &payload);
    static OcppMessage makeResult(const QString &uniqueId, const QJsonObject &payload);
    static OcppMessage makeError(const QString &uniqueId, const QString &code,
                                 const QString &description = QString(),
                                 const QJsonObject &details = QJsonObject());
};
