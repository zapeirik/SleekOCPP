#pragma once

#include <QDialog>

#include "core/db/Database.h"

class QCheckBox;
class QComboBox;
class QDateTimeEdit;
class QDialogButtonBox;
class QLabel;
class QListWidget;

/**
 * Dialog to choose which messages to delete (pruning step 1: soft delete).
 *
 * The user picks the charger (this one or all), message actions, call types (Call / CallResult /
 * CallError) and optionally "older than" a date. Nothing ticked means no restriction on that
 * field. A live counter shows how many messages match. The dialog only builds a MessageFilter;
 * the caller asks for confirmation and performs the deletion.
 */
class MessagePruneDialog : public QDialog
{
    Q_OBJECT
public:
    /// @p chargePointId is the charger shown in the detail view; @p actions are the choices offered.
    MessagePruneDialog(Database *db, const QString &chargePointId, const QStringList &actions,
                       QWidget *parent = nullptr);

    /// The selection as a database filter.
    MessageFilter filter() const;

private:
    void updateCount();

    Database *m_db;
    QString m_id;
    QComboBox *m_scope;
    QListWidget *m_actions;
    QCheckBox *m_call, *m_result, *m_error, *m_useDate;
    QDateTimeEdit *m_before;
    QLabel *m_count;
    QDialogButtonBox *m_buttons;
};
