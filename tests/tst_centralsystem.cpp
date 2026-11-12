#include <QSignalSpy>
#include <QWebSocket>
#include <QtTest>

#include "core/CentralSystem.h"
#include "core/db/Database.h"
#include "core/ocpp/OcppMessage.h"
#include "core/ocpp/OcppServer.h"

/// End-to-end tests: a fake charge point talks to CentralSystem, and we check replies and the database.
class TstCentralSystem : public QObject
{
    Q_OBJECT

    Database db;
    OcppServer server;
    CentralSystem *cs = nullptr;
    std::unique_ptr<QWebSocket> cp;
    QStringList inbox;   ///< Frames received by the fake charge point

private slots:
    void init()
    {
        QVERIFY(db.open(":memory:")); // a fresh in-memory database for every test
        QVERIFY(server.listen(0, QHostAddress::LocalHost));
        cs = new CentralSystem(&server, &db, this);

        cp = std::make_unique<QWebSocket>();
        QSignalSpy connected(&server, &OcppServer::chargePointConnected);
        QNetworkRequest req(QUrl(QString("ws://127.0.0.1:%1/CP1").arg(server.port())));
        req.setRawHeader("Sec-WebSocket-Protocol", "ocpp1.6");
        connect(cp.get(), &QWebSocket::textMessageReceived, this, [this](const QString &t) { inbox << t; });
        cp->open(req);
        QVERIFY(connected.wait(3000));
        QTRY_COMPARE(cp->state(), QAbstractSocket::ConnectedState);
    }

    void cleanup()
    {
        cp.reset();
        delete cs;
        cs = nullptr;
        server.stop();
        inbox.clear();
    }

    /// Sends a Call from the fake charge point and returns the parsed answer.
    OcppMessage call(const QString &action, const QJsonObject &payload)
    {
        inbox.clear();
        cp->sendTextMessage(OcppMessage::makeCall("id1", action, payload).toJson());
        [this] { QTRY_VERIFY_WITH_TIMEOUT(!inbox.isEmpty(), 3000); }();
        return OcppMessage::parse(inbox.first()).value_or(OcppMessage());
    }

    void connectionIsRecorded()
    {
        auto r = db.chargePoint("CP1");
        QVERIFY(r);
        QVERIFY(r->connected);
        QVERIFY(r->lastConnected.isValid());

        QSignalSpy gone(&server, &OcppServer::chargePointDisconnected);
        cp->close();
        QVERIFY(gone.wait(3000));
        r = db.chargePoint("CP1");
        QVERIFY(!r->connected);
        QVERIFY(r->lastDisconnected.isValid());
    }

    void bootAndHeartbeat()
    {
        auto m = call("BootNotification", {{"chargePointVendor", "Acme"}, {"chargePointModel", "X1"}});
        QCOMPARE(m.type, OcppMessage::Type::CallResult);
        QCOMPARE(m.payload["status"].toString(), QString("Accepted"));
        QVERIFY(m.payload["interval"].toInt() > 0);
        QCOMPARE(db.chargePoint("CP1")->vendor, QString("Acme"));

        m = call("Heartbeat", {});
        QVERIFY(m.payload.contains("currentTime"));
        QVERIFY(db.chargePoint("CP1")->lastHeartbeat.isValid());
    }

    void statusNotificationUpdatesConnector()
    {
        auto m = call("StatusNotification", {{"connectorId", 1}, {"errorCode", "NoError"}, {"status", "Charging"}});
        QCOMPARE(m.type, OcppMessage::Type::CallResult);
        const auto list = db.connectorStatuses("CP1");
        QCOMPARE(list.size(), 1);
        QCOMPARE(list[0].status, QString("Charging"));
    }

    void missingFieldsGiveFormationViolation()
    {
        auto m = call("StatusNotification", {{"connectorId", 1}});
        QCOMPARE(m.type, OcppMessage::Type::CallError);
        QCOMPARE(m.errorCode, QString("FormationViolation"));
    }

