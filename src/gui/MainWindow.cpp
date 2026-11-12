#include "MainWindow.h"

#include <QLabel>
#include <QMessageBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

#include "core/CentralSystem.h"
#include "core/ChargePointModel.h"
#include "core/ocpp/OcppServer.h"
#include "gui/ChargePointConfigurationWidget.h"
#include "gui/ChargePointControlWidget.h"
#include "gui/ChargePointDetailWidget.h"
#include "gui/ChargePointListWidget.h"

MainWindow::MainWindow(const AppConfig &config, CentralSystem *centralSystem, QWidget *parent)
    : QMainWindow(parent), m_config(config), m_cs(centralSystem)
{
    setWindowTitle("SleekOCPP");
    resize(1300, 700);

    m_model = new ChargePointModel(m_cs, this);
    const QString hint = QString("No charge points yet.\nPoint a charger at  ws://<this computer>:%1/<charge point id>")
                             .arg(m_cs->server()->port());
    m_list = new ChargePointListWidget(m_model, hint, this);
    m_detail = new ChargePointDetailWidget(m_cs, this);
    connect(m_list, &ChargePointListWidget::chargePointSelected, m_detail, &ChargePointDetailWidget::setChargePoint);
    auto *configuration = new ChargePointConfigurationWidget(m_cs, this);
    m_detail->addTab(configuration, "Configuration");
    connect(m_list, &ChargePointListWidget::chargePointSelected, configuration, &ChargePointConfigurationWidget::setChargePoint);
    auto *control = new ChargePointControlWidget(m_cs, m_config.defaultIdTag, this);
    m_detail->addTab(control, "Control");
    connect(m_list, &ChargePointListWidget::chargePointSelected, control, &ChargePointControlWidget::setChargePoint);

    // Central area: error banner (hidden unless start-up failed) above list | detail.
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_banner = new QLabel(central);
    m_banner->setObjectName("startupErrorBanner");
    m_banner->setStyleSheet("background:#d73a49; color:white; padding:6px;");
    m_banner->hide();
    auto *splitter = new QSplitter(Qt::Horizontal, central);
    splitter->addWidget(m_list);
    splitter->addWidget(m_detail);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({600, 700});
    layout->addWidget(m_banner);
    layout->addWidget(splitter, 1);
    setCentralWidget(central);

    m_status = new QLabel(this);
    statusBar()->addWidget(m_status);
    connect(m_model, &QAbstractItemModel::rowsInserted, this, &MainWindow::updateStatusBar);
    connect(m_model, &QAbstractItemModel::modelReset, this, &MainWindow::updateStatusBar);
    connect(m_model, &QAbstractItemModel::dataChanged, this, &MainWindow::updateStatusBar);
    updateStatusBar();
}

void MainWindow::showStartupErrors(const QStringList &errors)
{
    const QString text = errors.join("\n");
    m_banner->setText(text);
    m_banner->show();
    QTimer::singleShot(0, this, [this, text] {
        auto *box = new QMessageBox(QMessageBox::Critical, "SleekOCPP",
                                    text + "\n\nChange the port with --port / --rest-port or in the ini file.", QMessageBox::Ok, this);
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->open(); // non-blocking
    });
}

/// Shows ports and how many chargers are connected.
void MainWindow::updateStatusBar()
{
    int connected = 0;
    for (int row = 0; row < m_model->rowCount(); ++row)
        connected += m_model->record(row).connected ? 1 : 0;
    m_status->setText(QString("OCPP port %1   REST port %2   %3 of %4 charge points connected")
                          .arg(m_cs->server()->port())
                          .arg(m_config.restPort)
                          .arg(connected)
                          .arg(m_model->rowCount()));
}
