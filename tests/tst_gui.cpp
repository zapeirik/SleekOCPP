#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QMenu>
#include <QTabWidget>
#include <QToolButton>
#include <QTableWidget>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTableView>
#include <QWebSocket>
#include <QtTest>

#include "core/CentralSystem.h"
#include "core/db/Database.h"
#include "core/ocpp/OcppMessage.h"
#include "core/ocpp/OcppServer.h"
#include "gui/ChargePointConfigurationWidget.h"
#include "gui/ChargePointControlWidget.h"
#include "gui/ChargePointDetailWidget.h"
#include "gui/ChargePointListWidget.h"
#include "gui/MainWindow.h"
#include "gui/TransactionsWidget.h"

/// Runs the main window offscreen with fake charge points and checks the list behaviour.
/// Set SLEEKOCPP_SCREENSHOT=/path/file.png to save an image of the window.
class TstGui : public QObject
{
    Q_OBJECT

    std::unique_ptr<QWebSocket> connectCp(OcppServer &server, const QString &id)
    {
        auto ws = std::make_unique<QWebSocket>();
        QNetworkRequest req(QUrl(QString("ws://127.0.0.1:%1/%2").arg(server.port()).arg(id)));
        req.setRawHeader("Sec-WebSocket-Protocol", "ocpp1.6");
        ws->open(req);
        QTest::qWaitFor([&] { return ws->state() == QAbstractSocket::ConnectedState; }, 3000);
        return ws;
    }

    static void status(QWebSocket &ws, int connector, const QString &status)
    {
        ws.sendTextMessage(OcppMessage::makeCall("s" + QString::number(connector), "StatusNotification",
                           {{"connectorId", connector}, {"errorCode", "NoError"}, {"status", status}}).toJson());
    }

private slots:
    void messageFilterHidesActions()
    {
        Database db;
        QVERIFY(db.open(":memory:"));
        db.chargePointConnected("CP-F", "10.0.0.3");
        db.logMessage("CP-F", true, "Call", "Heartbeat", "1", "[2,\"1\",\"Heartbeat\",{}]");
        db.logMessage("CP-F", true, "Call", "StartTransaction", "2", "[2,\"2\",\"StartTransaction\",{}]");
        db.logMessage("CP-F", false, "CallResult", "", "2", "[3,\"2\",{}]");
        db.logMessage("CP-F", true, "Call", "StopTransaction", "3", "[2,\"3\",\"StopTransaction\",{}]");
        db.logMessage("CP-F", true, "Call", "Heartbeat", "4", "[2,\"4\",\"Heartbeat\",{}]");

        QCOMPARE(db.messageActions("CP-F"), QStringList({"Heartbeat", "StartTransaction", "StopTransaction"}));
        QCOMPARE(db.recentMessages("CP-F", 100, {"Heartbeat"}).size(), 3);

        OcppServer server;
        QVERIFY(server.listen(0, QHostAddress::LocalHost));
        CentralSystem cs(&server, &db);
        AppConfig config;
        MainWindow window(config, &cs);
        window.show();
        window.detail()->setChargePoint("CP-F");
        auto *table = window.detail()->findChild<QTableWidget *>("messagesTable");
        QCOMPARE(table->rowCount(), 5);

        auto *button = window.detail()->findChild<QToolButton *>("messageFilterButton");
        QVERIFY(button);
        button->menu()->aboutToShow();
        for (QAction *a : button->menu()->actions())
            if (a->text() == "Heartbeat")
                a->setChecked(false);
        QCOMPARE(table->rowCount(), 3);
        QVERIFY(button->text().contains("1 hidden"));

        // Actions the charger never sent are still selectable.
        button->menu()->aboutToShow();
        bool found = false;
        for (QAction *a : button->menu()->actions())
            found |= a->text() == "SetChargingProfile";
        QVERIFY(found);
    }

