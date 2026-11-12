#pragma once

#include <QAbstractTableModel>
#include <QSet>
#include <QTimer>

#include "core/db/Database.h"

class CentralSystem;

/**
 * Table model with one row per known charge point, for the GUI list (QTableView).
 *
 * Contains only QtCore code, so it can also be used without Widgets (e.g. by the REST API).
 * Data comes from the database; the model refreshes a row when CentralSystem reports a change
 * for that charge point. Refreshes are batched (about 100 ms) so a burst of messages does not
 * cause a query per message.
 *
 * Use the custom roles for programmatic access (colors, sorting, detail view); DisplayRole
 * gives ready-to-show text.
 */
class ChargePointModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column {
        IdColumn,
        ConnectionColumn,
        StatusColumn,
        ModelColumn,
        LastHeartbeatColumn,
        LastMeterValueColumn,
        LastConnectedColumn,
        LastDisconnectedColumn,
        ColumnCount
    };

    enum Role {
        IdRole = Qt::UserRole + 1,   ///< QString charge point id
        ConnectedRole,               ///< bool
        StatusRole,                  ///< QString, e.g. "Charging" (single connector) or "1: Charging, 2: Available"
        RecordRole                   ///< QVariant holding the ChargePointRecord (use record() instead)
    };

    explicit ChargePointModel(CentralSystem *centralSystem, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    /// Row index of a charge point id, or -1.
    int rowOf(const QString &chargePointId) const;
    /// Record of a row (must be valid).
    ChargePointRecord record(int row) const { return m_rows.at(row).record; }
    /// Latest status per connector of a row.
    QList<ConnectorStatusRecord> connectors(int row) const { return m_rows.at(row).connectors; }

    /// Reloads everything from the database.
    void reload();
    /// Applies pending refreshes immediately instead of waiting for the batch timer.
    void flush();

private:
    struct Row
    {
        ChargePointRecord record;
        QList<ConnectorStatusRecord> connectors;
    };

    void markDirty(const QString &chargePointId);
    void refresh(const QString &chargePointId);
    Row load(const ChargePointRecord &record) const;
    /// "Charging" for one connector, "1: Charging, 2: Available" for several, "-" if unknown.
    static QString statusText(const Row &row);
    static QString timeText(const QDateTime &t);

    CentralSystem *m_cs;
    QList<Row> m_rows;           ///< Sorted by charge point id
    QSet<QString> m_dirty;
    QTimer m_timer;
};
