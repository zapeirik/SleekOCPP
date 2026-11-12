#pragma once

#include <QTimer>
#include <QWidget>

class CentralSystem;
class QFormLayout;
class QLabel;
class QPlainTextEdit;
class QTableWidget;
class QTabWidget;
class QToolButton;

/**
 * Detail view of one charge point, shown next to the list.
 *
 * Header: identity, connection and the key timestamps (heartbeat, meter value, connect,
 * disconnect), per-connector status and the last transaction.
 * Tabs: "Messages" (the log of OCPP frames, newest first, with the selected frame shown as
 * formatted JSON). Further tabs (configuration, control) are added with addTab().
 *
 * Call setChargePoint() to switch charger. The view refreshes itself when CentralSystem
 * reports a change for the shown charger; refreshes are batched to ~100 ms.
 */
class ChargePointDetailWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ChargePointDetailWidget(CentralSystem *centralSystem, QWidget *parent = nullptr);

    /// Shows this charge point; an empty id shows the "nothing selected" hint.
    void setChargePoint(const QString &chargePointId);
    QString chargePointId() const { return m_id; }

    /// Adds a tab for other views (configuration, control). The tab gets the current charger via
    /// the widget's own setChargePoint(); this class does not manage it.
    void addTab(QWidget *page, const QString &title);

    /// Reloads from the database now (normally happens automatically).
    void refresh();

private:
    void scheduleRefresh(const QString &changedId);
    void showSelectedMessage();
    void rebuildFilterMenu();
    void updateFilterButton();
    QStringList allActions() const;
    void promptDeleteMessages();
    void promptPurgeMessages();

    CentralSystem *m_cs;
    QString m_id;
    QTimer m_refreshTimer;

    QWidget *m_content;          ///< hidden while nothing is selected
    QLabel *m_hint;
    QLabel *m_title;
    QFormLayout *m_form;
    QLabel *m_identity, *m_connection, *m_heartbeat, *m_meterValue, *m_connected, *m_disconnected;
    QLabel *m_connectors, *m_transaction;
    QTabWidget *m_tabs;
    QTableWidget *m_messages;
    QPlainTextEdit *m_messageText;
    QToolButton *m_filterButton;  ///< menu of message actions to show/hide
    QStringList m_hiddenActions;  ///< actions hidden in the Messages tab (kept when switching charger)
};
