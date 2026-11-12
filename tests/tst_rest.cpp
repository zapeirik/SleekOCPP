#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QWebSocket>
#include <QtTest>

#include "core/CentralSystem.h"
#include "core/RestApi.h"
#include "core/db/Database.h"
#include "core/ocpp/OcppMessage.h"
#include "core/ocpp/OcppServer.h"

/// Tests the REST API with a real HTTP client and a fake charge point.
class TstRest : public QObject
{
    Q_OBJECT

    struct Response
    {
        int status = 0;
        QJsonObject json;
    };

    Database db;
    OcppServer server;
    CentralSystem *cs = nullptr;
    RestApi *api = nullptr;
    QNetworkAccessManager http;
    std::unique_ptr<QWebSocket> cp;
    QList<OcppMessage> received;   ///< Calls received by the fake charge point
    bool answer = true;            ///< false = the fake charge point stays silent (timeout test)

    /// Performs an HTTP request and waits for the answer. An empty body means no body.
    Response request(const QString &method, const QString &path, const QByteArray &body = {})
    {
        QNetworkRequest req(QUrl(QString("http://127.0.0.1:%1%2").arg(api->port()).arg(path)));
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        QNetworkReply *reply = method == "GET" ? http.get(req) : http.post(req, body);
        QSignalSpy done(reply, &QNetworkReply::finished);
        Response r;
        if (!done.wait(5000))
            return r;
        r.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        r.json = QJsonDocument::fromJson(reply->readAll()).object();
        reply->deleteLater();
        return r;
    }

private slots:
    void init()
    {
        QVERIFY(db.open(":memory:"));
        QVERIFY(server.listen(0, QHostAddress::LocalHost));
        cs = new CentralSystem(&server, &db, this);
        api = new RestApi(cs, this);
        QVERIFY(api->listen(0));
        received.clear();
        answer = true;

        cp = std::make_unique<QWebSocket>();
        QSignalSpy connected(&server, &OcppServer::chargePointConnected);
        QNetworkRequest req(QUrl(QString("ws://127.0.0.1:%1/CP1").arg(server.port())));
        req.setRawHeader("Sec-WebSocket-Protocol", "ocpp1.6");
        connect(cp.get(), &QWebSocket::textMessageReceived, this, [this](const QString &t) {
            auto m = OcppMessage::parse(t);
            if (m && m->type == OcppMessage::Type::Call) {
                received << *m;
                if (answer)
                    cp->sendTextMessage(OcppMessage::makeResult(m->uniqueId, {{"status", "Accepted"}}).toJson());
            }
        });
        cp->open(req);
        QVERIFY(connected.wait(3000));
        QTRY_COMPARE(cp->state(), QAbstractSocket::ConnectedState);
    }

    void cleanup()
    {
        cp.reset();
        delete api;
        delete cs;
        api = nullptr;
        cs = nullptr;
        server.stop();
    }

    void listAndDetail()
    {
        db.saveConnectorStatus("CP1", 1, "Available", "NoError", "", QDateTime::currentDateTimeUtc());
        auto r = request("GET", "/api/charge_points");
        QCOMPARE(r.status, 200);
        const QJsonArray list = r.json["chargePoints"].toArray();
        QCOMPARE(list.size(), 1);
        QCOMPARE(list[0].toObject()["id"].toString(), QString("CP1"));
        QVERIFY(list[0].toObject()["connected"].toBool());
        QCOMPARE(list[0].toObject()["connectors"].toArray()[0].toObject()["status"].toString(), QString("Available"));

        r = request("GET", "/api/charge_points/CP1");
        QCOMPARE(r.status, 200);
        QVERIFY(!r.json.contains("lastTransaction"));

        QCOMPARE(request("GET", "/api/charge_points/NOPE").status, 404);
    }

    void messagesEndpointHonoursLimit()
    {
        for (int i = 0; i < 5; ++i)
            db.logMessage("CP1", true, "Call", "Heartbeat", QString::number(i), "[]");
        const auto r = request("GET", "/api/charge_points/CP1/messages?limit=3");
        QCOMPARE(r.json["messages"].toArray().size(), 3);
    }

