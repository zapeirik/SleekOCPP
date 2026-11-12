#include "gui/MessagePruneDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

MessagePruneDialog::MessagePruneDialog(Database *db, const QString &chargePointId, const QStringList &actions,
                                       QWidget *parent)
    : QDialog(parent), m_db(db), m_id(chargePointId)
{
    setWindowTitle("Delete messages");
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel("Choose which messages to delete. Leave a group empty to not restrict by it.\n"
                                 "Deleted messages are hidden at once and can be removed from the database afterwards."));
    auto *form = new QFormLayout;

    m_scope = new QComboBox(this);
    m_scope->setObjectName("pruneScope");
    m_scope->addItem("Charger " + m_id, m_id);
    m_scope->addItem("All chargers", QString());
    form->addRow("Charger", m_scope);

    m_actions = new QListWidget(this);
    m_actions->setObjectName("pruneActions");
    for (const QString &a : actions) {
        auto *item = new QListWidgetItem(a, m_actions);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
    }
    form->addRow("Message types", m_actions);

    auto *types = new QHBoxLayout;
    m_call = new QCheckBox("Call", this);
    m_result = new QCheckBox("CallResult", this);
    m_error = new QCheckBox("CallError", this);
    for (auto *c : {m_call, m_result, m_error}) {
        types->addWidget(c);
        connect(c, &QCheckBox::toggled, this, &MessagePruneDialog::updateCount);
    }
    types->addStretch(1);
    form->addRow("Call types", types);

    auto *date = new QHBoxLayout;
    m_useDate = new QCheckBox("Older than", this);
    m_before = new QDateTimeEdit(QDateTime::currentDateTime(), this);
    m_before->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
    m_before->setCalendarPopup(true);
    m_before->setEnabled(false);
    date->addWidget(m_useDate);
    date->addWidget(m_before);
    date->addStretch(1);
    form->addRow("Date", date);
    layout->addLayout(form);

    m_count = new QLabel(this);
    m_count->setObjectName("pruneCount");
    layout->addWidget(m_count);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_buttons->addButton("Delete...", QDialogButtonBox::AcceptRole)->setObjectName("pruneDelete");
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(m_buttons);

    connect(m_useDate, &QCheckBox::toggled, m_before, &QWidget::setEnabled);
    connect(m_useDate, &QCheckBox::toggled, this, &MessagePruneDialog::updateCount);
    connect(m_before, &QDateTimeEdit::dateTimeChanged, this, &MessagePruneDialog::updateCount);
    connect(m_scope, &QComboBox::currentIndexChanged, this, &MessagePruneDialog::updateCount);
    connect(m_actions, &QListWidget::itemChanged, this, &MessagePruneDialog::updateCount);
    updateCount();
}

MessageFilter MessagePruneDialog::filter() const
{
    MessageFilter f;
    f.chargePointId = m_scope->currentData().toString();
    for (int i = 0; i < m_actions->count(); ++i)
        if (m_actions->item(i)->checkState() == Qt::Checked)
            f.actions << m_actions->item(i)->text();
    if (m_call->isChecked())
        f.types << "Call";
    if (m_result->isChecked())
        f.types << "CallResult";
    if (m_error->isChecked())
        f.types << "CallError";
    if (m_useDate->isChecked())
        f.before = m_before->dateTime().toUTC();
    return f;
}

void MessagePruneDialog::updateCount()
{
    const int n = m_db->countMessages(filter());
    m_count->setText(QString("%1 message(s) match.").arg(n));
    m_buttons->button(QDialogButtonBox::Cancel)->setDefault(true);
    m_buttons->findChild<QPushButton *>("pruneDelete")->setEnabled(n > 0);
}
