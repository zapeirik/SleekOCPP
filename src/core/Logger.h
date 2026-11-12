#pragma once

#include <QString>

/**
 * Application-wide logging.
 *
 * Installs a Qt message handler so that all qDebug()/qInfo()/qWarning()/qCritical()
 * calls are filtered by level and written to the console (stderr) or to a file.
 * Use the normal Qt macros anywhere in the code; no Logger instance is needed.
 */
class Logger
{
public:
    enum class Level { Debug = 0, Info = 1, Warning = 2, Error = 3 };

    /// Parses "debug", "info", "warning" or "error" (case-insensitive). Falls back to Info.
    static Level levelFromString(const QString &text);

    /**
     * Installs the message handler. Call once, early in main().
     * @param level    Messages below this level are dropped.
     * @param filePath If non-empty, log to this file (appended) instead of the console.
     * @return false if the file could not be opened (logging then falls back to the console).
     */
    static bool init(Level level, const QString &filePath = QString());

    /// Changes the minimum level at runtime (e.g. from a GUI setting).
    static void setLevel(Level level);
};
