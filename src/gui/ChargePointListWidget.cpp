#include "ChargePointListWidget.h"

#include <QApplication>
#include <QColor>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QTableView>
#include <QVBoxLayout>

#include "core/ChargePointModel.h"

namespace {

/// Sorts the model and adds colors (the core model has no GUI dependencies, so colors live here).
class ColoringProxy : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (role != Qt::ForegroundRole || !index.isValid())
            return QSortFilterProxyModel::data(index, role);

        const bool connected = QSortFilterProxyModel::data(index, ChargePointModel::ConnectedRole).toBool();
        const QColor muted = QApplication::palette().color(QPalette::Disabled, QPalette::Text);

        if (index.column() == ChargePointModel::ConnectionColumn)
            return connected ? QColor(46, 160, 67) : muted;
        if (!connected)
            return muted;
        if (index.column() == ChargePointModel::StatusColumn)
            return statusColor(QSortFilterProxyModel::data(index, ChargePointModel::StatusRole).toString());
        return {};
    }

private:
    /// Color for the most important status in the text (a multi-connector text mixes statuses).
    static QVariant statusColor(const QString &status)
    {
        if (status.contains("Faulted"))
            return QColor(215, 58, 73);
        if (status.contains("Unavailable"))
            return QColor(210, 140, 0);
        if (status.contains("Charging"))
            return QColor(30, 120, 220);
        if (status.contains("Available"))
            return QColor(46, 160, 67);
        return {};
    }
};

} // namespace

ChargePointListWidget::ChargePointListWidget(ChargePointModel *model, const QString &emptyHint, QWidget *parent)
    : QWidget(parent), m_model(model)
{
    m_proxy = new ColoringProxy(this);
    m_proxy->setSourceModel(model);

    m_table = new QTableView(this);
    m_table->setModel(m_proxy);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setSortingEnabled(true);
    m_table->sortByColumn(ChargePointModel::IdColumn, Qt::AscendingOrder);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setShowGrid(false);

    auto *hint = new QLabel(emptyHint, this);
    hint->setAlignment(Qt::AlignCenter);
    hint->setEnabled(false); // muted look

    m_pages = new QStackedWidget(this);
    m_pages->addWidget(hint);
    m_pages->addWidget(m_table);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_pages);

    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this] { emit chargePointSelected(selectedId()); });
    // Column widths are fitted when rows appear; later changes keep what the user dragged.
    connect(model, &QAbstractItemModel::rowsInserted, this, [this] {
        updatePage();
        m_table->resizeColumnsToContents();
    });
    connect(model, &QAbstractItemModel::modelReset, this, [this] {
        updatePage();
        m_table->resizeColumnsToContents();
    });
    updatePage();
    m_table->resizeColumnsToContents();
}

void ChargePointListWidget::updatePage()
{
    m_pages->setCurrentIndex(m_model->rowCount() == 0 ? 0 : 1);
}

QString ChargePointListWidget::selectedId() const
{
    const QModelIndex current = m_table->currentIndex();
    return current.isValid() ? current.data(ChargePointModel::IdRole).toString() : QString();
}

void ChargePointListWidget::selectId(const QString &chargePointId)
{
    for (int row = 0; row < m_proxy->rowCount(); ++row) {
        const QModelIndex idx = m_proxy->index(row, 0);
        if (idx.data(ChargePointModel::IdRole).toString() == chargePointId) {
            m_table->setCurrentIndex(idx);
            return;
        }
    }
}
