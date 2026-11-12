#pragma once

#include <QTimer>
#include <QWidget>

class CentralSystem;
class QLabel;
class QTableWidget;

/**
 * Transaction history table (StartTransaction .. StopTransaction), newest first, whole history.
 *
 * Used twice: as the "Transactions" tab of one charge point (showChargePointColumn = false,
 * call setChargePoint()), and as the top-level "Transactions" view of all chargers
 * (showChargePointColumn = true, never call setChargePoint()).
 *
 * Columns can be sorted by clicking the header. The table refreshes itself when CentralSystem
 * reports a change (batched to ~100 ms), keeping the chosen sort order.
 */
class TransactionsWidget : public QWidget
{
    Q_OBJECT
public:
    TransactionsWidget(CentralSystem *centralSystem, bool showChargePointColumn, QWidget *parent = nullptr);

    /// Per-charger mode only: the charge point to show; an empty id shows an empty table.
    void setChargePoint(const QString &chargePointId);

    /// Reloads from the database now (normally happens automatically).
    void refresh();

private:
    void scheduleRefresh(const QString &changedId);

    CentralSystem *m_cs;
    bool m_allChargers;
    QString m_id;
    QTimer m_refreshTimer;
    QTableWidget *m_table;
    QLabel *m_summary;
};