    void transactionTabsShowHistory()
    {
        Database db;
        QVERIFY(db.open(":memory:"));
        db.chargePointConnected("CP-A", "10.0.0.1");
        db.chargePointConnected("CP-B", "10.0.0.2");
        const QDateTime t = QDateTime::currentDateTimeUtc();
        const qint64 a = db.startTransaction("CP-A", 1, "TAG1", 1000, t.addSecs(-3700));
        db.stopTransaction(a, 8000, t.addSecs(-100), "EVDisconnected");
        db.startTransaction("CP-B", 1, "TAG2", 0, t.addSecs(-5));
        db.startTransaction("CP-A", 1, "TAG3", 8000, t.addSecs(-1));

        OcppServer server;
        QVERIFY(server.listen(0, QHostAddress::LocalHost));
        CentralSystem cs(&server, &db);
        AppConfig config;
        MainWindow window(config, &cs);
        window.show();

        auto *all = window.findChild<QTabWidget *>("mainTabs")->widget(1)->findChild<QTableWidget *>("transactionsTable");
        QVERIFY(all);
        QCOMPARE(all->rowCount(), 3);
        QCOMPARE(all->item(0, 3)->text(), QString("TAG3")); // newest first
        QVERIFY(!all->isColumnHidden(1));

        auto *perCharger = window.detail()->findChild<TransactionsWidget *>();
        QVERIFY(perCharger);
        perCharger->setChargePoint("CP-A");
        auto *mine = perCharger->findChild<QTableWidget *>("transactionsTable");
        QVERIFY(mine);
        QCOMPARE(mine->rowCount(), 2);
        QCOMPARE(mine->item(1, 7)->text(), QString("7000"));
        QVERIFY(mine->isColumnHidden(1));
    }
    void listShowsChargersAndEmitsSelection()
    {
        Database db;
        QVERIFY(db.open(":memory:"));
        db.chargePointConnected("OLD-1", "10.0.0.9"); // known from an earlier run, now offline
        db.chargePointDisconnected("OLD-1");

        OcppServer server;
        QVERIFY(server.listen(0, QHostAddress::LocalHost));
        CentralSystem cs(&server, &db);
        AppConfig config;
        MainWindow window(config, &cs);
        window.show();

        auto a = connectCp(server, "CP-A");
        auto b = connectCp(server, "CP-B");
        status(*a, 1, "Charging");
        status(*b, 1, "Available");
        status(*b, 2, "Faulted");

        auto *table = window.list()->findChild<QTableView *>();
        QVERIFY(table);
        QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(), 3, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(table->model()->index(1, 2).data().toString(),
                                  QString("1: Available, 2: Faulted"), 3000);

        // Sorted by id; offline charger is listed but marked disconnected.
        QCOMPARE(table->model()->index(0, 0).data().toString(), QString("CP-A"));
        QCOMPARE(table->model()->index(1, 0).data().toString(), QString("CP-B"));
        QCOMPARE(table->model()->index(0, 1).data().toString(), QString("Connected"));
        QCOMPARE(table->model()->index(2, 0).data().toString(), QString("OLD-1"));
        QCOMPARE(table->model()->index(2, 1).data().toString(), QString("Disconnected"));

        // Colors: connected = green, faulted = red, offline rows are muted.
        QCOMPARE(table->model()->index(0, 1).data(Qt::ForegroundRole).value<QColor>(), QColor(46, 160, 67));
        QCOMPARE(table->model()->index(1, 2).data(Qt::ForegroundRole).value<QColor>(), QColor(215, 58, 73));
        QVERIFY(table->model()->index(2, 1).data(Qt::ForegroundRole).value<QColor>() != QColor(46, 160, 67));

        QVERIFY(window.statusBar()->findChild<QLabel *>()->text().contains("2 of 3 charge points connected"));

        QSignalSpy selected(window.list(), &ChargePointListWidget::chargePointSelected);
        window.list()->selectId("CP-B");
        QCOMPARE(selected.count(), 1);
        QCOMPARE(selected.first().first().toString(), QString("CP-B"));
        QCOMPARE(window.list()->selectedId(), QString("CP-B"));

        // The detail view follows the selection and shows the logged frames (newest first).
        QCOMPARE(window.detail()->chargePointId(), QString("CP-B"));
        auto *messages = window.detail()->findChild<QTableWidget *>("messagesTable");
        QVERIFY(messages);
        QTRY_VERIFY_WITH_TIMEOUT(messages->rowCount() >= 3, 3000);
        QCOMPARE(messages->item(0, 3)->text().isEmpty(), false);
        messages->selectRow(messages->rowCount() - 1); // oldest: a StatusNotification call
        QVERIFY(window.detail()->findChild<QPlainTextEdit *>("messageText")->toPlainText().contains("connectorId"));

        // A new frame for the shown charger appears without re-selecting.
        const int before = messages->rowCount();
        b->sendTextMessage(OcppMessage::makeCall("hb", "Heartbeat", {}).toJson());
        QTRY_VERIFY_WITH_TIMEOUT(messages->rowCount() >= before + 2, 3000);

        const QString shot = qEnvironmentVariable("SLEEKOCPP_SCREENSHOT");
        if (!shot.isEmpty())
            QVERIFY(window.grab().save(shot));
    }

