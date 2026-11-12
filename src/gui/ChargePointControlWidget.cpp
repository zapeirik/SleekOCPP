#include "gui/ChargePointControlWidget.h"

#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

#include "core/CentralSystem.h"
#include "core/ChargePointCommands.h"
#include "core/db/Database.h"

namespace {

/// A group box with a form layout; rows are added by the caller.
QGroupBox *group(const QString &title, QFormLayout **form)
{
    auto *box = new QGroupBox(title);
    *form = new QFormLayout(box);
    return box;
}

QSpinBox *spin(int min, int max, int value = 0)
{
    auto *s = new QSpinBox;
    s->setRange(min, max);
    s->setValue(value);
    return s;
}

} // namespace

ChargePointControlWidget::ChargePointControlWidget(CentralSystem *centralSystem, const QString &defaultIdTag, QWidget *parent)
    : QWidget(parent), m_cs(centralSystem)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    // The forms can be taller than the tab, so they scroll; the result stays visible below.
    auto *page = new QWidget;
    auto *pageLayout = new QVBoxLayout(page);
    m_idTag = new QLineEdit(defaultIdTag);
    m_idTag->setObjectName("idTag");
    pageLayout->addWidget(buildChargingGroup());
    pageLayout->addWidget(buildProfileGroup());
    pageLayout->addWidget(buildChargerGroup());
    pageLayout->addWidget(buildRawGroup());
    pageLayout->addStretch(1);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(page);
    outer->addWidget(scroll, 1);

    m_result = new QLabel;
    m_result->setObjectName("controlResult");
    m_result->setWordWrap(true);
    m_resultDetails = new QPlainTextEdit;
    m_resultDetails->setReadOnly(true);
    m_resultDetails->setMaximumHeight(90);
    m_resultDetails->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_resultDetails->hide();
    outer->addWidget(m_result);
    outer->addWidget(m_resultDetails);

    setChargePoint({});
    // A transaction started by the charger itself should also fill the stop form.
    connect(m_cs, &CentralSystem::chargePointChanged, this, [this](const QString &id) {
        if (id == m_id)
            refreshTransactionId();
    });
}

void ChargePointControlWidget::setChargePoint(const QString &chargePointId)
{
    m_id = chargePointId;
    setEnabled(!m_id.isEmpty());
    m_result->clear();
    m_resultDetails->hide();
    refreshTransactionId();
}

/// Pre-fills the stop form with the transaction that is running now (if any).
void ChargePointControlWidget::refreshTransactionId()
{
    if (m_id.isEmpty())
        return;
    const auto tx = m_cs->database()->lastTransaction(m_id);
    if (tx && tx->isActive())
        m_transactionId->setValue(int(tx->id));
}

QWidget *ChargePointControlWidget::buildChargingGroup()
{
    QFormLayout *form;
    auto *box = group("Charging", &form);
    m_startConnector = spin(0, 99);
    m_startConnector->setSpecialValueText("any");
    m_transactionId = spin(0, 2000000000);
    auto *start = new QPushButton("Start charging");
    auto *stop = new QPushButton("Stop charging");
    m_idTag->setToolTip("idTag sent in RemoteStartTransaction. Entered per start.");
    form->addRow("idTag", m_idTag);
    form->addRow("Connector", m_startConnector);
    form->addRow(start);
    form->addRow("Transaction id", m_transactionId);
    form->addRow(stop);

    connect(start, &QPushButton::clicked, this, [this] {
        const QString tag = m_idTag->text().trimmed();
        if (tag.isEmpty())
            return showOutcome("RemoteStartTransaction: enter an idTag first.", false);
        std::optional<int> connector;
        if (m_startConnector->value() > 0)
            connector = m_startConnector->value();
        QPointer<ChargePointControlWidget> self(this);
        m_cs->commands()->remoteStartTransaction(m_id, tag, connector, {}, [self](const CallOutcome &o) {
            if (self)
                self->showOutcome("RemoteStartTransaction: " + o.summary(), o.summary() == "Accepted", QString::fromUtf8(QJsonDocument(o.payload).toJson(QJsonDocument::Compact)));
        });
    });
    connect(stop, &QPushButton::clicked, this, [this] {
        QPointer<ChargePointControlWidget> self(this);
        m_cs->commands()->remoteStopTransaction(m_id, m_transactionId->value(), [self](const CallOutcome &o) {
            if (self)
                self->showOutcome("RemoteStopTransaction: " + o.summary(), o.summary() == "Accepted");
        });
    });
    return box;
}

