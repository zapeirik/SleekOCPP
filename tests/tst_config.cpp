#include <QTemporaryDir>
#include <QtTest>

#include "core/AppConfig.h"
#include "core/Logger.h"

/// Tests settings file round trip, command line parsing and log level parsing.
class TstConfig : public QObject
{
    Q_OBJECT
private slots:
    void saveAndLoadRoundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("a/test.ini");
        AppConfig a;
        a.ocppPort = 9100;
        a.defaultIdTag = "TAG1";
        a.restBind = "0.0.0.0";
        a.save(path);

        AppConfig b;
        b.load(path);
        QCOMPARE(b.ocppPort, quint16(9100));
        QCOMPARE(b.defaultIdTag, QString("TAG1"));
        QCOMPARE(b.restBind, QString("0.0.0.0"));
    }

    void restBindDefaultsToLocalhostAndCanBeOverridden()
    {
        QCOMPARE(AppConfig().restBind, QString("127.0.0.1"));
        QCommandLineParser parser;
        AppConfig::addOptions(parser);
        QVERIFY(parser.parse({"app", "--rest-bind", "0.0.0.0"}));
        QCOMPARE(AppConfig::resolve(parser).restBind, QString("0.0.0.0"));
    }

    void commandLineOptions()
    {
        QCommandLineParser parser;
        AppConfig::addOptions(parser);
        QVERIFY(parser.parse({"app", "--headless", "--port", "9200", "--log-level", "debug"}));
        QVERIFY(parser.isSet("headless"));
        QCOMPARE(parser.value("port"), QString("9200"));
    }

    void levelParsing()
    {
        QCOMPARE(Logger::levelFromString("DEBUG"), Logger::Level::Debug);
        QCOMPARE(Logger::levelFromString("warn"), Logger::Level::Warning);
        QCOMPARE(Logger::levelFromString("nonsense"), Logger::Level::Info);
    }
};

QTEST_MAIN(TstConfig)
#include "tst_config.moc"