    void startupErrorsShowBanner()
    {
        Database db;
        QVERIFY(db.open(":memory:"));
        OcppServer server;
        CentralSystem cs(&server, &db);
        AppConfig config;
        MainWindow window(config, &cs);
        window.show();
        window.showStartupErrors({"OCPP port 9000 could not be opened (already in use?)."});

        auto *banner = window.findChild<QLabel *>("startupErrorBanner");
        QVERIFY(banner);
        QVERIFY(banner->text().contains("9000"));
        QVERIFY(window.list()->isVisible());
        // Dismiss the modal dialog that is shown on the next event loop turn.
        QTRY_VERIFY_WITH_TIMEOUT(qobject_cast<QMessageBox *>(QApplication::activeModalWidget()), 2000);
        QApplication::activeModalWidget()->close();
    }

    void configurationTabReadsAndEdits()
    {
        Database db;
        QVERIFY(db.open(":memory:"));
        OcppServer server;
        QVERIFY(server.listen(0, QHostAddress::LocalHost));
        CentralSystem cs(&server, &db);
        AppConfig config;
        MainWindow window(config, &cs);
        window.show();

        // Fake charger: answers GetConfiguration with two keys, accepts HeartbeatInterval, rejects the rest.
        auto cp = connectCp(server, "CFG-1");
        QStringList changed;
        QObject::connect(cp.get(), &QWebSocket::textMessageReceived, cp.get(), [&](const QString &text) {
            const auto m = OcppMessage::parse(text);
            if (!m || m->type != OcppMessage::Type::Call)
                return;
            QJsonObject reply;
            if (m->action == "GetConfiguration") {
                reply = {{"configurationKey", QJsonArray{
                    QJsonObject{{"key", "HeartbeatInterval"}, {"readonly", false}, {"value", "60"}},
                    QJsonObject{{"key", "NumberOfConnectors"}, {"readonly", true}, {"value", "2"}}}}};
            } else if (m->action == "ChangeConfiguration") {
                changed << m->payload["key"].toString() + "=" + m->payload["value"].toString();
                reply = {{"status", m->payload["key"] == "HeartbeatInterval" ? "Accepted" : "Rejected"}};
            }
            cp->sendTextMessage(OcppMessage::makeResult(m->uniqueId, reply).toJson());
        });
        status(*cp, 1, "Available");
        auto *table = window.list()->findChild<QTableView *>();
        QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(), 1, 3000);
        window.list()->selectId("CFG-1");

        auto *tab = window.detail()->findChild<ChargePointConfigurationWidget *>();
        QVERIFY(tab);
        auto *keys = tab->findChild<QTableWidget *>();
        QCOMPARE(keys->rowCount(), 0);

