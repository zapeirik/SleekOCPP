#include "gui/ChargePointDetailWidget.h"

#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QMenu>
#include <QMessageBox>
#include <QToolButton>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include "core/CentralSystem.h"
#include "core/db/Database.h"
#include "gui/MessagePruneDialog.h"

namespace {

const int kMessageLimit = 200;

/// All OCPP 1.6 actions, so the filter can be set up before a charger has sent them.
const QStringList kKnownActions = {
    "Authorize", "BootNotification", "CancelReservation", "ChangeAvailability", "ChangeConfiguration",
    "ClearCache", "ClearChargingProfile", "DataTransfer", "DiagnosticsStatusNotification",
    "FirmwareStatusNotification", "GetCompositeSchedule", "GetConfiguration", "GetDiagnostics",
    "GetLocalListVersion", "Heartbeat", "MeterValues", "RemoteStartTransaction", "RemoteStopTransaction",
    "ReserveNow", "Reset", "SendLocalList", "SetChargingProfile", "StartTransaction", "StatusNotification",
    "StopTransaction", "TriggerMessage", "UnlockConnector", "UpdateFirmware"};

/// Local time text, or "-" for a time that never happened.
QString timeText(const QDateTime &t)
{
    return t.isValid() ? t.toLocalTime().toString("yyyy-MM-dd HH:mm:ss") : QStringLiteral("-");
}

QLabel *valueLabel()
{
    auto *l = new QLabel("-");
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return l;
}

/// Pretty-prints a raw OCPP frame; falls back to the raw text if it is not valid JSON.
QString prettyJson(const QString &raw)
{
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
    return doc.isNull() ? raw : QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
}

} // namespace

ChargePointDetailWidget::ChargePointDetailWidget(CentralSystem *centralSystem, QWidget *parent)
    : QWidget(parent), m_cs(centralSystem)
{
    auto *outer = new QVBoxLayout(this);
    m_hint = new QLabel("Select a charge point to see details.", this);
    m_hint->setAlignment(Qt::AlignCenter);
    m_hint->setStyleSheet("color: gray;");
    outer->addWidget(m_hint);

    m_content = new QWidget(this);
    outer->addWidget(m_content);
    auto *layout = new QVBoxLayout(m_content);
    layout->setContentsMargins(0, 0, 0, 0);

    m_title = new QLabel(m_content);
    QFont f = m_title->font();
    f.setPointSizeF(f.pointSizeF() * 1.4);
    f.setBold(true);
    m_title->setFont(f);
    layout->addWidget(m_title);

    m_form = new QFormLayout;
    m_identity = valueLabel();
    m_connection = valueLabel();
    m_heartbeat = valueLabel();
    m_meterValue = valueLabel();
    m_connected = valueLabel();
    m_disconnected = valueLabel();
    m_connectors = valueLabel();
    m_transaction = valueLabel();
    m_form->addRow("Charger", m_identity);
    m_form->addRow("Connection", m_connection);
    m_form->addRow("Connectors", m_connectors);
    m_form->addRow("Last transaction", m_transaction);
    m_form->addRow("Last heartbeat", m_heartbeat);
    m_form->addRow("Last meter value", m_meterValue);
    m_form->addRow("Last connected", m_connected);
    m_form->addRow("Last disconnected", m_disconnected);
    layout->addLayout(m_form);

    // Messages tab: frame list on top, selected frame as JSON below.
    m_messages = new QTableWidget(0, 4, this);
    m_messages->setObjectName("messagesTable");
    m_messages->setHorizontalHeaderLabels({"Time", "Dir", "Type", "Action"});
    m_messages->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_messages->setSelectionMode(QAbstractItemView::SingleSelection);
    m_messages->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_messages->verticalHeader()->hide();
    m_messages->horizontalHeader()->setStretchLastSection(true);
    m_messageText = new QPlainTextEdit(this);
    m_messageText->setObjectName("messageText");
    m_messageText->setReadOnly(true);
    { QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont); mono.setPointSizeF(QGuiApplication::font().pointSizeF()); m_messageText->setFont(mono); }
    auto *split = new QSplitter(Qt::Vertical, this);
    auto *messagesPage = new QWidget(this);
    auto *messagesLayout = new QVBoxLayout(messagesPage);
    messagesLayout->setContentsMargins(0, 0, 0, 0);
    auto *filterRow = new QHBoxLayout;
    m_filterButton = new QToolButton(messagesPage);
    m_filterButton->setObjectName("messageFilterButton");
    m_filterButton->setPopupMode(QToolButton::InstantPopup);
    m_filterButton->setMenu(new QMenu(m_filterButton));
    connect(m_filterButton->menu(), &QMenu::aboutToShow, this, &ChargePointDetailWidget::rebuildFilterMenu);
    filterRow->addWidget(m_filterButton);
    auto *pruneButton = new QToolButton(messagesPage);
    pruneButton->setObjectName("messagePruneButton");
    pruneButton->setText("Prune   ");
    pruneButton->setPopupMode(QToolButton::InstantPopup);
    auto *pruneMenu = new QMenu(pruneButton);
    connect(pruneMenu->addAction("Delete messages..."), &QAction::triggered, this, &ChargePointDetailWidget::promptDeleteMessages);
    connect(pruneMenu->addAction("Remove deleted from database..."), &QAction::triggered, this, &ChargePointDetailWidget::promptPurgeMessages);
    pruneButton->setMenu(pruneMenu);
    filterRow->addWidget(pruneButton);
    filterRow->addStretch(1);
    messagesLayout->addLayout(filterRow);
    messagesLayout->addWidget(split, 1);
    updateFilterButton();
    split->addWidget(m_messages);
    split->addWidget(m_messageText);
    split->setSizes({300, 150});

    m_tabs = new QTabWidget(m_content);
    m_tabs->addTab(messagesPage, "Messages");
    layout->addWidget(m_tabs, 1);

    connect(m_messages, &QTableWidget::itemSelectionChanged, this, &ChargePointDetailWidget::showSelectedMessage);

    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(100);
    connect(&m_refreshTimer, &QTimer::timeout, this, &ChargePointDetailWidget::refresh);
    connect(m_cs, &CentralSystem::chargePointChanged, this, &ChargePointDetailWidget::scheduleRefresh);
    connect(m_cs, &CentralSystem::messageLogged, this, &ChargePointDetailWidget::scheduleRefresh);

    setChargePoint({});
}

