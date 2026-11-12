#include "Logger.h"

#include <QDateTime>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>
#include <atomic>

namespace {

std::atomic<int> g_level{static_cast<int>(Logger::Level::Info)};
QMutex g_mutex;          // Messages can arrive from several threads; keep lines intact.
QFile g_file;            // Only open when logging to a file.

Logger::Level levelOf(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return Logger::Level::Debug;
    case QtInfoMsg: return Logger::Level::Info;
    case QtWarningMsg: return Logger::Level::Warning;
    default: return Logger::Level::Error; // Critical and Fatal
    }
}

const char *labelOf(Logger::Level level)
{
    switch (level) {
    case Logger::Level::Debug: return "DEBUG";
    case Logger::Level::Info: return "INFO ";
    case Logger::Level::Warning: return "WARN ";
    default: return "ERROR";
    }
}

/// Qt message handler: filters by level, formats one line and writes it to the file or stderr.
void messageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    const Logger::Level level = levelOf(type);
    if (static_cast<int>(level) < g_level.load() && type != QtFatalMsg)
        return;

    const QString line = QStringLiteral("%1 [%2] %3\n")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                                  QLatin1String(labelOf(level)), msg);

    QMutexLocker lock(&g_mutex);
    if (g_file.isOpen()) {
        g_file.write(line.toUtf8());
        g_file.flush();
    } else {
        QTextStream(stderr) << line;
    }

    if (type == QtFatalMsg)
        abort();
}

} // namespace

Logger::Level Logger::levelFromString(const QString &text)
{
    const QString t = text.trimmed().toLower();
    if (t == QLatin1String("debug")) return Level::Debug;
    if (t == QLatin1String("warning") || t == QLatin1String("warn")) return Level::Warning;
    if (t == QLatin1String("error")) return Level::Error;
    return Level::Info;
}

bool Logger::init(Level level, const QString &filePath)
{
    setLevel(level);
    bool ok = true;
    {
        QMutexLocker lock(&g_mutex);
        if (g_file.isOpen())
            g_file.close();
        if (!filePath.isEmpty()) {
            g_file.setFileName(filePath);
            ok = g_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
        }
    }
    qInstallMessageHandler(messageHandler);
    if (!ok)
        qWarning().noquote() << "Could not open log file" << filePath << "- logging to console";
    return ok;
}

void Logger::setLevel(Level level)
{
    g_level.store(static_cast<int>(level));
}