        QTest::mouseClick(tab->findChildren<QPushButton *>().first(), Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(keys->rowCount(), 2, 3000);
        QCOMPARE(keys->item(0, 0)->text(), QString("HeartbeatInterval"));
        QVERIFY(keys->item(0, 1)->flags() & Qt::ItemIsEditable);
        QVERIFY(!(keys->item(1, 1)->flags() & Qt::ItemIsEditable)); // read-only key

        // Accepted edit is kept; rejected edit snaps back.
        keys->item(0, 1)->setText("30");
        QTRY_COMPARE_WITH_TIMEOUT(changed, QStringList{"HeartbeatInterval=30"}, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(keys->item(0, 1)->text(), QString("30"), 3000);
        keys->item(1, 1)->setText("5"); // programmatic edit of a read-only row still goes out; charger rejects
        QTRY_COMPARE_WITH_TIMEOUT(changed.size(), 2, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(keys->item(1, 1)->text(), QString("2"), 3000);

        const QString shot = qEnvironmentVariable("SLEEKOCPP_SCREENSHOT_CONFIG");
        if (!shot.isEmpty()) {
            window.detail()->findChild<QTabWidget *>()->setCurrentWidget(tab);
            QVERIFY(window.grab().save(shot));
        }
    }

    void controlTabSendsCommands()
    {
        Database db;
        QVERIFY(db.open(":memory:"));
        OcppServer server;
        QVERIFY(server.listen(0, QHostAddress::LocalHost));
        CentralSystem cs(&server, &db);
        AppConfig config;
        config.defaultIdTag = "DEFAULT";
        MainWindow window(config, &cs);
        window.show();

        auto cp = connectCp(server, "CTL-1");
        QList<OcppMessage> calls;
        QObject::connect(cp.get(), &QWebSocket::textMessageReceived, cp.get(), [&](const QString &text) {
            const auto m = OcppMessage::parse(text);
            if (m && m->type == OcppMessage::Type::Call) {
                calls << *m;
                cp->sendTextMessage(OcppMessage::makeResult(m->uniqueId, {{"status", "Accepted"}}).toJson());
            }
        });
        status(*cp, 1, "Available");
        QTRY_COMPARE_WITH_TIMEOUT(window.list()->findChild<QTableView *>()->model()->rowCount(), 1, 3000);
        window.list()->selectId("CTL-1");

        auto *tab = window.detail()->findChild<ChargePointControlWidget *>();
        QVERIFY(tab);
        auto button = [&](const QString &text) {
            for (auto *b : tab->findChildren<QPushButton *>())
                if (b->text() == text)
                    return b;
            return static_cast<QPushButton *>(nullptr);
        };
        auto *label = tab->findChild<QLabel *>("controlResult");

        // idTag is pre-filled with the configured default and sent as typed.
        auto *idTag = tab->findChild<QLineEdit *>("idTag");
        QCOMPARE(idTag->text(), QString("DEFAULT"));
        idTag->setText("MYTAG");
        QTest::mouseClick(button("Start charging"), Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(calls.size(), 1, 3000);
        QCOMPARE(calls[0].action, QString("RemoteStartTransaction"));
        QCOMPARE(calls[0].payload["idTag"].toString(), QString("MYTAG"));
        QVERIFY(!calls[0].payload.contains("connectorId"));
        QTRY_VERIFY_WITH_TIMEOUT(label->text().contains("Accepted"), 3000);

        QTest::mouseClick(button("Send SetChargingProfile"), Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(calls.size(), 2, 3000);
        QCOMPARE(calls[1].action, QString("SetChargingProfile"));
        const auto profile = calls[1].payload["csChargingProfiles"].toObject();
        QCOMPARE(profile["chargingSchedule"].toObject()["chargingSchedulePeriod"].toArray()[0].toObject()["limit"].toDouble(), 16.0);

        // Raw message with invalid JSON is rejected locally, valid JSON goes out.
        auto *action = tab->findChild<QLineEdit *>("rawAction");
        action->setText("GetLocalListVersion");
        auto *payload = tab->findChild<QPlainTextEdit *>("rawPayload");
        payload->setPlainText("{nope");
        QTest::mouseClick(button("Send"), Qt::LeftButton);
        QVERIFY(label->text().contains("not a JSON object"));
        QCOMPARE(calls.size(), 2);
        payload->setPlainText("{}");
        QTest::mouseClick(button("Send"), Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(calls.size(), 3, 3000);
        QCOMPARE(calls[2].action, QString("GetLocalListVersion"));

        const QString shot = qEnvironmentVariable("SLEEKOCPP_SCREENSHOT_CONTROL");
        if (!shot.isEmpty()) {
            window.detail()->findChild<QTabWidget *>()->setCurrentWidget(tab);
            QVERIFY(window.grab().save(shot));
        }
    }

    void chargingProfileShape()
    {
        const auto tx = ChargePointControlWidget::buildChargingProfile(3, 1, "TxProfile", "Relative", "W", 7000, 600, 42);
        QCOMPARE(tx["transactionId"].toInt(), 42);
        QCOMPARE(tx["chargingSchedule"].toObject()["duration"].toInt(), 600);
        QVERIFY(!tx["chargingSchedule"].toObject().contains("startSchedule"));

        const auto abs = ChargePointControlWidget::buildChargingProfile(1, 0, "TxDefaultProfile", "Absolute", "A", 10, 0, 0);
        QVERIFY(abs["chargingSchedule"].toObject().contains("startSchedule"));
        QVERIFY(!abs["chargingSchedule"].toObject().contains("duration"));
        QVERIFY(!abs.contains("transactionId"));

        QCOMPARE(ChargePointControlWidget::buildChargingProfile(1, 0, "TxDefaultProfile", "Recurring", "A", 10, 0, 0)["recurrencyKind"].toString(),
                 QString("Daily"));
    }

    void emptyListShowsHint()
    {
        Database db;
        QVERIFY(db.open(":memory:"));
        OcppServer server;
        QVERIFY(server.listen(0, QHostAddress::LocalHost));
        CentralSystem cs(&server, &db);
        AppConfig config;
        MainWindow window(config, &cs);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        QVERIFY(!window.list()->findChild<QTableView *>()->isVisible());
        bool hintShown = false;
        for (auto *l : window.list()->findChildren<QLabel *>())
            hintShown = hintShown || (l->isVisible() && l->text().contains("No charge points yet"));
        QVERIFY(hintShown);
    }
};

QTEST_MAIN(TstGui)
#include "tst_gui.moc"
