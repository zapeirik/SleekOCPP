#include "gui/TransactionsWidget.h"

#include <QHeaderView>
#include <QLabel>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

#include "core/CentralSystem.h"
#include "core/db/Database.h"

namespace {

enum Column { IdColumn, ChargePointColumn, ConnectorColumn, IdTagColumn, StartedColumn, StoppedColumn,
              DurationColumn, EnergyColumn, StatusColumn, ColumnCount };

QString timeText(const QDateTime &t)
{
    return t.isValid() ? t.toLocalTime().toString("yyyy-MM-dd HH:mm:ss") : QStringLiteral("-");
}

/// "1h 05m 09s" style duration.
QString durationText(qint64 seconds)
{
    if (seconds < 0)
        return QStringLiteral("-");
    return QString("%1h %2m %3s").arg(seconds / 3600).arg(seconds / 60 % 60, 2, 10, QChar('0')).arg(seconds % 60, 2, 10, QChar('0'));
}

/// A cell that shows @p text but sorts by @p sortKey (so numbers and times sort correctly).
QTableWidgetItem *cell(const QString &text, const QVariant &sortKey = {})
{
    auto *item = new QTableWidgetItem(text);
    if (sortKey.isValid())
        item->setData(Qt::UserRole, sortKey);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

/// Sorts by the UserRole value when present, otherwise by text.
class SortableItem : public QTableWidgetItem
{
public:
    using QTableWidgetItem::QTableWidgetItem;
    bool operator<(const QTableWidgetItem &other) const override
    {
        const QVariant a = data(Qt::UserRole), b = other.data(Qt::UserRole);
        if (a.isValid() && b.isValid())
            return a.toLongLong() < b.toLongLong();
        return QTableWidgetItem::operator<(other);
    }
};

QTableWidgetItem *sortable(const QString &text, qint64 key)
{
    auto *item = new SortableItem(text);
    item->setData(Qt::UserRole, key);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

} // namespace

TransactionsWidget::TransactionsWidget(CentralSystem *centralSystem, bool showChargePointColumn, QWidget *parent)
    : QWidget(parent), m_cs(centralSystem), m_allChargers(showChargePointColumn)
{
    auto *layout = new QVBoxLayout(this);
    m_table = new QTableWidget(0, ColumnCount, this);
    m_table->setObjectName("transactionsTable");
    m_table->setHorizontalHeaderLabels({"Id", "Charge point", "Connector", "idTag", "Started", "Stopped",
                                        "Duration", "Energy (Wh)", "Status"});
    m_table->setColumnHidden(ChargePointColumn, !showChargePointColumn);
    m_table->verticalHeader()->hide();
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSortingEnabled(true);
    m_table->sortByColumn(IdColumn, Qt::DescendingOrder);

    // Details of the selected transaction: summary text and its meter values.
    auto *details = new QWidget(this);
    auto *detailsLayout = new QVBoxLayout(details);
    detailsLayout->setContentsMargins(0, 0, 0, 0);
    m_detailSummary = new QLabel(details);
    m_detailSummary->setObjectName("transactionSummary");
    m_detailSummary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_detailSummary->setWordWrap(true);
    m_samples = new QTableWidget(0, 8, details);
    m_samples->setObjectName("meterValuesTable");
    m_samples->setHorizontalHeaderLabels({"Time", "Source", "Measurand", "Phase", "Value", "Unit", "Context", "Interval"});
    m_samples->verticalHeader()->hide();
    m_samples->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_samples->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_samples->horizontalHeader()->setStretchLastSection(true);
    detailsLayout->addWidget(m_detailSummary);
    detailsLayout->addWidget(m_samples, 1);

    auto *split = new QSplitter(Qt::Vertical, this);
    split->addWidget(m_table);
    split->addWidget(details);
    split->setSizes({300, 220});
    layout->addWidget(split, 1);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &TransactionsWidget::showDetails);
    showDetails();

    m_summary = new QLabel(this);
    layout->addWidget(m_summary);

    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(100);
    connect(&m_refreshTimer, &QTimer::timeout, this, &TransactionsWidget::refresh);
    connect(m_cs, &CentralSystem::chargePointChanged, this, &TransactionsWidget::scheduleRefresh);

    if (m_allChargers)
        refresh();
}

void TransactionsWidget::setChargePoint(const QString &chargePointId)
{
    m_id = chargePointId;
    refresh();
}

void TransactionsWidget::scheduleRefresh(const QString &changedId)
{
    if ((m_allChargers || changedId == m_id) && !m_refreshTimer.isActive())
        m_refreshTimer.start();
}

/// Rebuilds the table from the database. Sorting is switched off while filling so rows do not move
/// under the loop, then switched on again which re-applies the user's chosen sort order.
void TransactionsWidget::refresh()
{
    const qint64 selectedId = m_table->currentRow() >= 0 && m_table->item(m_table->currentRow(), IdColumn)
        ? m_table->item(m_table->currentRow(), IdColumn)->data(Qt::UserRole).toLongLong() : -1;
    QSignalBlocker block(m_table);
    m_table->setSortingEnabled(false);
    m_table->setRowCount(0);
    QList<TransactionRecord> list;
    if (m_allChargers || !m_id.isEmpty())
        list = m_cs->database()->transactions(m_allChargers ? QString() : m_id);

    m_table->setRowCount(list.size());
    const QDateTime now = QDateTime::currentDateTimeUtc();
    qint64 totalWh = 0;
    int active = 0;
    for (int row = 0; row < list.size(); ++row) {
        const TransactionRecord &t = list.at(row);
        const qint64 seconds = t.startTime.isValid() ? t.startTime.secsTo(t.isActive() ? now : t.stopTime) : -1;
        const auto energy = t.energyWh();
        totalWh += energy.value_or(0);
        active += t.isActive() ? 1 : 0;

        m_table->setItem(row, IdColumn, sortable(QString::number(t.id), t.id));
        m_table->setItem(row, ChargePointColumn, cell(t.chargePointId));
        m_table->setItem(row, ConnectorColumn, sortable(QString::number(t.connectorId), t.connectorId));
        m_table->setItem(row, IdTagColumn, cell(t.idTag));
        m_table->setItem(row, StartedColumn, sortable(timeText(t.startTime), t.startTime.toSecsSinceEpoch()));
        m_table->setItem(row, StoppedColumn, sortable(timeText(t.stopTime), t.stopTime.isValid() ? t.stopTime.toSecsSinceEpoch() : INT64_MAX));
        m_table->setItem(row, DurationColumn, sortable(durationText(seconds), seconds));
        m_table->setItem(row, EnergyColumn, sortable(energy ? QString::number(*energy) : QStringLiteral("-"), energy.value_or(-1)));
        m_table->setItem(row, StatusColumn, cell(t.isActive() ? QStringLiteral("Running")
                                                 : (t.stopReason.isEmpty() ? QStringLiteral("Stopped") : "Stopped (" + t.stopReason + ")")));
    }
    m_table->setSortingEnabled(true);
    for (int row = 0; selectedId >= 0 && row < m_table->rowCount(); ++row)
        if (m_table->item(row, IdColumn)->data(Qt::UserRole).toLongLong() == selectedId)
            m_table->selectRow(row);
    block.unblock();
    showDetails();
    for (int c = 0; c < ColumnCount - 1; ++c)
        m_table->resizeColumnToContents(c);

    m_summary->setText(list.isEmpty() ? QStringLiteral("No transactions.")
                                      : QString("%1 transactions (%2 running), %3 Wh delivered in finished ones")
                                            .arg(list.size()).arg(active).arg(totalWh));
}

/// Fills the details area for the selected transaction: summary and all its reported meter values.
void TransactionsWidget::showDetails()
{
    m_samples->setRowCount(0);
    const int row = m_table->currentRow();
    if (row < 0 || !m_table->item(row, IdColumn)) {
        m_detailSummary->setText("Select a transaction to see its meter values.");
        return;
    }
    Database *db = m_cs->database();
    const auto tx = db->transaction(m_table->item(row, IdColumn)->data(Qt::UserRole).toLongLong());
    if (!tx)
        return;
    const auto samples = db->transactionMeterValues(tx->id);

    qint64 minInterval = -1, maxInterval = -1, sumInterval = 0;
    int intervals = 0;
    for (const MeterValueRecord &v : samples) {
        if (!v.intervalSecs || *v.intervalSecs <= 0)
            continue;
        minInterval = intervals == 0 ? *v.intervalSecs : qMin(minInterval, *v.intervalSecs);
        maxInterval = qMax(maxInterval, *v.intervalSecs);
        sumInterval += *v.intervalSecs;
        ++intervals;
    }

    QString text = QString("Transaction #%1 on %2, connector %3, idTag %4\nMeter start %5 Wh, meter stop %6, stop reason %7\n")
                       .arg(tx->id).arg(tx->chargePointId).arg(tx->connectorId).arg(tx->idTag).arg(tx->meterStart)
                       .arg(tx->meterStop ? QString::number(*tx->meterStop) + " Wh" : QStringLiteral("-"),
                            tx->stopReason.isEmpty() ? QStringLiteral("-") : tx->stopReason);
    text += QString("%1 reported value(s)").arg(samples.size());
    if (intervals > 0)
        text += QString("; interval between samples: min %1 s, average %2 s, max %3 s")
                    .arg(minInterval).arg(sumInterval / intervals).arg(maxInterval);
    m_detailSummary->setText(text);

    m_samples->setRowCount(samples.size());
    for (int i = 0; i < samples.size(); ++i) {
        const MeterValueRecord &v = samples.at(i);
        const QStringList cells = {timeText(v.timestamp), v.source.isEmpty() ? QStringLiteral("-") : v.source,
                                   v.measurand, v.phase.isEmpty() ? QStringLiteral("-") : v.phase, v.value,
                                   v.unit, v.context,
                                   v.intervalSecs ? durationText(*v.intervalSecs) : QStringLiteral("-")};
        for (int c = 0; c < cells.size(); ++c)
            m_samples->setItem(i, c, new QTableWidgetItem(cells.at(c)));
    }
    for (int c = 0; c < m_samples->columnCount() - 1; ++c)
        m_samples->resizeColumnToContents(c);
}