QWidget *ChargePointControlWidget::buildProfileGroup()
{
    QFormLayout *form;
    auto *box = group("Charging profile (limit)", &form);
    m_profileConnector = spin(0, 99);
    m_profileConnector->setToolTip("0 = the whole charge point (ChargePointMaxProfile / TxDefaultProfile)");
    m_profileId = spin(1, 999999, 1);
    m_stackLevel = spin(0, 99);
    m_duration = spin(0, 86400 * 7);
    m_duration->setSuffix(" s");
    m_duration->setSpecialValueText("none");
    m_purpose = new QComboBox;
    m_purpose->addItems({"TxDefaultProfile", "TxProfile", "ChargePointMaxProfile"});
    m_kind = new QComboBox;
    m_kind->addItems({"Absolute", "Recurring", "Relative"});
    m_unit = new QComboBox;
    m_unit->addItems({"A", "W"});
    m_limit = new QDoubleSpinBox;
    m_limit->setRange(0, 1000000);
    m_limit->setValue(16);
    m_limit->setDecimals(1);
    form->addRow("Connector", m_profileConnector);
    form->addRow("Profile id", m_profileId);
    form->addRow("Stack level", m_stackLevel);
    form->addRow("Purpose", m_purpose);
    form->addRow("Kind", m_kind);
    form->addRow("Limit", m_limit);
    form->addRow("Unit", m_unit);
    form->addRow("Duration", m_duration);
    auto *set = new QPushButton("Send SetChargingProfile");
    auto *clear = new QPushButton("Clear all profiles");
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(set);
    buttons->addWidget(clear);
    form->addRow(buttons);

    connect(set, &QPushButton::clicked, this, [this] {
        // A TxProfile applies to one transaction; use the one in the stop form.
        const int tx = m_purpose->currentText() == "TxProfile" ? m_transactionId->value() : 0;
        const QJsonObject profile = buildChargingProfile(m_profileId->value(), m_stackLevel->value(), m_purpose->currentText(),
                                                         m_kind->currentText(), m_unit->currentText(), m_limit->value(),
                                                         m_duration->value(), tx);
        QPointer<ChargePointControlWidget> self(this);
        m_cs->commands()->setChargingProfile(m_id, m_profileConnector->value(), profile, [self](const CallOutcome &o) {
            if (self)
                self->showOutcome("SetChargingProfile: " + o.summary(), o.summary() == "Accepted");
        });
    });
    connect(clear, &QPushButton::clicked, this, [this] {
        QPointer<ChargePointControlWidget> self(this);
        m_cs->commands()->clearChargingProfile(m_id, {}, {}, {}, {}, [self](const CallOutcome &o) {
            if (self)
                self->showOutcome("ClearChargingProfile: " + o.summary(), o.summary() == "Accepted");
        });
    });
    return box;
}

