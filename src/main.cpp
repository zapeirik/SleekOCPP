#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <memory>

#include "core/AppConfig.h"
#include "core/Logger.h"
#include "core/CentralSystem.h"
#include "core/RestApi.h"
#include "core/db/Database.h"
#include "core/ocpp/OcppServer.h"

#ifdef SLEEKOCPP_HAS_GUI
#include <QApplication>
#include <QIcon>
#include "gui/MainWindow.h"
#endif

/**
 * Entry point. Parses options and starts either the GUI or the headless server.
 * Both modes run the same server code in-process; only the front end differs.
 */
int main(int argc, char *argv[])
{
    // Scan for --headless before creating the app object: a GUI app needs QApplication,
    // a headless one must not require a display (QCoreApplication).
    bool headless = false;
    for (int i = 1; i < argc; ++i)
        headless = headless || QString::fromLocal8Bit(argv[i]) == QLatin1String("--headless");

#ifndef SLEEKOCPP_HAS_GUI
    headless = true; // Built without GUI support
#endif

    std::unique_ptr<QCoreApplication> app;
#ifdef SLEEKOCPP_HAS_GUI
    if (!headless) {
        app = std::make_unique<QApplication>(argc, argv);
        QApplication::setWindowIcon(QIcon(":/icons/icon_512x512.png"));
    }
#endif
    if (!app)
        app = std::make_unique<QCoreApplication>(argc, argv);

    QCoreApplication::setApplicationName("SleekOCPP");
    QCoreApplication::setOrganizationName("SleekOCPP");
    QCoreApplication::setApplicationVersion("0.1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("OCPP 1.6 central system with optional GUI");
    parser.addHelpOption();
    parser.addVersionOption();
    AppConfig::addOptions(parser);
    parser.process(*app);

    AppConfig config = AppConfig::resolve(parser);
    if (headless)
        config.mode = AppConfig::Mode::Headless;

    QDir().mkpath(QFileInfo(config.dbPath).absolutePath());
    Logger::init(Logger::levelFromString(config.logLevel), config.logFile);
    qInfo().noquote() << "SleekOCPP starting in"
                      << (config.mode == AppConfig::Mode::Gui ? "GUI" : "headless")
                      << "mode, OCPP port" << config.ocppPort << "db" << config.dbPath;

    // The server runs in-process in both modes; only the front end differs.
    Database database;
    QString dbError;
    if (!database.open(config.dbPath, &dbError)) {
        qCritical().noquote() << "Cannot open database" << config.dbPath << ":" << dbError;
        return 1;
    }
    OcppServer server;
    CentralSystem centralSystem(&server, &database);
    RestApi restApi(&centralSystem);
    // Listen failures are logged by the servers. Headless exits; the GUI stays open and shows the problem.
    QStringList startupErrors;
    if (!server.listen(config.ocppPort))
        startupErrors << QString("OCPP port %1 could not be opened (already in use?).").arg(config.ocppPort);
    QHostAddress restAddress(config.restBind);
    if (restAddress.isNull()) {
        qCritical().noquote() << "Invalid --rest-bind address" << config.restBind;
        startupErrors << QString("REST bind address \"%1\" is not a valid IP address.").arg(config.restBind);
    } else if (!restApi.listen(config.restPort, restAddress)) {
        startupErrors << QString("REST port %1 could not be opened (already in use?).").arg(config.restPort);
    }

#ifdef SLEEKOCPP_HAS_GUI
    if (config.mode == AppConfig::Mode::Gui) {
        MainWindow window(config, &centralSystem);
        window.show();
        if (!startupErrors.isEmpty())
            window.showStartupErrors(startupErrors);
        return app->exec();
    }
#endif
    if (!startupErrors.isEmpty())
        return 1;

    return app->exec();
}
