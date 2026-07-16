#include "can/can_bus_endpoint.h"
#include "can/dbc_decoder.h"
#include "logging/session_log_sink.h"
#include "sim/wcm_simulator.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>

int main(int argc, char* argv[]) {
    QGuiApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("wcm_simulator"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

#ifdef Q_OS_WIN
    const QString defaultPlugin = QStringLiteral("virtualcan");
#else
    const QString defaultPlugin = QStringLiteral("socketcan");
#endif

    QCommandLineParser arguments;
    arguments.setApplicationDescription(QStringLiteral("Mouse-driven WCM CAN simulator"));
    arguments.addHelpOption();
    arguments.addVersionOption();
    arguments.addOption({QStringLiteral("dbc"), QStringLiteral("Vehicle-wide DBC file"),
                         QStringLiteral("path"), QStringLiteral("shared/miata.dbc")});
    arguments.addOption({QStringLiteral("plugin"), QStringLiteral("Qt CAN plugin"),
                         QStringLiteral("name"), defaultPlugin});
    arguments.addOption({QStringLiteral("interface"), QStringLiteral("CAN interface/channel"),
                         QStringLiteral("name"), QStringLiteral("can0")});
    arguments.addOption({QStringLiteral("status-rate"), QStringLiteral("Periodic status rate in Hz"),
                         QStringLiteral("hz"), QStringLiteral("20")});
    arguments.addOption({QStringLiteral("record"),
                         QStringLiteral("Optional candump file for generated frames"),
                         QStringLiteral("path")});
    const QCommandLineOption noBusOption(
        QStringLiteral("no-bus"),
        QStringLiteral("Do not publish to a CAN plugin; requires --record"));
    arguments.addOption(noBusOption);
    arguments.process(application);

    if (arguments.isSet(noBusOption) && !arguments.isSet(QStringLiteral("record"))) {
        qCritical() << "--no-bus requires --record";
        return 1;
    }

    miata::data::DbcDecoder codec;
    QString error;
    const QString dbcPath = arguments.value(QStringLiteral("dbc"));
    if (!codec.load(dbcPath, &error)) {
        qCritical().noquote() << "DBC load failed:" << error;
        return 2;
    }

    bool rateValid = false;
    const int statusRate = arguments.value(QStringLiteral("status-rate")).toInt(&rateValid);
    miata::data::WcmSimulator simulator(&codec);
    if (!rateValid || !simulator.setStatusRateHz(statusRate, &error)) {
        qCritical().noquote() << (error.isEmpty() ? QStringLiteral("Invalid --status-rate") : error);
        return 3;
    }

    miata::data::CanBusEndpoint endpoint;
    const bool busEnabled = !arguments.isSet(noBusOption);
    if (busEnabled && !endpoint.start(arguments.value(QStringLiteral("plugin")),
                                      arguments.value(QStringLiteral("interface")), &error)) {
        qCritical().noquote() << "CAN endpoint start failed:" << error;
        return 4;
    }

    miata::data::SessionLogSink recorder;
    if (arguments.isSet(QStringLiteral("record"))
        && !recorder.openFiles(arguments.value(QStringLiteral("record")), {}, dbcPath, &error)) {
        qCritical().noquote() << "Simulator recording failed:" << error;
        return 5;
    }

    QObject::connect(&simulator, &miata::data::WcmSimulator::frameGenerated,
                     &application, [&](const miata::data::CanFrameRecord& generated) {
        auto record = generated;
        record.interfaceName = arguments.value(QStringLiteral("interface"));
        if (busEnabled && !endpoint.writeFrame(record.frame, &error))
            qWarning().noquote() << "CAN write failed:" << error;
        if (recorder.isOpen() && !recorder.writeRawFrame(record, &error))
            qWarning().noquote() << "Simulator recording failed:" << error;
    });
    QObject::connect(&simulator, &miata::data::WcmSimulator::simulatorError,
                     [](const QString& message) { qWarning().noquote() << message; });
    QObject::connect(&endpoint, &miata::data::CanBusEndpoint::endpointError,
                     [](const QString& message) { qWarning().noquote() << "CAN error:" << message; });

    QQmlApplicationEngine engine;
    engine.setInitialProperties({
        {QStringLiteral("simulatorBackend"), QVariant::fromValue(&simulator)},
        {QStringLiteral("busDescription"), busEnabled
             ? QStringLiteral("%1 / %2")
                   .arg(arguments.value(QStringLiteral("plugin")),
                        arguments.value(QStringLiteral("interface")))
             : QStringLiteral("Recording only")},
        {QStringLiteral("statusRateHz"), statusRate},
    });
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &application, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("Miata.WcmSimulator"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) return 6;

    QObject::connect(&application, &QCoreApplication::aboutToQuit, [&] {
        simulator.stop();
        recorder.flush();
    });
    simulator.start();
    return application.exec();
}
