#include "ChargePointModel.h"

#include "core/CentralSystem.h"

#include <utility>

ChargePointModel::ChargePointModel(CentralSystem *centralSystem, QObject *parent)
    : QAbstractTableModel(parent), m_cs(centralSystem)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(100);
    connect(&m_timer, &QTimer::timeout, this, &ChargePointModel::flush);
    connect(m_cs, &CentralSystem::chargePointChanged, this, &ChargePointModel::markDirty);
    reload();
}

int ChargePointModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

int ChargePointModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QString ChargePointModel::timeText(const QDateTime &t)
{
    return t.isValid() ? t.toLocalTime().toString("yyyy-MM-dd HH:mm:ss") : QStringLiteral("-");
}

QString ChargePointModel::statusText(const Row &row)
{
    // Connector 0 is the charge point as a whole; real connectors are 1..n.
    QStringList parts;
    for (const ConnectorStatusRecord &c : row.connectors) {
        if (c.connectorId > 0)
            parts << c.status;
    }
    if (parts.isEmpty()) {
        for (const ConnectorStatusRecord &c : row.connectors)
            return c.status; // only connector 0 known
        return QStringLiteral("-");
    }
    if (parts.size() == 1)
        return parts.first();

    parts.clear();
    for (const ConnectorStatusRecord &c : row.connectors) {
        if (c.connectorId > 0)
            parts << QString("%1: %2").arg(c.connectorId).arg(c.status);
    }
    return parts.join(", ");
}

QVariant ChargePointModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const Row &row = m_rows.at(index.row());
    const ChargePointRecord &r = row.record;

    switch (role) {
    case IdRole: return r.id;
    case ConnectedRole: return r.connected;
    case StatusRole: return statusText(row);
    case RecordRole: return QVariant::fromValue(r);
    case Qt::DisplayRole:
        switch (index.column()) {
        case IdColumn: return r.id;
        case ConnectionColumn: return r.connected ? "Connected" : "Disconnected";
        case StatusColumn: return statusText(row);
        case ModelColumn: return (r.vendor + " " + r.model).trimmed();
        case LastHeartbeatColumn: return timeText(r.lastHeartbeat);
        case LastMeterValueColumn: return timeText(r.lastMeterValue);
        case LastConnectedColumn: return timeText(r.lastConnected);
        case LastDisconnectedColumn: return timeText(r.lastDisconnected);
        }
        break;
    }
    return {};
}

QVariant ChargePointModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case IdColumn: return "Charge point";
    case ConnectionColumn: return "Connection";
    case StatusColumn: return "Status";
    case ModelColumn: return "Model";
    case LastHeartbeatColumn: return "Last heartbeat";
    case LastMeterValueColumn: return "Last meter value";
    case LastConnectedColumn: return "Last connected";
    case LastDisconnectedColumn: return "Last disconnected";
    }
    return {};
}

int ChargePointModel::rowOf(const QString &chargePointId) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).record.id == chargePointId)
            return i;
    }
    return -1;
}

ChargePointModel::Row ChargePointModel::load(const ChargePointRecord &record) const
{
    return Row{record, m_cs->database()->connectorStatuses(record.id)};
}

void ChargePointModel::reload()
{
    beginResetModel();
    m_rows.clear();
    for (const ChargePointRecord &r : m_cs->database()->chargePoints())
        m_rows.append(load(r));
    endResetModel();
}

void ChargePointModel::markDirty(const QString &chargePointId)
{
    m_dirty.insert(chargePointId);
    if (!m_timer.isActive())
        m_timer.start();
}

void ChargePointModel::flush()
{
    m_timer.stop();
    const QSet<QString> ids = std::exchange(m_dirty, {});
    for (const QString &id : ids)
        refresh(id);
}

/// Re-reads one charge point; inserts a new row (keeping id order) if it is not in the list yet.
void ChargePointModel::refresh(const QString &chargePointId)
{
    const auto rec = m_cs->database()->chargePoint(chargePointId);
    if (!rec)
        return;

    const int existing = rowOf(chargePointId);
    if (existing >= 0) {
        m_rows[existing] = load(*rec);
        emit dataChanged(index(existing, 0), index(existing, ColumnCount - 1));
        return;
    }

    int pos = 0;
    while (pos < m_rows.size() && m_rows.at(pos).record.id < chargePointId)
        ++pos;
    beginInsertRows({}, pos, pos);
    m_rows.insert(pos, load(*rec));
    endInsertRows();
}
