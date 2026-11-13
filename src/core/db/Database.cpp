#include "Database.h"

#include <QDebug>
#include <QHash>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace {

constexpr int kSchemaVersion = 3;

QString now() { return Database::toDb(QDateTime::currentDateTimeUtc()); }

ChargePointRecord readChargePoint(const QSqlQuery &q)
{
    ChargePointRecord r;
    r.id = q.value("id").toString();
    r.vendor = q.value("vendor").toString();
    r.model = q.value("model").toString();
    r.serial = q.value("serial").toString();
    r.firmware = q.value("firmware").toString();
    r.connected = q.value("connected").toBool();
    r.remoteAddress = q.value("remote_address").toString();
    r.firstSeen = Database::fromDb(q.value("first_seen"));
    r.lastConnected = Database::fromDb(q.value("last_connected"));
    r.lastDisconnected = Database::fromDb(q.value("last_disconnected"));
    r.lastHeartbeat = Database::fromDb(q.value("last_heartbeat"));
    r.lastMeterValue = Database::fromDb(q.value("last_meter_value"));
    return r;
}

MeterValueRecord readMeterValue(const QSqlQuery &q)
{
    MeterValueRecord r;
    r.id = q.value("id").toLongLong();
    r.chargePointId = q.value("charge_point_id").toString();
    r.connectorId = q.value("connector_id").toInt();
    if (!q.value("transaction_id").isNull())
        r.transactionId = q.value("transaction_id").toLongLong();
    r.timestamp = Database::fromDb(q.value("timestamp"));
    r.measurand = q.value("measurand").toString();
    r.value = q.value("value").toString();
    r.unit = q.value("unit").toString();
    r.context = q.value("context").toString();
    r.phase = q.value("phase").toString();
    r.location = q.value("location").toString();
    r.source = q.value("source").toString();
    return r;
}

} // namespace

Database::Database()
    : m_connectionName(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
}

Database::~Database()
{
    if (m_db.isOpen())
        m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool Database::open(const QString &path, QString *error)
{
    if (m_db.isValid()) { // reopening: drop the previous connection first
        m_db.close();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connectionName);
    }
    m_db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    m_db.setDatabaseName(path);
    if (!m_db.open()) {
        if (error)
            *error = m_db.lastError().text();
        return false;
    }
    run("PRAGMA journal_mode=WAL");
    run("PRAGMA foreign_keys=ON");
    if (!migrate()) {
        if (error)
            *error = "Schema migration failed";
        return false;
    }
    return true;
}

