#include "ipc/replay_control.h"

#include <QCoreApplication>
#include <QtTest>

class ReplayControlTest final : public QObject {
    Q_OBJECT

private slots:
    void exchangesStatusAndCommands();
};

void ReplayControlTest::exchangesStatusAndCommands() {
    miata::ipc::ReplayControlStatus status{
        QStringLiteral("can"), QStringLiteral("playing"), 10'000'000, 100'000'000, 1.0};
    QString lastCommand;
    QJsonObject lastParameters;
    miata::ipc::ReplayControlServer server;
    server.setStatusProvider([&] { return status; });
    server.setCommandHandler([&](const QString& command, const QJsonObject& parameters, QString*) {
        lastCommand = command;
        lastParameters = parameters;
        if (command == QStringLiteral("pause")) status.state = QStringLiteral("paused");
        if (command == QStringLiteral("play")) status.state = QStringLiteral("playing");
        if (command == QStringLiteral("seek"))
            status.positionNs = qint64(parameters.value(QStringLiteral("position_ns")).toDouble());
        if (command == QStringLiteral("speed"))
            status.speedFactor = parameters.value(QStringLiteral("factor")).toDouble();
        return true;
    });
    const QString name = QStringLiteral("miata-replay-test-%1").arg(QCoreApplication::applicationPid());
    QString error;
    QVERIFY2(server.start(name, &error), qPrintable(error));

    miata::ipc::ReplayControlClient client;
    client.setServerName(name);
    client.start();
    QTRY_VERIFY_WITH_TIMEOUT(client.available(), 2000);
    QCOMPARE(client.source(), QStringLiteral("can"));
    QCOMPARE(client.durationMs(), 100.0);

    QVERIFY(client.pause());
    QTRY_COMPARE_WITH_TIMEOUT(lastCommand, QStringLiteral("pause"), 1000);
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), QStringLiteral("paused"), 1000);
    QVERIFY(client.seekMs(42.0));
    QTRY_COMPARE_WITH_TIMEOUT(lastCommand, QStringLiteral("seek"), 1000);
    QTRY_COMPARE_WITH_TIMEOUT(client.positionMs(), 42.0, 1000);
    QVERIFY(client.setPlaybackSpeed(2.0));
    QTRY_COMPARE_WITH_TIMEOUT(lastCommand, QStringLiteral("speed"), 1000);
    QTRY_COMPARE_WITH_TIMEOUT(client.speedFactor(), 2.0, 1000);
    QVERIFY(!client.seekMs(-1));
    QVERIFY(!client.setPlaybackSpeed(0));

    client.stop();
    server.stop();
}

QTEST_MAIN(ReplayControlTest)
#include "replay_control_test.moc"
