#include "gui/TransactionsWidget.h"

#include <QHeaderView>
#include <QLabel>
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
    layout->addWidget(m_table, 1);

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
    for (int c = 0; c < ColumnCount - 1; ++c)
        m_table->resizeColumnToContents(c);

    m_summary->setText(list.isEmpty() ? QStringLiteral("No transactions.")
                                      : QString("%1 transactions (%2 running), %3 Wh delivered in finished ones")
                                            .arg(list.size()).arg(active).arg(totalWh));
}