void ChargePointDetailWidget::setChargePoint(const QString &chargePointId)
{
    m_id = chargePointId;
    m_content->setVisible(!m_id.isEmpty());
    m_hint->setVisible(m_id.isEmpty());
    m_messages->setRowCount(0);
    m_messageText->clear();
    if (!m_id.isEmpty())
        refresh();
}

void ChargePointDetailWidget::addTab(QWidget *page, const QString &title)
{
    m_tabs->addTab(page, title);
}

void ChargePointDetailWidget::scheduleRefresh(const QString &changedId)
{
    if (!m_id.isEmpty() && changedId == m_id && !m_refreshTimer.isActive())
        m_refreshTimer.start();
}

/// Reloads header and message log. The selected frame stays selected (matched by row id).
void ChargePointDetailWidget::refresh()
{
    if (m_id.isEmpty())
        return;
    Database *db = m_cs->database();
    m_title->setText(m_id);

    const auto cp = db->chargePoint(m_id);
    if (cp) {
        QStringList ident;
        for (const QString &s : {cp->vendor, cp->model, cp->serial})
            if (!s.isEmpty())
                ident << s;
        QString identity = ident.isEmpty() ? QStringLiteral("-") : ident.join(" / ");
        if (!cp->firmware.isEmpty())
            identity += "  (firmware " + cp->firmware + ")";
        m_identity->setText(identity);
        m_connection->setText(cp->connected ? "Connected from " + cp->remoteAddress : "Disconnected");
        m_heartbeat->setText(timeText(cp->lastHeartbeat));
        m_meterValue->setText(timeText(cp->lastMeterValue));
        m_connected->setText(timeText(cp->lastConnected));
        m_disconnected->setText(timeText(cp->lastDisconnected));
    }

    QStringList connectors;
    for (const auto &c : db->connectorStatuses(m_id)) {
        QString s = (c.connectorId == 0 ? QStringLiteral("charger") : QString::number(c.connectorId)) + ": " + c.status;
        if (!c.errorCode.isEmpty() && c.errorCode != "NoError")
            s += " (" + c.errorCode + ")";
        connectors << s;
    }
    m_connectors->setText(connectors.isEmpty() ? QStringLiteral("-") : connectors.join(",  "));

    if (const auto tx = db->lastTransaction(m_id)) {
        QString s = QString("#%1 on connector %2, idTag %3, started %4")
                        .arg(tx->id).arg(tx->connectorId).arg(tx->idTag, timeText(tx->startTime));
        if (tx->isActive())
            s += " - running";
        else
            s += QString(", stopped %1 (%2 Wh)").arg(timeText(tx->stopTime))
                     .arg(tx->meterStop.value_or(tx->meterStart) - tx->meterStart);
        m_transaction->setText(s);
    } else {
        m_transaction->setText("-");
    }

    // Rebuild the message table, keeping the selection.
    const qint64 selectedMessage = m_messages->currentRow() >= 0
        ? m_messages->item(m_messages->currentRow(), 0)->data(Qt::UserRole).toLongLong() : -1;
    const auto messages = db->recentMessages(m_id, kMessageLimit, m_hiddenActions);
    QSignalBlocker block(m_messages);
    m_messages->setRowCount(messages.size());
    int selectRow = -1;
    for (int row = 0; row < messages.size(); ++row) {
        const MessageRecord &m = messages.at(row);
        const QString action = m.action.isEmpty() ? QStringLiteral("-") : m.action;
        const QStringList cells = {timeText(m.timestamp), m.incoming ? "CP -> CS" : "CS -> CP", m.type, action};
        for (int col = 0; col < cells.size(); ++col)
            m_messages->setItem(row, col, new QTableWidgetItem(cells.at(col)));
        m_messages->item(row, 0)->setData(Qt::UserRole, m.id);
        m_messages->item(row, 0)->setData(Qt::UserRole + 1, m.raw);
        if (m.id == selectedMessage)
            selectRow = row;
    }
    m_messages->resizeColumnsToContents();
    if (selectRow >= 0)
        m_messages->selectRow(selectRow);
    else
        m_messageText->clear();
}