/// Brings the schema up to kSchemaVersion. Add a new `if (version < N)` block to change the schema.
bool Database::migrate()
{
    QSqlQuery q(m_db);
    if (!run(q, "PRAGMA user_version") || !q.next())
        return false;
    int version = q.value(0).toInt();

    if (version < 1) {
        const QStringList steps = {
            R"(CREATE TABLE charge_points (
                 id TEXT PRIMARY KEY,
                 vendor TEXT, model TEXT, serial TEXT, firmware TEXT,
                 connected INTEGER NOT NULL DEFAULT 0,
                 remote_address TEXT,
                 first_seen TEXT, last_connected TEXT, last_disconnected TEXT,
                 last_heartbeat TEXT, last_meter_value TEXT))",
            R"(CREATE TABLE connector_status (
                 charge_point_id TEXT NOT NULL, connector_id INTEGER NOT NULL,
                 status TEXT, error_code TEXT, info TEXT, updated_at TEXT,
                 PRIMARY KEY (charge_point_id, connector_id)))",
            R"(CREATE TABLE status_history (
                 id INTEGER PRIMARY KEY AUTOINCREMENT,
                 charge_point_id TEXT NOT NULL, connector_id INTEGER NOT NULL,
                 status TEXT, error_code TEXT, info TEXT, timestamp TEXT))",
            "CREATE INDEX idx_status_history ON status_history(charge_point_id, id)",
            R"(CREATE TABLE transactions (
                 id INTEGER PRIMARY KEY AUTOINCREMENT,
                 charge_point_id TEXT NOT NULL, connector_id INTEGER NOT NULL,
                 id_tag TEXT, meter_start INTEGER, start_time TEXT,
                 meter_stop INTEGER, stop_time TEXT, stop_reason TEXT))",
            "CREATE INDEX idx_transactions_cp ON transactions(charge_point_id, id)",
            R"(CREATE TABLE meter_values (
                 id INTEGER PRIMARY KEY AUTOINCREMENT,
                 charge_point_id TEXT NOT NULL, connector_id INTEGER NOT NULL,
                 transaction_id INTEGER, timestamp TEXT,
                 measurand TEXT, value TEXT, unit TEXT, context TEXT, phase TEXT, location TEXT))",
            "CREATE INDEX idx_meter_values_cp ON meter_values(charge_point_id, id)",
            R"(CREATE TABLE messages (
                 id INTEGER PRIMARY KEY AUTOINCREMENT,
                 charge_point_id TEXT NOT NULL, timestamp TEXT NOT NULL,
                 incoming INTEGER NOT NULL, type TEXT, action TEXT, unique_id TEXT, raw TEXT))",
            "CREATE INDEX idx_messages_cp ON messages(charge_point_id, id)",
            "CREATE INDEX idx_messages_uid ON messages(charge_point_id, unique_id)",
            R"(CREATE TABLE config_cache (
                 charge_point_id TEXT NOT NULL, key TEXT NOT NULL,
                 value TEXT, readonly INTEGER, updated_at TEXT,
                 PRIMARY KEY (charge_point_id, key)))",
        };
        m_db.transaction();
        for (const QString &s : steps) {
            if (!run(s)) {
                m_db.rollback();
                return false;
            }
        }
        run(QString("PRAGMA user_version=%1").arg(kSchemaVersion));
        m_db.commit();
        version = 1;
    }
    if (version < 2) {
        // Two-step pruning: messages are first marked deleted, later removed for good.
        m_db.transaction();
        if (!run("ALTER TABLE messages ADD COLUMN deleted INTEGER NOT NULL DEFAULT 0")) {
            m_db.rollback();
            return false;
        }
        run(QString("PRAGMA user_version=%1").arg(kSchemaVersion));
        m_db.commit();
        version = 2;
    }
    if (version < 3) {
        // Meter values remember whether they came from MeterValues or StopTransaction; index for per-transaction lookup.
        m_db.transaction();
        if (!run("ALTER TABLE meter_values ADD COLUMN source TEXT NOT NULL DEFAULT ''")
            || !run("CREATE INDEX idx_meter_values_tx ON meter_values(transaction_id)")) {
            m_db.rollback();
            return false;
        }
        run(QString("PRAGMA user_version=%1").arg(kSchemaVersion));
        m_db.commit();
        version = 3;
    }
    return true;
}

bool Database::run(QSqlQuery &out, const QString &sql, const QVariantList &args) const
{
    if (!out.prepare(sql)) {
        qWarning().noquote() << "SQL prepare failed:" << out.lastError().text() << "|" << sql;
        return false;
    }
    for (const QVariant &a : args)
        out.addBindValue(a);
    if (!out.exec()) {
        qWarning().noquote() << "SQL failed:" << out.lastError().text() << "|" << sql;
        return false;
    }
    return true;
}

bool Database::run(const QString &sql, const QVariantList &args) const
{
    QSqlQuery q(m_db);
    return run(q, sql, args);
}

QString Database::toDb(const QDateTime &t)
{
    return t.toUTC().toString("yyyy-MM-ddTHH:mm:ss.zzzZ");
}

QDateTime Database::fromDb(const QVariant &v)
{
    if (v.isNull())
        return QDateTime();
    QDateTime t = QDateTime::fromString(v.toString(), Qt::ISODateWithMs);
    return t.toUTC();
}

