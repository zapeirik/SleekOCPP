#include <QSignalSpy>
#include <QWebSocket>
#include <QtTest>

#include "core/CentralSystem.h"
#include "core/ChargePointCommands.h"
#include "core/db/Database.h"
#include "core/ocpp/OcppMessage.h"
#include "core/ocpp/OcppServer.h"

/// Tests that commands put the right OCPP payload on the wire and handle the answers.
/// A fake charge point records every Call it receives and answers with `nextReply`.
class TstCommands : public QObject
{
    Q_OBJECT

    Database db;
    OcppServer server;
    CentralSystem *cs = nullptr;
    std::unique_ptr<QWebSocket> cp;
    QList<OcppMessage> received;     ///< Calls received by the fake charge point
    QJsonObject nextReply;           ///< Payload the fake charge point answers with

    std::optional<CallOutcome> outcome;
    ChargePointSession::CallCallback store()
    {
        outcome.reset();
        return [this](const CallOutcome &o) { outcome = o; };
    }

private slots:
    void init()
    {
        QVERIFY(db.open(":memory:"));
        QVERIFY(server.listen(0, QHostAddress::LocalHost));
        cs = new CentralSystem(&server, &db, this);
        received.clear();
        nextReply = {{"status", "Accepted"}};

        cp = std::make_unique<QWebSocket>();
        QSignalSpy connected(&server, &OcppServer::chargePointConnected);
        QNetworkRequest req(QUrl(QString("ws://127.0.0.1:%1/CP1").arg(server.port())));
        req.setRawHeader("Sec-WebSocket-Protocol", "ocpp1.6");
        connect(cp.get(), &QWebSocket::textMessageReceived, this, [this](const QString &t) {
            auto m = OcppMessage::parse(t);
            if (m && m->type == OcppMessage::Type::Call) {
                received << *m;
                cp->sendTextMessage(OcppMessage::makeResult(m->uniqueId, nextReply).toJson());
            }
        });
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
    }

    void remoteStartAndStop()
    {
        auto *c = cs->commands();
        c->remoteStartTransaction("CP1", "TAG", 1, {}, store());
        QTRY_VERIFY_WITH_TIMEOUT(outcome.has_value(), 3000);
        QCOMPARE(received.last().action, QString("RemoteStartTransaction"));
        QCOMPARE(received.last().payload["idTag"].toString(), QString("TAG"));
        QCOMPARE(received.last().payload["connectorId"].toInt(), 1);
        QCOMPARE(outcome->summary(), QString("Accepted"));

        c->remoteStartTransaction("CP1", "TAG", std::nullopt, {}, store());
        QTRY_VERIFY_WITH_TIMEOUT(outcome.has_value(), 3000);
        QVERIFY(!received.last().payload.contains("connectorId"));

        c->remoteStopTransaction("CP1", 42, store());
        QTRY_VERIFY_WITH_TIMEOUT(outcome.has_value(), 3000);
        QCOMPARE(received.last().action, QString("RemoteStopTransaction"));
        QCOMPARE(received.last().payload["transactionId"].toInt(), 42);
    }

    void chargingProfileCommands()
    {
        auto *c = cs->commands();
        const QJsonObject profile{{"chargingProfileId", 1}, {"stackLevel", 0}};
        c->setChargingProfile("CP1", 1, profile, store());
        QTRY_VERIFY_WITH_TIMEOUT(outcome.has_value(), 3000);
        QCOMPARE(received.last().action, QString("SetChargingProfile"));
        QCOMPARE(received.last().payload["csChargingProfiles"].toObject(), profile);

        c->clearChargingProfile("CP1", std::nullopt, 1, "TxProfile", std::nullopt, store());
        QTRY_VERIFY_WITH_TIMEOUT(outcome.has_value(), 3000);
        QCOMPARE(received.last().payload["chargingProfilePurpose"].toString(), QString("TxProfile"));
        QVERIFY(!received.last().payload.contains("id"));

        c->getCompositeSchedule("CP1", 1, 3600, "A", store());
        QTRY_VERIFY_WITH_TIMEOUT(outcome.has_value(), 3000);
        QCOMPARE(received.last().payload["duration"].toInt(), 3600);
        QCOMPARE(received.last().payload["chargingRateUnit"].toString(), QString("A"));
    }

    void getConfigurationFillsCache()
    {
        nextReply = {{"configurationKey", QJsonArray{
                          QJsonObject{{"key", "HeartbeatInterval"}, {"readonly", false}, {"value", "60"}},
                          QJsonObject{{"key", "NumberOfConnectors"}, {"readonly", true}, {"value", "2"}}}}};
        QSignalSpy changed(cs->commands(), &ChargePointCommands::configurationChanged);
        cs->commands()->getConfiguration("CP1", {}, store());
        QTRY_VERIFY_WITH_TIMEOUT(outcome.has_value(), 3000);
        QVERIFY(!received.last().payload.contains("key")); // empty list = all keys
        QCOMPARE(changed.count(), 1);

        const auto cfg = db.configuration("CP1");
        QCOMPARE(cfg.size(), 2);
        QCOMPARE(cfg[0].key, QString("HeartbeatInterval"));
        QVERIFY(!cfg[0].readonly);
        QVERIFY(cfg[1].readonly);
    }

    void changeConfigurationUpdatesCacheOnlyWhenAccepted()
    {
        auto *c = cs->commands();
        nextReply = {{"status", "Rejected"}};
        c->changeConfiguration("CP1", "HeartbeatInterval", "30", store());
        QTRY_VERIFY_WITH_TIMEOUT(outcome.has_value(), 3000);
        QCOMPARE(received.last().payload["value"].toString(), QString("30"));
        QVERIFY(db.configuration("CP1").isEmpty());

        nextReply = {{"status", "RebootRequired"}};
        c->changeConfiguration("CP1", "HeartbeatInterval", "30", store());
        QTRY_VERIFY_WITH_TIMEOUT(outcome.has_value(), 3000);
        QCOMPARE(db.configuration("CP1").value(0).value, QString("30"));
    }

    void notConnectedReportsDisconnected()
    {
        cs->commands()->reset("UNKNOWN", "Soft", store());
        QVERIFY(outcome.has_value()); // reported immediately
        QCOMPARE(outcome->status, CallOutcome::Status::Disconnected);
    }
};

QTEST_MAIN(TstCommands)
#include "tst_commands.moc"