    void unknownActionIsNotImplemented()
    {
        QCOMPARE(call("GetDiagnostics", {}).errorCode, QString("NotImplemented"));
    }

    void fullTransaction()
    {
        auto m = call("StartTransaction", {{"connectorId", 1}, {"idTag", "TAG"}, {"meterStart", 1000},
                                           {"timestamp", "2026-01-01T10:00:00Z"}});
        const int txId = m.payload["transactionId"].toInt();
        QVERIFY(txId > 0);
        QVERIFY(db.lastTransaction("CP1")->isActive());

        call("MeterValues", {{"connectorId", 1}, {"transactionId", txId},
                             {"meterValue", QJsonArray{QJsonObject{
                                 {"timestamp", "2026-01-01T10:05:00Z"},
                                 {"sampledValue", QJsonArray{QJsonObject{{"value", "1500"}, {"unit", "Wh"}}}}}}}});
        const auto mv = db.recentMeterValues("CP1");
        QCOMPARE(mv.size(), 1);
        QCOMPARE(mv[0].value, QString("1500"));
        QCOMPARE(mv[0].measurand, QString("Energy.Active.Import.Register"));
        QCOMPARE(mv[0].transactionId.value_or(-1), qint64(txId));
        QVERIFY(db.chargePoint("CP1")->lastMeterValue.isValid());

        m = call("StopTransaction", {{"transactionId", txId}, {"meterStop", 2000},
                                     {"timestamp", "2026-01-01T11:00:00Z"}, {"reason", "Local"}});
        QCOMPARE(m.payload["idTagInfo"].toObject()["status"].toString(), QString("Accepted"));
        const auto tx = db.lastTransaction("CP1");
        QVERIFY(!tx->isActive());
        QCOMPARE(tx->meterStop.value_or(0), qint64(2000));
        QCOMPARE(tx->stopReason, QString("Local"));
    }

    void stopTransactionDataKeepsConnector()
    {
        auto m = call("StartTransaction", {{"connectorId", 2}, {"idTag", "TAG"}, {"meterStart", 0},
                                           {"timestamp", "2026-01-01T10:00:00Z"}});
        const int txId = m.payload["transactionId"].toInt();
        call("StopTransaction", {{"transactionId", txId}, {"meterStop", 500}, {"timestamp", "2026-01-01T11:00:00Z"},
                                 {"transactionData", QJsonArray{QJsonObject{
                                     {"timestamp", "2026-01-01T11:00:00Z"},
                                     {"sampledValue", QJsonArray{QJsonObject{{"value", "500"}}}}}}}});
        const auto mv = db.recentMeterValues("CP1");
        QCOMPARE(mv.size(), 1);
        QCOMPARE(mv[0].connectorId, 2);
        QCOMPARE(mv[0].transactionId.value_or(-1), qint64(txId));
    }

    void stopOfUnknownTransactionIsAccepted()
    {
        const auto m = call("StopTransaction", {{"transactionId", 9999}, {"meterStop", 1},
                                                {"timestamp", "2026-01-01T11:00:00Z"}});
        QCOMPARE(m.type, OcppMessage::Type::CallResult);
        QVERIFY(!db.lastTransaction("CP1").has_value());
    }

    void messagesAreLoggedWithActionForReplies()
    {
        call("Heartbeat", {});
        QTRY_COMPARE_WITH_TIMEOUT(db.recentMessages("CP1", 10).size(), 2, 3000);
        const auto msgs = db.recentMessages("CP1", 10); // newest first
        QCOMPARE(msgs[0].type, QString("CallResult"));
        QCOMPARE(msgs[0].action, QString("Heartbeat"));
        QVERIFY(!msgs[0].incoming);
        QCOMPARE(msgs[1].type, QString("Call"));
        QVERIFY(msgs[1].incoming);
    }
};

QTEST_MAIN(TstCentralSystem)
#include "tst_centralsystem.moc"
