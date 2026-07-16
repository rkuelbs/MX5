#include "backend/dash_input_controller.h"
#include "backend/ipc_signal_source.h"
#include "ipc/signal_ipc_server.h"

#include <QCoreApplication>
#include <QUuid>
#include <QtTest>

class SignalIpcIntegrationTest final : public QObject {
    Q_OBJECT

private slots:
    void streamsDefinitionsStateAndEveryWcmEvent();
    void reconnectsAfterServerRestart();
};

namespace {

QString uniqueServerName() {
    return QStringLiteral("miata-ipc-test-%1-%2")
        .arg(QCoreApplication::applicationPid())
        .arg(QUuid::createUuid().toString(QUuid::Id128));
}

}  // namespace

void SignalIpcIntegrationTest::streamsDefinitionsStateAndEveryWcmEvent() {
    miata::ipc::SignalIpcServer server;
    server.setDefinitions({
        {QStringLiteral("ECM.rpm"), QStringLiteral("RPM")},
        {QStringLiteral("WCM.inputs"), {}},
        {QStringLiteral("WCM.event_id"), {}},
        {QStringLiteral("WCM.event_type"), {}},
        {QStringLiteral("WCM.event_length"), QStringLiteral("ms")},
    });
    QString error;
    const QString name = uniqueServerName();
    QVERIFY2(server.start(name, &error), qPrintable(error));

    miata::dash::IpcSignalSource source;
    source.setServerName(name);
    bool connected = false;
    int definitionCount = 0;
    QList<miata::data::SignalSample> received;
    miata::dash::DashInputController inputController;
    QSignalSpy actions(&inputController, &miata::dash::DashInputController::actionTriggered);
    connect(&source, &miata::dash::SignalDataSource::connectedChanged,
            this, [&connected](bool value) { connected = value; });
    connect(&source, &miata::dash::SignalDataSource::definitionsAvailable,
            this, [&definitionCount](const auto& definitions) {
        definitionCount = definitions.size();
    });
    connect(&source, &miata::dash::SignalDataSource::samplesAvailable,
            this, [&received](const auto& samples) { received.append(samples); });
    connect(&source, &miata::dash::SignalDataSource::samplesAvailable,
            &inputController, &miata::dash::DashInputController::processSamples);
    source.start();
    QTRY_VERIFY_WITH_TIMEOUT(connected, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(definitionCount, 5, 2000);

    server.publishSamples({
        {QStringLiteral("ECM.rpm"), 4100.0, {}, 10, miata::data::SignalSource::Can},
        {QStringLiteral("ECM.rpm"), 4200.0, {}, 11, miata::data::SignalSource::Can},
        {QStringLiteral("WCM.inputs"), 0, {}, 12, miata::data::SignalSource::Can},
        {QStringLiteral("WCM.event_id"), 2, {}, 20, miata::data::SignalSource::Can},
        {QStringLiteral("WCM.event_type"), 1, {}, 20, miata::data::SignalSource::Can},
        {QStringLiteral("WCM.event_length"), 0, {}, 20, miata::data::SignalSource::Can},
        {QStringLiteral("WCM.event_id"), 2, {}, 21, miata::data::SignalSource::Can},
        {QStringLiteral("WCM.event_type"), 0, {}, 21, miata::data::SignalSource::Can},
        {QStringLiteral("WCM.event_length"), 125, {}, 21, miata::data::SignalSource::Can},
    });
    QTRY_VERIFY_WITH_TIMEOUT(received.size() >= 8, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(actions.size(), 1, 2000);
    QCOMPARE(qvariant_cast<miata::dash::DashAction>(actions.at(0).at(0)),
             miata::dash::DashAction::MenuBack);

    int eventSamples = 0;
    int rpmSamples = 0;
    for (const auto& sample : std::as_const(received)) {
        if (sample.canonicalName.startsWith(QStringLiteral("WCM.event_"))) ++eventSamples;
        if (sample.canonicalName == QStringLiteral("ECM.rpm")) {
            ++rpmSamples;
            QCOMPARE(sample.value.toDouble(), 4200.0);
            QCOMPARE(sample.unit, QStringLiteral("RPM"));
        }
    }
    QCOMPARE(eventSamples, 6);
    QCOMPARE(rpmSamples, 1);
    source.stop();
    server.stop();
}

void SignalIpcIntegrationTest::reconnectsAfterServerRestart() {
    const QString name = uniqueServerName();
    miata::ipc::SignalIpcServer server;
    server.setDefinitions({{QStringLiteral("ECM.rpm"), QStringLiteral("RPM")}});
    QString error;
    QVERIFY2(server.start(name, &error), qPrintable(error));

    miata::dash::IpcSignalSource source;
    source.setServerName(name);
    int connections = 0;
    int disconnections = 0;
    connect(&source, &miata::dash::SignalDataSource::connectedChanged,
            this, [&connections, &disconnections](bool connected) {
        connected ? ++connections : ++disconnections;
    });
    source.start();
    QTRY_COMPARE_WITH_TIMEOUT(connections, 1, 2000);

    server.stop();
    QTRY_COMPARE_WITH_TIMEOUT(disconnections, 1, 2000);
    QVERIFY2(server.start(name, &error), qPrintable(error));
    QTRY_COMPARE_WITH_TIMEOUT(connections, 2, 3000);

    source.stop();
    server.stop();
}

QTEST_MAIN(SignalIpcIntegrationTest)
#include "signal_ipc_integration_test.moc"
