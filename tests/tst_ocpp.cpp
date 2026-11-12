#include <QSignalSpy>
#include <QWebSocket>
#include <QtTest>

#include "core/ocpp/OcppMessage.h"
#include "core/ocpp/OcppServer.h"

/// Tests OCPP-J framing and the WebSocket server using a fake charge point (a QWebSocket client).
class TstOcpp : public QObject
{
    Q_OBJECT

    /// Connects a fake charge point to the server and waits until the server has seen it.
    std::unique_ptr<QWebSocket> connectCp(OcppServer &server, const QString &path)
    {
        auto ws = std::make_unique<QWebSocket>();
        QSignalSpy connected(&server, &OcppServer::chargePointConnected);
        QNetworkRequest req(QUrl(QString("ws://127.0.0.1:%1%2").arg(server.port()).arg(path)));
        req.setRawHeader("Sec-WebSocket-Protocol", "ocpp1.6");
        ws->open(req);
        if (!connected.wait(3000))
            return nullptr;
        // The server sees the connection slightly before the client completes its handshake.
        if (!QTest::qWaitFor([&] { return ws->state() == QAbstractSocket::ConnectedState; }, 3000))
            return nullptr;
        return ws;
    }

private slots:
    void parseCall()
    {
        auto m = OcppMessage::parse(R"([2,"abc","Heartbeat",{}])");
        QVERIFY(m);
        QCOMPARE(m->type, OcppMessage::Type::Call);
        QCOMPARE(m->uniqueId, QString("abc"));
        QCOMPARE(m->action, QString("Heartbeat"));
    }

    void parseErrorAndRoundTrip()
    {
        auto m = OcppMessage::parse(R"([4,"x","NotImplemented","nope",{}])");
        QVERIFY(m);
        QCOMPARE(m->errorCode, QString("NotImplemented"));
        QCOMPARE(m->toJson(), QString(R"([4,"x","NotImplemented","nope",{}])"));

        const auto call = OcppMessage::makeCall("1", "Reset", QJsonObject{{"type", "Soft"}});
        QCOMPARE(call.toJson(), QString(R"([2,"1","Reset",{"type":"Soft"}])"));
    }

    void parseRejectsGarbage()
    {
        QString err;
        QVERIFY(!OcppMessage::parse("not json", &err));
        QVERIFY(!OcppMessage::parse(R"({"a":1})", &err));
        QVERIFY(!OcppMessage::parse(R"([2,"id","Action"])", &err));
        QVERIFY(!OcppMessage::parse(R"([9,"id",{}])", &err));
    }

    void unhandledCallGetsNotImplemented()
    {
        OcppServer server;
        QVERIFY(server.listen(0, QHostAddress::LocalHost));
        auto cp = connectCp(server, "/ocpp/CP001");
        QVERIFY(cp);
        QCOMPARE(server.connectedIds(), QStringList{"CP001"});

        QSignalSpy reply(cp.get(), &QWebSocket::textMessageReceived);
        cp->sendTextMessage(R"([2,"u1","BootNotification",{}])");
        QVERIFY(reply.wait(3000));
        auto m = OcppMessage::parse(reply.first().first().toString());
        QVERIFY(m);
        QCOMPARE(m->type, OcppMessage::Type::CallError);
        QCOMPARE(m->uniqueId, QString("u1"));
        QCOMPARE(m->errorCode, QString("NotImplemented"));
    }

    void outgoingCallMatchesResult()
    {
        OcppServer server;
        QVERIFY(server.listen(0, QHostAddress::LocalHost));
        auto cp = connectCp(server, "/CP002");
        QVERIFY(cp);

        // The fake charge point answers every Call with an empty CallResult.
        connect(cp.get(), &QWebSocket::textMessageReceived, cp.get(), [&cp](const QString &t) {
            auto m = OcppMessage::parse(t);
            cp->sendTextMessage(OcppMessage::makeResult(m->uniqueId, {{"status", "Accepted"}}).toJson());
        });

        std::optional<CallOutcome> outcome;
        server.session("CP002")->sendCall("Reset", {{"type", "Soft"}},
                                          [&](const CallOutcome &o) { outcome = o; });
        QTRY_VERIFY_WITH_TIMEOUT(outcome.has_value(), 3000);
        QVERIFY(outcome->ok());
        QCOMPARE(outcome->payload["status"].toString(), QString("Accepted"));
    }

    void callTimesOutAndDisconnectFailsPending()
    {
        OcppServer server;
        QVERIFY(server.listen(0, QHostAddress::LocalHost));
        auto cp = connectCp(server, "/CP003");
        QVERIFY(cp);

        ChargePointSession *s = server.session("CP003");
        s->setCallTimeoutMs(200);
        std::optional<CallOutcome> timedOut;
        s->sendCall("Reset", {}, [&](const CallOutcome &o) { timedOut = o; });
        QTRY_VERIFY_WITH_TIMEOUT(timedOut.has_value(), 3000);
        QCOMPARE(timedOut->status, CallOutcome::Status::Timeout);

        s->setCallTimeoutMs(30000);
        std::optional<CallOutcome> dropped;
        s->sendCall("Reset", {}, [&](const CallOutcome &o) { dropped = o; });
        QSignalSpy gone(&server, &OcppServer::chargePointDisconnected);
        cp->close();
        QTRY_VERIFY_WITH_TIMEOUT(dropped.has_value(), 3000);
        QCOMPARE(dropped->status, CallOutcome::Status::Disconnected);
        QCOMPARE(gone.count(), 1);
    }

    void reconnectReplacesSession()
    {
        OcppServer server;
        QVERIFY(server.listen(0, QHostAddress::LocalHost));
        auto first = connectCp(server, "/CP004");
        QVERIFY(first);
        auto second = connectCp(server, "/CP004");
        QVERIFY(second);
        QCOMPARE(server.connectedIds().size(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(first->state() == QAbstractSocket::UnconnectedState, 3000);
        QVERIFY(server.session("CP004")->isConnected());
    }
};

QTEST_MAIN(TstOcpp)
#include "tst_ocpp.moc"
