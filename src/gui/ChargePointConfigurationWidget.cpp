#include "gui/ChargePointConfigurationWidget.h"

#include <QHBoxLayout>
#include <QJsonArray>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "core/CentralSystem.h"
#include "core/ChargePointCommands.h"
#include "core/db/Database.h"

namespace {
enum Column { KeyColumn, ValueColumn, AccessColumn, UpdatedColumn };
}

ChargePointConfigurationWidget::ChargePointConfigurationWidget(CentralSystem *centralSystem, QWidget *parent)
    : QWidget(parent), m_cs(centralSystem)
{
    auto *layout = new QVBoxLayout(this);

    auto *top = new QHBoxLayout;
    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText("Filter keys...");
    m_filter->setClearButtonEnabled(true);
    m_read = new QPushButton("Read from charger", this);
    top->addWidget(m_filter, 1);
    top->addWidget(m_read);
    layout->addLayout(top);

    m_table = new QTableWidget(0, 4, this);
    m_table->setHorizontalHeaderLabels({"Key", "Value", "Access", "Updated"});
    m_table->verticalHeader()->hide();
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSortingEnabled(false);
    layout->addWidget(m_table, 1);

    auto *bottom = new QHBoxLayout;
    m_newKey = new QLineEdit(this);
    m_newKey->setPlaceholderText("Key");
    m_newValue = new QLineEdit(this);
    m_newValue->setPlaceholderText("Value");
    m_set = new QPushButton("Set", this);
    m_set->setEnabled(false);
    bottom->addWidget(m_newKey, 1);
    bottom->addWidget(m_newValue, 2);
    bottom->addWidget(m_set);
    layout->addLayout(bottom);

    m_result = new QLabel(this);
    m_result->setWordWrap(true);
    layout->addWidget(m_result);

    connect(m_filter, &QLineEdit::textChanged, this, &ChargePointConfigurationWidget::applyFilter);
    connect(m_read, &QPushButton::clicked, this, &ChargePointConfigurationWidget::readFromCharger);
    connect(m_newKey, &QLineEdit::textChanged, this, [this](const QString &t) { m_set->setEnabled(!t.trimmed().isEmpty()); });
    connect(m_set, &QPushButton::clicked, this, [this] {
        changeKey(m_newKey->text().trimmed(), m_newValue->text());
    });
    connect(m_newValue, &QLineEdit::returnPressed, m_set, &QPushButton::click);
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (item->column() == ValueColumn)
            changeKey(m_table->item(item->row(), KeyColumn)->text(), item->text());
    });
    connect(m_cs->commands(), &ChargePointCommands::configurationChanged, this, [this](const QString &id) {
        if (id == m_id)
            reload();
    });

    setChargePoint({});
}

void ChargePointConfigurationWidget::setChargePoint(const QString &chargePointId)
{
    m_id = chargePointId;
    m_result->clear();
    m_read->setEnabled(!m_id.isEmpty());
    reload();
}

/// Rebuilds the table from the cached configuration. Values of writable keys are editable.
void ChargePointConfigurationWidget::reload()
{
    QSignalBlocker block(m_table); // setting items must not look like user edits
    m_table->setRowCount(0);
    if (m_id.isEmpty())
        return;
    const auto keys = m_cs->database()->configuration(m_id);
    m_table->setRowCount(keys.size());
    for (int row = 0; row < keys.size(); ++row) {
        const ConfigKeyRecord &k = keys.at(row);
        auto *key = new QTableWidgetItem(k.key);
        auto *value = new QTableWidgetItem(k.value);
        auto *access = new QTableWidgetItem(k.readonly ? "read-only" : "read/write");
        auto *updated = new QTableWidgetItem(k.updatedAt.isValid() ? k.updatedAt.toLocalTime().toString("yyyy-MM-dd HH:mm:ss") : "-");
        for (auto *i : {key, access, updated})
            i->setFlags(i->flags() & ~Qt::ItemIsEditable);
        if (k.readonly) {
            value->setFlags(value->flags() & ~Qt::ItemIsEditable);
            value->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
        }
        m_table->setItem(row, KeyColumn, key);
        m_table->setItem(row, ValueColumn, value);
        m_table->setItem(row, AccessColumn, access);
        m_table->setItem(row, UpdatedColumn, updated);
    }
    m_table->resizeColumnToContents(KeyColumn);
    m_table->resizeColumnToContents(ValueColumn);
    applyFilter();
}

void ChargePointConfigurationWidget::applyFilter()
{
    const QString f = m_filter->text().trimmed();
    for (int row = 0; row < m_table->rowCount(); ++row)
        m_table->setRowHidden(row, !f.isEmpty() && !m_table->item(row, KeyColumn)->text().contains(f, Qt::CaseInsensitive));
}

void ChargePointConfigurationWidget::readFromCharger()
{
    if (m_id.isEmpty())
        return;
    showResult("Reading configuration...", true);
    QPointer<ChargePointConfigurationWidget> self(this);
    m_cs->commands()->getConfiguration(m_id, {}, [self](const CallOutcome &o) {
        if (!self)
            return;
        const bool ok = o.status == CallOutcome::Status::Result;
        self->showResult(ok ? QString("Configuration read (%1 keys).").arg(o.payload["configurationKey"].toArray().size())
                            : "GetConfiguration failed: " + o.summary(), ok);
    });
}

/// Sends ChangeConfiguration and shows the answer. The cache (and so this table) is updated by
/// ChargePointCommands when the charger accepts; otherwise the table is reloaded to undo the edit.
void ChargePointConfigurationWidget::changeKey(const QString &key, const QString &value)
{
    if (m_id.isEmpty() || key.isEmpty())
        return;
    QPointer<ChargePointConfigurationWidget> self(this);
    const QString id = m_id;
    m_cs->commands()->changeConfiguration(id, key, value, [self, key, value, id](const CallOutcome &o) {
        if (!self)
            return;
        const QString s = o.summary();
        const bool ok = o.status == CallOutcome::Status::Result && (s == "Accepted" || s == "RebootRequired");
        self->showResult(QString("%1 = \"%2\": %3").arg(key, value, s == "RebootRequired" ? "Accepted, reboot required" : s), ok);
        if (id == self->m_id)
            self->reload();
    });
}

void ChargePointConfigurationWidget::showResult(const QString &text, bool ok)
{
    m_result->setStyleSheet(ok ? "color: #2ea043;" : "color: #d73a49;");
    m_result->setText(text);
}
