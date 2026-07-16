#include "backend/dash_input_controller.h"
#include "backend/dash_config_store.h"
#include "backend/fake_signal_source.h"
#include "backend/ipc_signal_source.h"
#include "backend/navigation_controller.h"
#include "backend/signal_filter_model.h"
#include "backend/signal_history_store.h"
#include "backend/signal_list_model.h"
#include "backend/signal_value_provider.h"
#include "backend/warning_evaluator.h"
#include "backend/warning_manager.h"
#include "ipc/signal_ipc_protocol.h"
#include "ipc/replay_control.h"
#include "platform/systemd_notifier.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>

int main(int argc, char* argv[]) {
    QGuiApplication application(argc, argv);
    QElapsedTimer startupClock;
    startupClock.start();
    QCoreApplication::setApplicationName(QStringLiteral("miata_dash"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    qInfo() << "BOOT_MILESTONE dash_process_started_ms" << startupClock.elapsed();

    miata::platform::SystemdNotifier systemdNotifier;
    QObject::connect(&systemdNotifier, &miata::platform::SystemdNotifier::notificationError,
                     [](const QString& message) { qWarning().noquote() << message; });

    QCommandLineParser arguments;
    arguments.setApplicationDescription(QStringLiteral("Miata Qt Quick dashboard"));
    arguments.addHelpOption();
    arguments.addVersionOption();
    const QCommandLineOption fullscreenOption(
        QStringLiteral("fullscreen"), QStringLiteral("Start in full-screen mode"));
    const QCommandLineOption demoWarningOption(
        QStringLiteral("demo-warning"),
        QStringLiteral("Show an acknowledgeable warning for UI testing"));
    const QCommandLineOption dataSourceOption(
        QStringLiteral("data-source"),
        QStringLiteral("Signal source: fake or ipc"),
        QStringLiteral("source"),
#ifdef Q_OS_WIN
        QStringLiteral("fake"));
#else
        QStringLiteral("ipc"));
#endif
    const QCommandLineOption ipcNameOption(
        QStringLiteral("ipc-name"), QStringLiteral("Logger local IPC server name"),
        QStringLiteral("name"), miata::ipc::kDefaultSignalIpcServerName);
    const QCommandLineOption dashConfigOption(
        QStringLiteral("dash-config"), QStringLiteral("Dash presentation and warning configuration"),
        QStringLiteral("path"), QStringLiteral(":/dash/dash.json"));
    const QCommandLineOption replayControlOption(
        QStringLiteral("replay-control-name"),
        QStringLiteral("Development replay control server name"), QStringLiteral("name"));
    arguments.addOption(fullscreenOption);
    arguments.addOption(demoWarningOption);
    arguments.addOption(dataSourceOption);
    arguments.addOption(ipcNameOption);
    arguments.addOption(dashConfigOption);
    arguments.addOption(replayControlOption);
    arguments.process(application);

    const QString dataSourceSelection = arguments.value(dataSourceOption).toLower();
    if (dataSourceSelection != QStringLiteral("fake")
        && dataSourceSelection != QStringLiteral("ipc")) {
        qCritical() << "--data-source must be 'fake' or 'ipc'";
        return 1;
    }

    miata::dash::SignalListModel sourceModel;
    miata::dash::SignalFilterModel filteredModel;
    filteredModel.setSignalModel(&sourceModel);
    miata::dash::FakeSignalSource fakeSource;
    miata::dash::IpcSignalSource ipcSource;
    ipcSource.setServerName(arguments.value(ipcNameOption));
    miata::dash::SignalDataSource* dataSource = dataSourceSelection == QStringLiteral("ipc")
        ? static_cast<miata::dash::SignalDataSource*>(&ipcSource)
        : static_cast<miata::dash::SignalDataSource*>(&fakeSource);
    miata::dash::DashInputController inputController;
    miata::dash::NavigationController navigationController;
    miata::dash::WarningManager warningManager;
    miata::dash::DashConfigStore dashConfiguration;
    miata::dash::SignalValueProvider signalProvider;
    miata::dash::SignalHistoryStore signalHistory;
    miata::ipc::ReplayControlClient replayController;
    if (arguments.isSet(replayControlOption)) {
        replayController.setServerName(arguments.value(replayControlOption));
        replayController.start();
    }
    QString dashConfigError;
    if (!dashConfiguration.load(arguments.value(dashConfigOption), &dashConfigError)) {
        qCritical().noquote() << "Dash config load failed:" << dashConfigError;
        return 2;
    }
    for (const auto& signalName : dashConfiguration.configuredSignalNames()) {
        const auto* presentation = dashConfiguration.presentationObject(signalName);
        if (presentation && presentation->freshnessConfigured())
            signalProvider.setSignalStaleAfterMs(signalName, presentation->staleAfterMs());
    }
    miata::dash::WarningEvaluator warningEvaluator(
        &dashConfiguration, &warningManager);

    QObject::connect(dataSource, &miata::dash::SignalDataSource::definitionsAvailable,
                     &sourceModel, &miata::dash::SignalListModel::setDefinitions);
    QObject::connect(dataSource, &miata::dash::SignalDataSource::definitionsAvailable,
                     &signalProvider, &miata::dash::SignalValueProvider::setDefinitions);
    QObject::connect(dataSource, &miata::dash::SignalDataSource::samplesAvailable,
                     &sourceModel, &miata::dash::SignalListModel::updateSamples);
    QObject::connect(dataSource, &miata::dash::SignalDataSource::samplesAvailable,
                     &inputController, &miata::dash::DashInputController::processSamples);
    QObject::connect(dataSource, &miata::dash::SignalDataSource::samplesAvailable,
                     &dashConfiguration, &miata::dash::DashConfigStore::updateSamples);
    QObject::connect(dataSource, &miata::dash::SignalDataSource::samplesAvailable,
                     &signalProvider, &miata::dash::SignalValueProvider::updateSamples);
    QObject::connect(dataSource, &miata::dash::SignalDataSource::samplesAvailable,
                     &warningEvaluator, &miata::dash::WarningEvaluator::processSamples);
    QObject::connect(dataSource, &miata::dash::SignalDataSource::samplesAvailable,
                     &signalHistory, &miata::dash::SignalHistoryStore::updateSamples);
    QObject::connect(&sourceModel, &miata::dash::SignalListModel::selectedSignalsChanged,
                     &signalHistory, &miata::dash::SignalHistoryStore::setSelectedSignals);
    QObject::connect(dataSource, &miata::dash::SignalDataSource::sourceError,
                     [](const QString& message) { qWarning().noquote() << message; });
    QObject::connect(
        &inputController, &miata::dash::DashInputController::actionTriggered,
        &application, [&warningManager, &navigationController](miata::dash::DashAction action) {
            if (!warningManager.handleAction(action)) navigationController.handleAction(action);
        });
    if (arguments.isSet(demoWarningOption)) {
        warningManager.raiseWarning(
            QStringLiteral("demo_oil_pressure"), QStringLiteral("OIL PRESSURE"),
            QStringLiteral("This is a simulated warning. Press ACK or Enter to acknowledge it."),
            QStringLiteral("critical"));
    }

    QQmlApplicationEngine engine;
    engine.setInitialProperties({
        {QStringLiteral("signalModel"), QVariant::fromValue(&filteredModel)},
        {QStringLiteral("showFullScreen"), arguments.isSet(fullscreenOption)},
        {QStringLiteral("dataSourceName"),
         dataSourceSelection == QStringLiteral("ipc")
             ? QStringLiteral("Logger IPC: %1").arg(arguments.value(ipcNameOption))
             : QStringLiteral("Animated simulation")},
        {QStringLiteral("inputController"), QVariant::fromValue(&inputController)},
        {QStringLiteral("navigationController"), QVariant::fromValue(&navigationController)},
        {QStringLiteral("warningManager"), QVariant::fromValue(&warningManager)},
        {QStringLiteral("signalProvider"), QVariant::fromValue(&signalProvider)},
        {QStringLiteral("presentationProvider"), QVariant::fromValue(&dashConfiguration)},
        {QStringLiteral("historyStore"), QVariant::fromValue(&signalHistory)},
        {QStringLiteral("replayController"), QVariant::fromValue(&replayController)},
    });
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &application, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);

    dataSource->start();
    QObject::connect(&application, &QCoreApplication::aboutToQuit,
                     dataSource, &miata::dash::SignalDataSource::stop);
    QObject::connect(&application, &QCoreApplication::aboutToQuit, [&] {
        replayController.stop();
        systemdNotifier.notifyStopping(QStringLiteral("Dashboard stopping"));
    });
    engine.loadFromModule(QStringLiteral("Miata.Dash"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) return 3;

    bool firstFrameReported = false;
    auto reportReady = [&] {
        if (firstFrameReported) return;
        firstFrameReported = true;
        qInfo() << "BOOT_MILESTONE dash_first_frame_ms" << startupClock.elapsed();
        systemdNotifier.notifyReady(QStringLiteral("Dashboard first frame displayed"));
    };
    if (auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst())) {
        QObject::connect(window, &QQuickWindow::frameSwapped, &application, reportReady);
    } else {
        reportReady();
    }
    return application.exec();
}
