#pragma once

#include <QCommandLineParser>
#include <QString>

/**
 * All application settings in one place.
 *
 * Values are resolved in this order (later wins):
 *   1. built-in defaults (below)
 *   2. settings file (INI, see settingsPath())
 *   3. command line options (see addOptions())
 *
 * To add a new setting: add a field here, then handle it in load()/save() and,
 * if it should be overridable, in addOptions()/applyCommandLine().
 */
struct AppConfig
{
    /// How the process runs: with the Qt Widgets GUI, or as a console/log-only server.
    enum class Mode { Gui, Headless };

    Mode mode = Mode::Gui;
    quint16 ocppPort = 9000;        ///< WebSocket port chargers connect to (ws://host:port/<chargePointId>)
    quint16 restPort = 8080;        ///< HTTP port for the REST API
    QString restBind = "127.0.0.1"; ///< Address the REST API listens on. The API has no authentication, so localhost by default.
    QString dbPath;                 ///< SQLite file. Empty = default in the app data folder.
    QString logLevel = "info";      ///< debug | info | warning | error
    QString logFile;                ///< Empty = log to console
    QString defaultIdTag = "SLEEK"; ///< Pre-filled idTag for RemoteStartTransaction

    /// Path of the INI settings file (user config folder).
    static QString settingsPath();

    /// Default SQLite database location (user data folder).
    static QString defaultDbPath();

    /// Registers all supported command line options on the parser.
    static void addOptions(QCommandLineParser &parser);

    /// Builds the final config: defaults, then settings file, then command line.
    static AppConfig resolve(const QCommandLineParser &parser);

    /// Reads values from the settings file; missing keys keep their current value.
    void load(const QString &path = settingsPath());

    /// Writes persistent values (everything except mode) to the settings file.
    void save(const QString &path = settingsPath()) const;

private:
    /// Overrides fields with options that were explicitly given on the command line.
    void applyCommandLine(const QCommandLineParser &parser);
};
