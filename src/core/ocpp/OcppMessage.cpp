#include "OcppMessage.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace {
std::optional<OcppMessage> fail(QString *error, const QString &why)
{
    if (error)
        *error = why;
    return std::nullopt;
}
} // namespace

std::optional<OcppMessage> OcppMessage::parse(const QString &text, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return fail(error, "Invalid JSON: " + parseError.errorString());
    if (!doc.isArray())
        return fail(error, "Frame is not a JSON array");

    const QJsonArray a = doc.array();
    if (a.size() < 3 || !a[0].isDouble() || !a[1].isString())
        return fail(error, "Frame must start with [type, uniqueId, ...]");

    OcppMessage m;
    m.uniqueId = a[1].toString();

    switch (a[0].toInt()) {
    case 2:
        if (a.size() != 4 || !a[2].isString() || !a[3].isObject())
            return fail(error, "Malformed Call");
        m.type = Type::Call;
        m.action = a[2].toString();
        m.payload = a[3].toObject();
        break;
    case 3:
        if (a.size() != 3 || !a[2].isObject())
            return fail(error, "Malformed CallResult");
        m.type = Type::CallResult;
        m.payload = a[2].toObject();
        break;
    case 4:
        if (a.size() < 3 || !a[2].isString())
            return fail(error, "Malformed CallError");
        m.type = Type::CallError;
        m.errorCode = a[2].toString();
        m.errorDescription = a.size() > 3 ? a[3].toString() : QString();
        m.errorDetails = a.size() > 4 ? a[4].toObject() : QJsonObject();
        break;
    default:
        return fail(error, "Unknown message type id");
    }
    return m;
}

QString OcppMessage::toJson() const
{
    QJsonArray a;
    a.append(static_cast<int>(type));
    a.append(uniqueId);
    switch (type) {
    case Type::Call:
        a.append(action);
        a.append(payload);
        break;
    case Type::CallResult:
        a.append(payload);
        break;
    case Type::CallError:
        a.append(errorCode);
        a.append(errorDescription);
        a.append(errorDetails);
        break;
    }
    return QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact));
}

OcppMessage OcppMessage::makeCall(const QString &uniqueId, const QString &action, const QJsonObject &payload)
{
    OcppMessage m;
    m.type = Type::Call;
    m.uniqueId = uniqueId;
    m.action = action;
    m.payload = payload;
    return m;
}

OcppMessage OcppMessage::makeResult(const QString &uniqueId, const QJsonObject &payload)
{
    OcppMessage m;
    m.type = Type::CallResult;
    m.uniqueId = uniqueId;
    m.payload = payload;
    return m;
}

OcppMessage OcppMessage::makeError(const QString &uniqueId, const QString &code,
                                   const QString &description, const QJsonObject &details)
{
    OcppMessage m;
    m.type = Type::CallError;
    m.uniqueId = uniqueId;
    m.errorCode = code;
    m.errorDescription = description;
    m.errorDetails = details;
    return m;
}