void ChargePointDetailWidget::showSelectedMessage()
{
    const int row = m_messages->currentRow();
    m_messageText->setPlainText(row >= 0 ? prettyJson(m_messages->item(row, 0)->data(Qt::UserRole + 1).toString()) : QString());
}

/// Label of the filter button: tells how many message actions are hidden.
void ChargePointDetailWidget::updateFilterButton()
{
    // Trailing spaces keep the menu arrow away from the text.
    m_filterButton->setText((m_hiddenActions.isEmpty() ? QStringLiteral("Filter: all messages")
                                                       : QString("Filter: %1 hidden").arg(m_hiddenActions.size()))
                            + QStringLiteral("   "));
}

/// Known OCPP 1.6 actions plus any other action seen for this charger, sorted.
QStringList ChargePointDetailWidget::allActions() const
{
    QStringList actions = kKnownActions;
    const QStringList extra = m_cs->database()->messageActions(m_id) + m_hiddenActions;
    for (const QString &a : extra)
        if (!actions.contains(a))
            actions << a;
    actions.sort();
    return actions;
}

/// Pruning step 1: asks what to delete, confirms, then marks the messages deleted (hidden).
void ChargePointDetailWidget::promptDeleteMessages()
{
    MessagePruneDialog dialog(m_cs->database(), m_id, allActions(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const MessageFilter filter = dialog.filter();
    const int n = m_cs->database()->countMessages(filter);
    const QString scope = filter.chargePointId.isEmpty() ? QStringLiteral("all chargers") : filter.chargePointId;
    if (QMessageBox::question(this, "Delete messages",
                              QString("Delete %1 message(s) of %2?\n\nThey disappear from the views now. "
                                      "Use Prune > Remove deleted from database to erase them for good.")
                                  .arg(n).arg(scope)) != QMessageBox::Yes)
        return;
    m_cs->database()->softDeleteMessages(filter);
    refresh();
}

/// Pruning step 2: asks whether to erase the soft-deleted messages of this charger or of all.
void ChargePointDetailWidget::promptPurgeMessages()
{
    Database *db = m_cs->database();
    const int mine = db->countDeletedMessages(m_id), all = db->countDeletedMessages();
    QMessageBox box(QMessageBox::Warning, "Remove deleted messages",
                    QString("Remove deleted messages from the database permanently?\n\n"
                            "%1: %2 message(s)\nAll chargers: %3 message(s)\n\nThis cannot be undone.")
                        .arg(m_id).arg(mine).arg(all),
                    QMessageBox::Cancel, this);
    auto *thisCharger = box.addButton("This charger", QMessageBox::DestructiveRole);
    auto *allChargers = box.addButton("All chargers", QMessageBox::DestructiveRole);
    thisCharger->setEnabled(mine > 0);
    allChargers->setEnabled(all > 0);
    box.exec();
    if (box.clickedButton() == thisCharger)
        db->purgeDeletedMessages(m_id);
    else if (box.clickedButton() == allChargers)
        db->purgeDeletedMessages();
}

/// Fills the filter menu: one checkable entry per OCPP 1.6 action and any other action seen (checked = shown),
/// plus "Show all" / "Hide all". Requests and responses of an action are filtered together, so
/// showing only StartTransaction and StopTransaction shows both directions of those.
void ChargePointDetailWidget::rebuildFilterMenu()
{
    QMenu *menu = m_filterButton->menu();
    menu->clear();
    const QStringList actions = allActions();

    connect(menu->addAction("Show all"), &QAction::triggered, this, [this] {
        m_hiddenActions.clear();
        updateFilterButton();
        refresh();
    });
    connect(menu->addAction("Hide all"), &QAction::triggered, this, [this, actions] {
        m_hiddenActions = actions;
        updateFilterButton();
        refresh();
    });
    menu->addSeparator();
    for (const QString &action : std::as_const(actions)) {
        QAction *a = menu->addAction(action);
        a->setCheckable(true);
        a->setChecked(!m_hiddenActions.contains(action));
        connect(a, &QAction::toggled, this, [this, action](bool shown) {
            if (shown)
                m_hiddenActions.removeAll(action);
            else if (!m_hiddenActions.contains(action))
                m_hiddenActions << action;
            updateFilterButton();
            refresh();
        });
    }
}
