#pragma once

#include <QWidget>

class ChargePointModel;
class QSortFilterProxyModel;
class QStackedWidget;
class QTableView;

/**
 * The charger overview: a sortable table with one row per charge point, showing connection
 * state and status (from StatusNotification). Connection and status are colored.
 *
 * Shows a hint instead of an empty table while no charge point has been seen yet.
 * Emits chargePointSelected() when the user picks a row; the detail view listens to that.
 */
class ChargePointListWidget : public QWidget
{
    Q_OBJECT
public:
    /// @p emptyHint is shown while the list is empty (e.g. the URL chargers should connect to).
    ChargePointListWidget(ChargePointModel *model, const QString &emptyHint, QWidget *parent = nullptr);

    /// Id of the selected charge point, or an empty string.
    QString selectedId() const;
    void selectId(const QString &chargePointId);

signals:
    /// The selection changed. @p chargePointId is empty when nothing is selected.
    void chargePointSelected(const QString &chargePointId);

private:
    void updatePage();

    ChargePointModel *m_model;
    QSortFilterProxyModel *m_proxy;
    QTableView *m_table;
    QStackedWidget *m_pages;   ///< page 0: empty hint, page 1: table
};