QDateTime Database::parseOcppTime(const QString &text)
{
    QDateTime t = QDateTime::fromString(text, Qt::ISODateWithMs);
    return t.isValid() ? t.toUTC() : QDateTime::currentDateTimeUtc();
}

// ---- charge point lifecycle ------------------------------------------------------------

void Database::chargePointConnected(const QString &id, const QString &remoteAddress)
{
    const QString t = now();
    run(R"(INSERT INTO charge_points (id, connected, remote_address, first_seen, last_connected)
           VALUES (?, 1, ?, ?, ?)
           ON CONFLICT(id) DO UPDATE SET connected=1, remote_address=excluded.remote_address,
                                         last_connected=excluded.last_connected)",
        {id, remoteAddress, t, t});
}

void Database::chargePointDisconnected(const QString &id)
{
    run("UPDATE charge_points SET connected=0, last_disconnected=? WHERE id=?", {now(), id});
}

void Database::markAllDisconnected()
{
    run("UPDATE charge_points SET connected=0 WHERE connected=1");
}

void Database::updateBootInfo(const QString &id, const QString &vendor, const QString &model,
                              const QString &serial, const QString &firmware)
{
    run("UPDATE charge_points SET vendor=?, model=?, serial=?, firmware=? WHERE id=?",
        {vendor, model, serial, firmware, id});
}

/// Sets one of the fixed "last ..." timestamp columns of a charge point to now. @p column is never user input.
void Database::setTime(const QString &id, const char *column)
{
    run(QString("UPDATE charge_points SET %1=? WHERE id=?").arg(column), {now(), id});
}

void Database::touchHeartbeat(const QString &id) { setTime(id, "last_heartbeat"); }

// ---- status, transactions, meter values ------------------------------------------------

void Database::saveConnectorStatus(const QString &id, int connectorId, const QString &status,
                                   const QString &errorCode, const QString &info, const QDateTime &timestamp)
{
    const QString t = toDb(timestamp);
    run(R"(INSERT INTO connector_status (charge_point_id, connector_id, status, error_code, info, updated_at)
           VALUES (?, ?, ?, ?, ?, ?)
           ON CONFLICT(charge_point_id, connector_id) DO UPDATE SET status=excluded.status,
             error_code=excluded.error_code, info=excluded.info, updated_at=excluded.updated_at)",
        {id, connectorId, status, errorCode, info, t});
    run(R"(INSERT INTO status_history (charge_point_id, connector_id, status, error_code, info, timestamp)
           VALUES (?, ?, ?, ?, ?, ?))",
        {id, connectorId, status, errorCode, info, t});
}

qint64 Database::startTransaction(const QString &id, int connectorId, const QString &idTag,
                                  qint64 meterStart, const QDateTime &timestamp)
{
    QSqlQuery q(m_db);
    if (!run(q, R"(INSERT INTO transactions (charge_point_id, connector_id, id_tag, meter_start, start_time)
                   VALUES (?, ?, ?, ?, ?))",
             {id, connectorId, idTag, meterStart, toDb(timestamp)}))
        return 0;
    return q.lastInsertId().toLongLong();
}

bool Database::stopTransaction(qint64 transactionId, qint64 meterStop, const QDateTime &timestamp,
                               const QString &reason)
{
    QSqlQuery q(m_db);
    if (!run(q, "UPDATE transactions SET meter_stop=?, stop_time=?, stop_reason=? WHERE id=?",
             {meterStop, toDb(timestamp), reason, transactionId}))
        return false;
    return q.numRowsAffected() > 0;
}

