#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QList>
#include <QMetaType>
#include <QSqlDatabase>
#include <QString>
#include <QVariantList>
#include <optional>

class QSqlQuery;

/// A charge point as stored in the database. Times are UTC; null if the event never happened.
struct ChargePointRecord
{
    QString id;
    QString vendor, model, serial, firmware;   ///< From BootNotification
    bool connected = false;
    QString remoteAddress;
    QDateTime firstSeen, lastConnected, lastDisconnected;
    QDateTime lastHeartbeat, lastMeterValue;
};
Q_DECLARE_METATYPE(ChargePointRecord)

/// Latest StatusNotification for one connector (connector 0 = the charge point itself).
struct ConnectorStatusRecord
{
    int connectorId = 0;
    QString status;       ///< e.g. Available, Charging, Faulted
    QString errorCode;    ///< e.g. NoError
    QString info;
    QDateTime updatedAt;
};

/// One charging transaction (StartTransaction .. StopTransaction).
struct TransactionRecord
{
    qint64 id = 0;                 ///< The transactionId we handed to the charge point
    QString chargePointId;
    int connectorId = 0;
    QString idTag;
    qint64 meterStart = 0;         ///< Wh
    QDateTime startTime;
    std::optional<qint64> meterStop;
    QDateTime stopTime;            ///< Null while the transaction is running
    QString stopReason;

    bool isActive() const { return !stopTime.isValid(); }
};

/// One logged WebSocket frame.
struct MessageRecord
{
    qint64 id = 0;
    QString chargePointId;
    QDateTime timestamp;
    bool incoming = true;          ///< true = charge point -> central system
    QString type;                  ///< "Call", "CallResult" or "CallError"
    QString action;                ///< For results/errors: the action of the matching Call
    QString uniqueId;
    QString raw;                   ///< Complete JSON text as sent
};

/// One sampled value from a MeterValues message.
struct MeterValueRecord
{
    qint64 id = 0;
    QString chargePointId;
    int connectorId = 0;
    std::optional<qint64> transactionId;
    QDateTime timestamp;
    QString measurand;             ///< e.g. Energy.Active.Import.Register
    QString value;
    QString unit, context, phase, location;
};

/// One cached configuration key of a charge point (from GetConfiguration / ChangeConfiguration).
struct ConfigKeyRecord
{
    QString key;
    QString value;
    bool readonly = false;
    QDateTime updatedAt;
};

/**
 * SQLite persistence for everything the central system needs to remember.
 *
 * Single connection, used from the main thread only. Every write method logs a warning
 * and returns false/0 on SQL errors instead of throwing. Times are stored as UTC ISO-8601
 * strings. The schema is versioned with PRAGMA user_version; to change the schema add a
 * step in migrate() and bump kSchemaVersion.
 *
 * Tables: charge_points, connector_status, status_history, transactions, meter_values,
 * messages, config_cache.
 */
class Database
{
public:
    Database();
    ~Database();
    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    /// Opens (creating if needed) the database file and migrates the schema. Use ":memory:" in tests.
    bool open(const QString &path, QString *error = nullptr);

    // ---- charge point lifecycle -------------------------------------------------------
    /// Creates the charge point if unknown and marks it connected.
    void chargePointConnected(const QString &id, const QString &remoteAddress);
    void chargePointDisconnected(const QString &id);
    /// Marks every charge point disconnected. Call at start-up (leftovers from a crash).
    void markAllDisconnected();
    /// Stores the BootNotification details.
    void updateBootInfo(const QString &id, const QString &vendor, const QString &model,
                        const QString &serial, const QString &firmware);
    void touchHeartbeat(const QString &id);

    // ---- status, transactions, meter values --------------------------------------------
    /// Updates the current connector status and appends to the status history.
    void saveConnectorStatus(const QString &id, int connectorId, const QString &status,
                             const QString &errorCode, const QString &info, const QDateTime &timestamp);
    /// Stores a new transaction and returns its id (to be used as OCPP transactionId), or 0 on error.
    qint64 startTransaction(const QString &id, int connectorId, const QString &idTag,
                            qint64 meterStart, const QDateTime &timestamp);
    /// Closes a transaction. Returns false if the transaction id is unknown.
    bool stopTransaction(qint64 transactionId, qint64 meterStop, const QDateTime &timestamp,
                         const QString &reason);
    /// Stores the `meterValue` array of a MeterValues message (one row per sampled value)
    /// and updates the charge point's last meter value time.
    void saveMeterValues(const QString &id, int connectorId, std::optional<qint64> transactionId,
                         const QJsonArray &meterValue);

    // ---- message log -------------------------------------------------------------------
    /// Appends a frame to the log. For results/errors with an empty @p action the action is
    /// looked up from the matching Call.
    void logMessage(const QString &id, bool incoming, const QString &type, const QString &action,
                    const QString &uniqueId, const QString &raw);

    // ---- configuration cache -----------------------------------------------------------
    /// Stores keys from a GetConfiguration answer. With @p replaceAll the previous cache of
    /// this charge point is dropped first (use when all keys were requested).
    void saveConfiguration(const QString &id, const QList<ConfigKeyRecord> &keys, bool replaceAll);
    /// Updates (or adds) one cached value, e.g. after an accepted ChangeConfiguration.
    void setConfigurationValue(const QString &id, const QString &key, const QString &value);
    QList<ConfigKeyRecord> configuration(const QString &id) const;

    // ---- queries -----------------------------------------------------------------------
    QList<ChargePointRecord> chargePoints() const;
    std::optional<ChargePointRecord> chargePoint(const QString &id) const;
    QList<ConnectorStatusRecord> connectorStatuses(const QString &id) const;
    /// The most recent transaction (running or finished) of a charge point.
    std::optional<TransactionRecord> lastTransaction(const QString &id) const;
    /// A transaction by the id we handed out in StartTransaction.
    std::optional<TransactionRecord> transaction(qint64 transactionId) const;
    /// Newest first.
    QList<MessageRecord> recentMessages(const QString &id, int limit = 100) const;
    /// Newest first.
    QList<MeterValueRecord> recentMeterValues(const QString &id, int limit = 50) const;

    /// UTC timestamp as stored in the database ("2026-11-10T09:00:00.000Z").
    static QString toDb(const QDateTime &t);
    static QDateTime fromDb(const QVariant &v);
    /// Parses an OCPP timestamp; falls back to "now" if missing or invalid.
    static QDateTime parseOcppTime(const QString &text);

private:
    bool migrate();
    /// Prepares and runs a statement. Logs and returns false on error. Result rows are in @p out.
    bool run(QSqlQuery &out, const QString &sql, const QVariantList &args = {}) const;
    bool run(const QString &sql, const QVariantList &args = {}) const;
    void setTime(const QString &id, const char *column);

    QSqlDatabase m_db;
    QString m_connectionName;
};
