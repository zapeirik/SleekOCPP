#include <QSignalSpy>
#include <QWebSocket>
#include <QtTest>

#include "core/CentralSystem.h"
#include "core/ChargePointModel.h"
#include "core/db/Database.h"
#include "core/ocpp/OcppMessage.h"
#include "core/ocpp/OcppServer.h"

/// Tests that ChargePointModel mirrors the database and updates when charge points connect and report status.
class TstModel : public QObject
{
    Q_OBJECT

    static QString display(const ChargePointModel &m, int row, int col)
    {
        return m.data(m.index(row, col)).toString();
    }

private slots:
    void loadsExistingAndFollowsLiveChanges()
    {
        Database db;
        QVERIFY(db.open(":memory:"));
        db.chargePointConnected("B-existing", "10.0.0.1");
        db.chargePointDisconnected("B-existing");

        OcppServer server;
        QVERIFY(server.listen(0, QHostAddress::LocalHost));
        CentralSystem cs(&server, &db);
        ChargePointModel model(&cs);

        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(display(model, 0, ChargePointModel::IdColumn), QString("B-existing"));
        QCOMPARE(display(model, 0, ChargePointModel::ConnectionColumn), QString("Disconnected"));
        QCOMPARE(display(model, 0, ChargePointModel::LastHeartbeatColumn), QString("-"));

        // A new charge point connects: it must appear in sorted position (before "B-existing").
        QWebSocket cp;
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        QNetworkRequest req(QUrl(QString("ws://127.0.0.1:%1/A-new").arg(server.port())));
        req.setRawHeader("Sec-WebSocket-Protocol", "ocpp1.6");
        cp.open(req);
        QTRY_COMPARE_WITH_TIMEOUT(inserted.count(), 1, 3000);
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(display(model, 0, ChargePointModel::IdColumn), QString("A-new"));
        QCOMPARE(model.data(model.index(0, 0), ChargePointModel::ConnectedRole).toBool(), true);

        // Status notifications show up in the status column.
        QTRY_COMPARE_WITH_TIMEOUT(cp.state(), QAbstractSocket::ConnectedState, 3000);
        cp.sendTextMessage(OcppMessage::makeCall("1", "StatusNotification",
                           {{"connectorId", 1}, {"errorCode", "NoError"}, {"status", "Charging"}}).toJson());
        QTRY_COMPARE_WITH_TIMEOUT(display(model, 0, ChargePointModel::StatusColumn), QString("Charging"), 3000);

        cp.sendTextMessage(OcppMessage::makeCall("2", "StatusNotification",
                           {{"connectorId", 2}, {"errorCode", "NoError"}, {"status", "Available"}}).toJson());
        QTRY_COMPARE_WITH_TIMEOUT(display(model, 0, ChargePointModel::StatusColumn),
                                  QString("1: Charging, 2: Available"), 3000);

        // Disconnect is reflected too.
        cp.close();
        QTRY_COMPARE_WITH_TIMEOUT(model.data(model.index(0, 0), ChargePointModel::ConnectedRole).toBool(), false, 3000);
        QVERIFY(model.record(0).lastDisconnected.isValid());
    }
};

QTEST_MAIN(TstModel)
#include "tst_model.moc"