void Database::saveMeterValues(const QString &id, int connectorId, std::optional<qint64> transactionId,
                               const QJsonArray &meterValue, const QString &source)
{
    m_db.transaction();
    for (const QJsonValue &mv : meterValue) {
        const QJsonObject entry = mv.toObject();
        const QString ts = toDb(parseOcppTime(entry["timestamp"].toString()));
        for (const QJsonValue &sv : entry["sampledValue"].toArray()) {
            const QJsonObject s = sv.toObject();
            // OCPP default measurand when omitted is the active import energy register.
            const QString measurand = s["measurand"].toString("Energy.Active.Import.Register");
            run(R"(INSERT INTO meter_values (charge_point_id, connector_id, transaction_id, timestamp,
                                             measurand, value, unit, context, phase, location, source)
                   VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?))",
                {id, connectorId, transactionId ? QVariant(*transactionId) : QVariant(QMetaType::fromType<qint64>()),
                 ts, measurand, s["value"].toString(), s["unit"].toString(),
                 s["context"].toString(), s["phase"].toString(), s["location"].toString(), source});
        }
    }
    m_db.commit();
    setTime(id, "last_meter_value");
}

// ---- message log -----------------------------------------------------------------------

void Database::logMessage(const QString &id, bool incoming, const QString &type, const QString &action,
                          const QString &uniqueId, const QString &raw)
{
    QString act = action;
    if (act.isEmpty() && type != QLatin1String("Call")) {
        QSqlQuery q(m_db);
        if (run(q, "SELECT action FROM messages WHERE charge_point_id=? AND unique_id=? AND type='Call' "
                   "ORDER BY id DESC LIMIT 1", {id, uniqueId}) && q.next())
            act = q.value(0).toString();
    }
    run("INSERT INTO messages (charge_point_id, timestamp, incoming, type, action, unique_id, raw) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)",
        {id, now(), incoming ? 1 : 0, type, act, uniqueId, raw});
}

// ---- configuration cache ---------------------------------------------------------------

void Database::saveConfiguration(const QString &id, const QList<ConfigKeyRecord> &keys, bool replaceAll)
{
    m_db.transaction();
    if (replaceAll)
        run("DELETE FROM config_cache WHERE charge_point_id=?", {id});
    const QString t = now();
    for (const ConfigKeyRecord &k : keys) {
        run(R"(INSERT INTO config_cache (charge_point_id, key, value, readonly, updated_at)
               VALUES (?, ?, ?, ?, ?)
               ON CONFLICT(charge_point_id, key) DO UPDATE SET value=excluded.value,
                 readonly=excluded.readonly, updated_at=excluded.updated_at)",
            {id, k.key, k.value, k.readonly ? 1 : 0, t});
    }
    m_db.commit();
}

void Database::setConfigurationValue(const QString &id, const QString &key, const QString &value)
{
    run(R"(INSERT INTO config_cache (charge_point_id, key, value, readonly, updated_at)
           VALUES (?, ?, ?, 0, ?)
           ON CONFLICT(charge_point_id, key) DO UPDATE SET value=excluded.value,
             updated_at=excluded.updated_at)",
        {id, key, value, now()});
}

QList<ConfigKeyRecord> Database::configuration(const QString &id) const
{
    QList<ConfigKeyRecord> list;
    QSqlQuery q(m_db);
    if (run(q, "SELECT key, value, readonly, updated_at FROM config_cache WHERE charge_point_id=? ORDER BY key", {id})) {
        while (q.next()) {
            ConfigKeyRecord r;
            r.key = q.value(0).toString();
            r.value = q.value(1).toString();
            r.readonly = q.value(2).toBool();
            r.updatedAt = fromDb(q.value(3));
            list.append(r);
        }
    }
    return list;
}

// ---- queries ---------------------------------------------------------------------------

QList<ChargePointRecord> Database::chargePoints() const
{
    QList<ChargePointRecord> list;
    QSqlQuery q(m_db);
    if (run(q, "SELECT * FROM charge_points ORDER BY id")) {
        while (q.next())
            list.append(readChargePoint(q));
    }
    return list;
}

std::optional<ChargePointRecord> Database::chargePoint(const QString &id) const
{
    QSqlQuery q(m_db);
    if (run(q, "SELECT * FROM charge_points WHERE id=?", {id}) && q.next())
        return readChargePoint(q);
    return std::nullopt;
}

