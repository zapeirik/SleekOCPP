#pragma once

#include <QWidget>

class CentralSystem;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

/**
 * Configuration tab: shows the configuration keys of a charge point (as cached in the
 * database) and lets the user change them.
 *
 * - "Read from charger" sends GetConfiguration (all keys) and refreshes the table.
 * - Double-click a value of a writable key to edit it; the change is sent as ChangeConfiguration.
 *   The result (Accepted / Rejected / RebootRequired / NotSupported ...) is shown below the table.
 * - "Set" at the bottom changes a key that is not in the list (e.g. a vendor key).
 * - The filter box hides keys that do not contain the typed text.
 *
 * The table is rebuilt from the database whenever ChargePointCommands reports a configuration
 * change, so a rejected edit automatically snaps back to the old value.
 */
class ChargePointConfigurationWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ChargePointConfigurationWidget(CentralSystem *centralSystem, QWidget *parent = nullptr);

    void setChargePoint(const QString &chargePointId);

    /// Reloads the table from the cache.
    void reload();

private:
    void readFromCharger();
    void changeKey(const QString &key, const QString &value);
    void applyFilter();
    void showResult(const QString &text, bool ok);

    CentralSystem *m_cs;
    QString m_id;
    QLineEdit *m_filter;
    QPushButton *m_read;
    QTableWidget *m_table;
    QLineEdit *m_newKey, *m_newValue;
    QPushButton *m_set;
    QLabel *m_result;
};
