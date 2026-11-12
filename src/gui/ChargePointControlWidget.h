#pragma once

#include <QJsonObject>
#include <QWidget>

class CentralSystem;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;

/**
 * Control tab: sends commands to the selected charge point and shows the answer.
 *
 *  - Start charging: RemoteStartTransaction. The idTag is typed per start (pre-filled with the
 *    configured default); connector 0 lets the charger choose.
 *  - Stop charging: RemoteStopTransaction; the transaction id is pre-filled with the running one.
 *  - Charging profile: SetChargingProfile with a single-period schedule (a current/power limit),
 *    or ClearChargingProfile.
 *  - Charger: Reset and TriggerMessage.
 *  - Raw message: any action with a JSON payload, for things the buttons do not cover.
 *
 * To add another button: build the payload and call send() on CentralSystem::commands(), then
 * pass the outcome to showOutcome().
 */
class ChargePointControlWidget : public QWidget
{
    Q_OBJECT
public:
    ChargePointControlWidget(CentralSystem *centralSystem, const QString &defaultIdTag, QWidget *parent = nullptr);

    void setChargePoint(const QString &chargePointId);

    /// Builds the OCPP `csChargingProfiles` object for a single-period profile.
    /// Absolute profiles start now (UTC); recurring ones are daily. @p durationSeconds 0 = no duration.
    /// @p transactionId is only used for TxProfile (0 = omit).
    static QJsonObject buildChargingProfile(int profileId, int stackLevel, const QString &purpose, const QString &kind,
                                            const QString &rateUnit, double limit, int durationSeconds, int transactionId);

private:
    QWidget *buildChargingGroup();
    QWidget *buildProfileGroup();
    QWidget *buildChargerGroup();
    QWidget *buildRawGroup();
    void refreshTransactionId();
    void showOutcome(const QString &what, bool ok, const QString &details = {});

    CentralSystem *m_cs;
    QString m_id;

    QLineEdit *m_idTag;
    QSpinBox *m_startConnector, *m_transactionId;

    QSpinBox *m_profileConnector, *m_profileId, *m_stackLevel, *m_duration;
    QComboBox *m_purpose, *m_kind, *m_unit;
    QDoubleSpinBox *m_limit;

    QComboBox *m_resetType, *m_trigger;

    QLineEdit *m_rawAction;
    QPlainTextEdit *m_rawPayload;

    QLabel *m_result;
    QPlainTextEdit *m_resultDetails;
};