    void transactionHistoryEndpoints()
    {
        db.chargePointConnected("CP2", "127.0.0.1");
        const QDateTime t = QDateTime::currentDateTimeUtc();
        const qint64 a = db.startTransaction("CP1", 1, "A", 100, t.addSecs(-100));
        db.stopTransaction(a, 600, t.addSecs(-50), "Local");
        db.startTransaction("CP2", 1, "B", 0, t.addSecs(-10));
        db.startTransaction("CP1", 2, "C", 5, t);

        auto r = request("GET", "/api/transactions");
        QCOMPARE(r.status, 200);
        QCOMPARE(r.json["transactions"].toArray().size(), 3);
        QCOMPARE(r.json["transactions"].toArray()[0].toObject()["idTag"].toString(), QString("C")); // newest first
        QCOMPARE(request("GET", "/api/transactions?limit=2").json["transactions"].toArray().size(), 2);

        r = request("GET", "/api/charge_points/CP1/transactions");
        const QJsonArray mine = r.json["transactions"].toArray();
        QCOMPARE(mine.size(), 2);
        QCOMPARE(mine[1].toObject()["energyWh"].toInt(), 500);
        QCOMPARE(mine[1].toObject()["chargePointId"].toString(), QString("CP1"));
        QVERIFY(mine[0].toObject()["energyWh"].isNull());
        QCOMPARE(request("GET", "/api/charge_points/NOPE/transactions").status, 404);
    }

    void messagePruningInTwoSteps()
    {
        db.logMessage("CP1", true, "Call", "Heartbeat", "1", "[]");
        db.logMessage("CP1", false, "CallResult", "", "1", "[]"); // gets action Heartbeat
        db.logMessage("CP1", true, "Call", "StartTransaction", "2", "[]");
        db.logMessage("CP2", true, "Call", "Heartbeat", "3", "[]");

        QCOMPARE(request("POST", "/api/messages/delete", "not json").status, 400);
        auto r = request("POST", "/api/messages/delete", R"({"chargePointId":"CP1","actions":["Heartbeat"],"types":["Call"]})");
        QCOMPARE(r.json["deleted"].toInt(), 1);
        QCOMPARE(db.recentMessages("CP1", 100).size(), 2); // hidden, not removed
        QCOMPARE(db.countDeletedMessages(), 1);

        QCOMPARE(request("POST", "/api/messages/delete", R"({"before":"2000-01-01T00:00:00Z"})").json["deleted"].toInt(), 0);
        QCOMPARE(request("POST", "/api/messages/delete", R"({"before":"2999-01-01T00:00:00Z","actions":["Heartbeat"]})").json["deleted"].toInt(), 2);

        QCOMPARE(request("POST", "/api/messages/purge", R"({"chargePointId":"CP2"})").json["removed"].toInt(), 1);
        QCOMPARE(db.countDeletedMessages(), 2); // CP1's still waiting
        QCOMPARE(request("POST", "/api/messages/purge").json["removed"].toInt(), 2);
        QCOMPARE(db.countDeletedMessages(), 0);
    }

    void remoteStartReturnsChargerAnswer()
    {
        auto r = request("POST", "/api/charge_points/CP1/remote_start", R"({"idTag":"TAG","connectorId":1})");
        QCOMPARE(r.status, 200);
        QCOMPARE(r.json["outcome"].toString(), QString("result"));
        QCOMPARE(r.json["status"].toString(), QString("Accepted"));
        QCOMPARE(received.last().action, QString("RemoteStartTransaction"));
        QCOMPARE(received.last().payload["idTag"].toString(), QString("TAG"));
    }

    void remoteStopUsesActiveTransaction()
    {
        QCOMPARE(request("POST", "/api/charge_points/CP1/remote_stop").status, 400); // nothing running

        const qint64 tx = db.startTransaction("CP1", 1, "TAG", 0, QDateTime::currentDateTimeUtc());
        const auto r = request("POST", "/api/charge_points/CP1/remote_stop");
        QCOMPARE(r.status, 200);
        QCOMPARE(received.last().payload["transactionId"].toInt(), int(tx));
    }

    void validationAndErrors()
    {
        QCOMPARE(request("POST", "/api/charge_points/CP1/remote_start", "{}").status, 400);
        QCOMPARE(request("POST", "/api/charge_points/CP1/reset", R"({"type":"Medium"})").status, 400);
        QCOMPARE(request("POST", "/api/charge_points/CP1/reset", "not json").status, 400);
        QVERIFY(received.isEmpty());

        const auto r = request("POST", "/api/charge_points/OFFLINE/reset", R"({"type":"Soft"})");
        QCOMPARE(r.status, 409);
        QCOMPARE(r.json["outcome"].toString(), QString("disconnected"));
    }

    void timeoutGives504()
    {
        answer = false;
        cs->server()->session("CP1")->setCallTimeoutMs(200);
        const auto r = request("POST", "/api/charge_points/CP1/reset", R"({"type":"Soft"})");
        QCOMPARE(r.status, 504);
        QCOMPARE(r.json["outcome"].toString(), QString("timeout"));
    }

    void genericSend()
    {
        const auto r = request("POST", "/api/charge_points/CP1/send",
                               R"({"action":"GetLocalListVersion","payload":{}})");
        QCOMPARE(r.status, 200);
        QCOMPARE(received.last().action, QString("GetLocalListVersion"));
    }
};

QTEST_MAIN(TstRest)
#include "tst_rest.moc"