QList<ConnectorStatusRecord> Database::connectorStatuses(const QString &id) const
{
    QList<ConnectorStatusRecord> list;
    QSqlQuery q(m_db);
    if (run(q, "SELECT * FROM connector_status WHERE charge_point_id=? ORDER BY connector_id", {id})) {
        while (q.next()) {
            ConnectorStatusRecord r;
            r.connectorId = q.value("connector_id").toInt();
            r.status = q.value("status").toString();
            r.errorCode = q.value("error_code").toString();
            r.info = q.value("info").toString();
            r.updatedAt = fromDb(q.value("updated_at"));
            list.append(r);
        }
    }
    return list;
}

namespace {
/// Reads the current row of a `SELECT * FROM transactions` query.
TransactionRecord transactionFromRow(const QSqlQuery &q)
{
    TransactionRecord r;
    r.id = q.value("id").toLongLong();
    r.chargePointId = q.value("charge_point_id").toString();
    r.connectorId = q.value("connector_id").toInt();
    r.idTag = q.value("id_tag").toString();
    r.meterStart = q.value("meter_start").toLongLong();
    r.startTime = Database::fromDb(q.value("start_time"));
    if (!q.value("meter_stop").isNull())
        r.meterStop = q.value("meter_stop").toLongLong();
    r.stopTime = Database::fromDb(q.value("stop_time"));
    r.stopReason = q.value("stop_reason").toString();
    return r;
}
} // namespace

std::optional<TransactionRecord> Database::lastTransaction(const QString &id) const
{
    QSqlQuery q(m_db);
    if (!run(q, "SELECT * FROM transactions WHERE charge_point_id=? ORDER BY id DESC LIMIT 1", {id})
        || !q.next())
        return std::nullopt;
    return transactionFromRow(q);
}

std::optional<TransactionRecord> Database::transaction(qint64 transactionId) const
{
    QSqlQuery q(m_db);
    if (!run(q, "SELECT * FROM transactions WHERE id=?", {transactionId}) || !q.next())
        return std::nullopt;
    return transactionFromRow(q);
}

QList<TransactionRecord> Database::transactions(const QString &chargePointId, int limit) const
{
    QList<TransactionRecord> list;
    QSqlQuery q(m_db);
    QString sql = "SELECT * FROM transactions";
    QVariantList args;
    if (!chargePointId.isEmpty()) {
        sql += " WHERE charge_point_id=?";
        args << chargePointId;
    }
    sql += " ORDER BY id DESC";
    if (limit > 0) {
        sql += " LIMIT ?";
        args << limit;
    }
    if (run(q, sql, args))
        while (q.next())
            list.append(transactionFromRow(q));
    return list;
}

namespace {

/// WHERE clause (without the "WHERE") and arguments for the not-yet-deleted messages matching @p f.
QString pruneWhere(const MessageFilter &f, QVariantList &args)
{
    QStringList cond = {"deleted=0"};
    if (!f.chargePointId.isEmpty()) {
        cond << "charge_point_id=?";
        args << f.chargePointId;
    }
    if (!f.actions.isEmpty()) {
        cond << "action IN (" + QStringList(f.actions.size(), "?").join(",") + ")";
        for (const QString &a : f.actions)
            args << a;
    }
    if (!f.types.isEmpty()) {
        cond << "type IN (" + QStringList(f.types.size(), "?").join(",") + ")";
        for (const QString &t : f.types)
            args << t;
    }
    if (f.before.isValid()) {
        cond << "timestamp < ?";
        args << Database::toDb(f.before);
    }
    return cond.join(" AND ");
}

} // namespace

int Database::countMessages(const MessageFilter &filter) const
{
    QVariantList args;
    QSqlQuery q(m_db);
    if (run(q, "SELECT COUNT(*) FROM messages WHERE " + pruneWhere(filter, args), args) && q.next())
        return q.value(0).toInt();
    return 0;
}