QWidget *ChargePointControlWidget::buildChargerGroup()
{
    QFormLayout *form;
    auto *box = group("Charger", &form);
    m_resetType = new QComboBox;
    m_resetType->addItems({"Soft", "Hard"});
    auto *reset = new QPushButton("Reset");
    auto *row1 = new QHBoxLayout;
    row1->addWidget(m_resetType);
    row1->addWidget(reset);
    form->addRow("Reset", row1);

    m_trigger = new QComboBox;
    m_trigger->addItems({"StatusNotification", "MeterValues", "Heartbeat", "BootNotification",
                         "DiagnosticsStatusNotification", "FirmwareStatusNotification"});
    auto *trigger = new QPushButton("Trigger");
    auto *row2 = new QHBoxLayout;
    row2->addWidget(m_trigger);
    row2->addWidget(trigger);
    form->addRow("TriggerMessage", row2);

    connect(reset, &QPushButton::clicked, this, [this] {
        QPointer<ChargePointControlWidget> self(this);
        m_cs->commands()->reset(m_id, m_resetType->currentText(), [self](const CallOutcome &o) {
            if (self)
                self->showOutcome("Reset: " + o.summary(), o.summary() == "Accepted");
        });
    });
    connect(trigger, &QPushButton::clicked, this, [this] {
        QPointer<ChargePointControlWidget> self(this);
        m_cs->commands()->triggerMessage(m_id, m_trigger->currentText(), {}, [self](const CallOutcome &o) {
            if (self)
                self->showOutcome("TriggerMessage: " + o.summary(), o.summary() == "Accepted");
        });
    });
    return box;
}

QWidget *ChargePointControlWidget::buildRawGroup()
{
    QFormLayout *form;
    auto *box = group("Raw message", &form);
    m_rawAction = new QLineEdit;
    m_rawAction->setObjectName("rawAction");
    m_rawAction->setPlaceholderText("Action, e.g. GetLocalListVersion");
    m_rawPayload = new QPlainTextEdit("{}");
    m_rawPayload->setObjectName("rawPayload");
    m_rawPayload->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_rawPayload->setMaximumHeight(80);
    auto *send = new QPushButton("Send");
    form->addRow("Action", m_rawAction);
    form->addRow("Payload", m_rawPayload);
    form->addRow(send);

    connect(send, &QPushButton::clicked, this, [this] {
        const QString action = m_rawAction->text().trimmed();
        if (action.isEmpty())
            return showOutcome("Raw message: enter an action.", false);
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(m_rawPayload->toPlainText().toUtf8(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject())
            return showOutcome("Raw message: payload is not a JSON object (" + err.errorString() + ").", false);
        QPointer<ChargePointControlWidget> self(this);
        m_cs->commands()->send(m_id, action, doc.object(), [self, action](const CallOutcome &o) {
            if (self)
                self->showOutcome(action + ": " + o.summary(), o.status == CallOutcome::Status::Result,
                                  QString::fromUtf8(QJsonDocument(o.payload).toJson(QJsonDocument::Indented)));
        });
    });
    return box;
}

QJsonObject ChargePointControlWidget::buildChargingProfile(int profileId, int stackLevel, const QString &purpose, const QString &kind,
                                                           const QString &rateUnit, double limit, int durationSeconds, int transactionId)
{
    QJsonObject schedule{
        {"chargingRateUnit", rateUnit},
        {"chargingSchedulePeriod", QJsonArray{QJsonObject{{"startPeriod", 0}, {"limit", limit}}}},
    };
    if (durationSeconds > 0)
        schedule["duration"] = durationSeconds;
    if (kind == "Absolute")
        schedule["startSchedule"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    QJsonObject profile{
        {"chargingProfileId", profileId},
        {"stackLevel", stackLevel},
        {"chargingProfilePurpose", purpose},
        {"chargingProfileKind", kind},
        {"chargingSchedule", schedule},
    };
    if (kind == "Recurring")
        profile["recurrencyKind"] = "Daily";
    if (purpose == "TxProfile" && transactionId > 0)
        profile["transactionId"] = transactionId;
    return profile;
}

/// Shows the latest answer under the forms. @p details (optional) is the payload text.
void ChargePointControlWidget::showOutcome(const QString &what, bool ok, const QString &details)
{
    m_result->setStyleSheet(ok ? "color: #2ea043;" : "color: #d73a49;");
    m_result->setText(what);
    const bool hasDetails = !details.isEmpty() && details != "{}";
    m_resultDetails->setPlainText(details);
    m_resultDetails->setVisible(hasDetails);
}
