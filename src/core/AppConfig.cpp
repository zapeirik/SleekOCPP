#include "AppConfig.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

QString AppConfig::settingsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/sleekocpp.ini";
}

QString AppConfig::defaultDbPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/sleekocpp.db";
}

void AppConfig::addOptions(QCommandLineParser &parser)
{
    parser.addOptions({
        {"gui", "Start with the GUI (default)."},
        {"headless", "Run as a server without GUI, logging to console or file."},
        {{"p", "port"}, "OCPP WebSocket port (default 9000).", "port"},
        {"rest-port", "REST API port (default 8080).", "port"},
        {"rest-bind", "Address the REST API listens on (default 127.0.0.1; use 0.0.0.0 for all interfaces, there is no authentication).", "address"},
        {"db", "SQLite database file.", "file"},
        {"log-level", "debug, info, warning or error (default info).", "level"},
        {"log-file", "Write log to this file instead of the console.", "file"},
        {"id-tag", "Default idTag for remote start.", "idTag"},
    });
}

AppConfig AppConfig::resolve(const QCommandLineParser &parser)
{
    AppConfig cfg;
    cfg.load();
    cfg.applyCommandLine(parser);
    if (cfg.dbPath.isEmpty())
        cfg.dbPath = defaultDbPath();
    return cfg;
}

void AppConfig::load(const QString &path)
{
    QSettings s(path, QSettings::IniFormat);
    ocppPort = static_cast<quint16>(s.value("ocppPort", ocppPort).toUInt());
    restPort = static_cast<quint16>(s.value("restPort", restPort).toUInt());
    restBind = s.value("restBind", restBind).toString();
    dbPath = s.value("dbPath", dbPath).toString();
    logLevel = s.value("logLevel", logLevel).toString();
    logFile = s.value("logFile", logFile).toString();
    defaultIdTag = s.value("defaultIdTag", defaultIdTag).toString();
}

void AppConfig::save(const QString &path) const
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSettings s(path, QSettings::IniFormat);
    s.setValue("ocppPort", ocppPort);
    s.setValue("restPort", restPort);
    s.setValue("restBind", restBind);
    s.setValue("dbPath", dbPath);
    s.setValue("logLevel", logLevel);
    s.setValue("logFile", logFile);
    s.setValue("defaultIdTag", defaultIdTag);
}

void AppConfig::applyCommandLine(const QCommandLineParser &parser)
{
    mode = parser.isSet("headless") ? Mode::Headless : Mode::Gui;
    if (parser.isSet("port"))
        ocppPort = static_cast<quint16>(parser.value("port").toUInt());
    if (parser.isSet("rest-port"))
        restPort = static_cast<quint16>(parser.value("rest-port").toUInt());
    if (parser.isSet("rest-bind"))
        restBind = parser.value("rest-bind");
    if (parser.isSet("db"))
        dbPath = parser.value("db");
    if (parser.isSet("log-level"))
        logLevel = parser.value("log-level");
    if (parser.isSet("log-file"))
        logFile = parser.value("log-file");
    if (parser.isSet("id-tag"))
        defaultIdTag = parser.value("id-tag");
}
