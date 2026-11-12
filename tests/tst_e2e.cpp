#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QWebSocket>
#include <QtTest>

#include "core/ocpp/OcppMessage.h"

/// End-to-end test of the real `sleekocpp --headless` binary: a fake charger connects over
/// WebSocket, the REST API is used to inspect and command it, and the database survives a restart.
class TstEndToEnd : public QObject
{
    Q_OBJECT

    static int freePort()
    {
        QTcpServer s;
        s.listen(QHostAddress::LocalHost, 0);
        return s.serverPort();
    }

    /// GET/POST against the REST API of the running process.
    QJsonObject rest(int port, const QString &method, const QString &path, const QByteArray &body = {})
    {
        QNetworkAccessManager http;
        QNetworkRequest req(QUrl(QString("http://127.0.0.1:%1%2").arg(port).arg(path)));
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        QNetworkReply *reply = method == "GET" ? http.get(req) : http.post(req, body);
        QSignalSpy done(reply, &QNetworkReply::finished);
        if (!done.wait(5000))
            return {};
        const auto json = QJsonDocument::fromJson(reply->readAll()).object();
        reply->deleteLater();
        return json;
    }

    std::unique_ptr<QProcess> startServer(const QString &db, int ocppPort, int restPort)
    {
        auto p = std::make_unique<QProcess>();
        p->setProcessChannelMode(QProcess::ForwardedErrorChannel);
        p->start(SLEEKOCPP_BINARY, {"--headless", "--db", db, "--port", QString::number(ocppPort),
                                     "--rest-port", QString::number(restPort), "--log-level", "warning"});
        if (!p->waitForStarted(5000))
            return nullptr;
        // Ready when the REST port accepts connections.
        const bool ready = QTest::qWaitFor([&] {
            QTcpSocket s;
            s.connectToHost("127.0.0.1", restPort);
            return s.waitForConnected(200);
        }, 10000);
        return ready ? std::move(p) : nullptr;
    }

private slots:
    void chargerLifecycleAndRestart()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString db = dir.filePath("e2e.db");
        const int ocppPort = freePort();
        const int restPort = freePort();

        {
            auto server = startServer(db, ocppPort, restPort);
            QVERIFY(server);

            QWebSocket cp;
            QStringList received;
            connect(&cp, &QWebSocket::textMessageReceived, this, [&](const QString &t) {
                const auto m = OcppMessage::parse(t);
                if (m && m->type == OcppMessage::Type::Call) {
                    received << m->action;
                    cp.sendTextMessage(OcppMessage::makeResult(m->uniqueId, {{"status", "Accepted"}}).toJson());
                }
            });
            QNetworkRequest req(QUrl(QString("ws://127.0.0.1:%1/E2E-1").arg(ocppPort)));
            req.setRawHeader("Sec-WebSocket-Protocol", "ocpp1.6");
            cp.open(req);
            QTRY_COMPARE_WITH_TIMEOUT(cp.state(), QAbstractSocket::ConnectedState, 5000);
            cp.sendTextMessage(OcppMessage::makeCall("1", "BootNotification",
                {{"chargePointVendor", "ACME"}, {"chargePointModel", "X1"}}).toJson());

            QTRY_VERIFY_WITH_TIMEOUT(
                rest(restPort, "GET", "/api/charge_points")["chargePoints"].toArray().size() == 1, 5000);
            auto detail = rest(restPort, "GET", "/api/charge_points/E2E-1");
            QVERIFY(detail["connected"].toBool());
            QCOMPARE(detail["vendor"].toString(), QString("ACME"));

            const auto answer = rest(restPort, "POST", "/api/charge_points/E2E-1/remote_start", R"({"idTag":"TAG","connectorId":1})");
            QCOMPARE(answer["status"].toString(), QString("Accepted"));
            QCOMPARE(received, QStringList{"RemoteStartTransaction"});

            cp.close();
            QTRY_VERIFY_WITH_TIMEOUT(!rest(restPort, "GET", "/api/charge_points/E2E-1")["connected"].toBool(), 5000);

            server->terminate();
            QVERIFY(server->waitForFinished(5000));
        }

        // New process, same database: the charger is remembered, and shown as disconnected.
        auto server = startServer(db, ocppPort, restPort);
        QVERIFY(server);
        const auto detail = rest(restPort, "GET", "/api/charge_points/E2E-1");
        QCOMPARE(detail["id"].toString(), QString("E2E-1"));
        QVERIFY(!detail["connected"].toBool());
        QVERIFY(!rest(restPort, "GET", "/api/charge_points/E2E-1/messages")["messages"].toArray().isEmpty());
        server->terminate();
        QVERIFY(server->waitForFinished(5000));
    }

    void secondInstanceOnSamePortFailsCleanly()
    {
        QTemporaryDir dir;
        const int ocppPort = freePort();
        auto first = startServer(dir.filePath("a.db"), ocppPort, freePort());
        QVERIFY(first);

        QProcess second;
        second.start(SLEEKOCPP_BINARY, {"--headless", "--db", dir.filePath("b.db"), "--port", QString::number(ocppPort),
                                         "--rest-port", QString::number(freePort())});
        QVERIFY(second.waitForFinished(10000));
        QCOMPARE(second.exitCode(), 1);

        first->terminate();
        QVERIFY(first->waitForFinished(5000));
    }
};

QTEST_MAIN(TstEndToEnd)
#include "tst_e2e.moc"
