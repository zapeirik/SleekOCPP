#pragma once

#include <QMainWindow>

#include "core/AppConfig.h"

class CentralSystem;
class ChargePointListWidget;
class ChargePointDetailWidget;
class ChargePointModel;
class QLabel;

/**
 * Main application window: the charger list, and a status bar with the listening ports
 * and the number of connected chargers. The detail view will be added next to the list.
 *
 * The window only displays and controls; the server itself (CentralSystem) is owned by main().
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    /// @p centralSystem must outlive the window.
    MainWindow(const AppConfig &config, CentralSystem *centralSystem, QWidget *parent = nullptr);

    ChargePointListWidget *list() const { return m_list; }
    ChargePointDetailWidget *detail() const { return m_detail; }

    /// Shows server start-up problems (e.g. port in use) in a red banner and a dialog.
    void showStartupErrors(const QStringList &errors);

private:
    void updateStatusBar();

    AppConfig m_config;
    CentralSystem *m_cs;
    ChargePointModel *m_model;
    ChargePointListWidget *m_list;
    ChargePointDetailWidget *m_detail;
    QLabel *m_banner;
    QLabel *m_status;
};