int Database::softDeleteMessages(const MessageFilter &filter)
{
    QVariantList args;
    QSqlQuery q(m_db);
    if (!run(q, "UPDATE messages SET deleted=1 WHERE " + pruneWhere(filter, args), args))
        return 0;
    return q.numRowsAffected();
}

int Database::countDeletedMessages(const QString &chargePointId) const
{
    QSqlQuery q(m_db);
    const bool all = chargePointId.isEmpty();
    if (run(q, QString("SELECT COUNT(*) FROM messages WHERE deleted=1") + (all ? "" : " AND charge_point_id=?"),
            all ? QVariantList() : QVariantList{chargePointId}) && q.next())
        return q.value(0).toInt();
    return 0;
}

int Database::purgeDeletedMessages(const QString &chargePointId)
{
    QSqlQuery q(m_db);
    const bool all = chargePointId.isEmpty();
    if (!run(q, QString("DELETE FROM messages WHERE deleted=1") + (all ? "" : " AND charge_point_id=?"),
             all ? QVariantList() : QVariantList{chargePointId}))
        return 0;
    return q.numRowsAffected();
}

QStringList Database::messageActions(const QString &id) const
{
    QStringList actions;
    QSqlQuery q(m_db);
    if (run(q, "SELECT DISTINCT action FROM messages WHERE charge_point_id=? AND deleted=0 AND action<>'' ORDER BY action", {id}))
        while (q.next())
            actions << q.value(0).toString();
    return actions;
}

QList<MessageRecord> Database::recentMessages(const QString &id, int limit, const QStringList &hiddenActions) const
{
    QList<MessageRecord> list;
    QSqlQuery q(m_db);
    QString sql = "SELECT * FROM messages WHERE charge_point_id=? AND deleted=0";
    QVariantList args{id};
    if (!hiddenActions.isEmpty()) {
        sql += " AND COALESCE(action,'') NOT IN (" + QStringList(hiddenActions.size(), "?").join(",") + ")";
        for (const QString &a : hiddenActions)
            args << a;
    }
    sql += " ORDER BY id DESC LIMIT ?";
    args << limit;
    if (run(q, sql, args)) {
        while (q.next()) {
            MessageRecord r;
            r.id = q.value("id").toLongLong();
            r.chargePointId = id;
            r.timestamp = fromDb(q.value("timestamp"));
            r.incoming = q.value("incoming").toBool();
            r.type = q.value("type").toString();
            r.action = q.value("action").toString();
            r.uniqueId = q.value("unique_id").toString();
            r.raw = q.value("raw").toString();
            list.append(r);
        }
    }
    return list;
}

QList<MeterValueRecord> Database::transactionMeterValues(qint64 transactionId) const
{
    QList<MeterValueRecord> list;
    QSqlQuery q(m_db);
    if (!run(q, "SELECT * FROM meter_values WHERE transaction_id=? ORDER BY timestamp, id", {transactionId}))
        return list;
    QHash<QString, QDateTime> previous; // last timestamp per measurand/phase/location/source
    while (q.next()) {
        MeterValueRecord r = readMeterValue(q);
        const QString key = r.measurand + '|' + r.phase + '|' + r.location + '|' + r.source;
        if (previous.contains(key) && r.timestamp.isValid() && previous[key].isValid())
            r.intervalSecs = previous[key].secsTo(r.timestamp);
        previous[key] = r.timestamp;
        list.append(r);
    }
    return list;
}

QList<MeterValueRecord> Database::recentMeterValues(const QString &id, int limit) const
{
    QList<MeterValueRecord> list;
    QSqlQuery q(m_db);
    if (run(q, "SELECT * FROM meter_values WHERE charge_point_id=? ORDER BY id DESC LIMIT ?", {id, limit})) {
        while (q.next())
            list.append(readMeterValue(q));
    }
    return list;
}
