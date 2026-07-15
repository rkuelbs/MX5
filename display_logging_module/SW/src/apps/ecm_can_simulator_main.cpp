#include "can/can_bus_endpoint.h"
#include "can/dbc_decoder.h"
#include "logging/session_log_sink.h"
#include "sim/ecm_simulator.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QTimer>

using miata::data::CanBusEndpoint;
using miata::data::DbcDecoder;
using miata::data::EcmSimulator;
using miata::data::SessionLogSink;

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("ecm_can_simulator"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser arguments;
    arguments.setApplicationDescription(
        QStringLiteral("DBC-driven ECM CAN simulator with hot-reloaded scenarios"));
    arguments.addHelpOption();
    arguments.addVersionOption();
    arguments.addOption({QStringLiteral("dbc"), QStringLiteral("Vehicle-wide DBC file"),
                         QStringLiteral("path"), QStringLiteral("shared/miata.dbc")});
    arguments.addOption({QStringLiteral("scenario"), QStringLiteral("Simulator JSON scenario"),
                         QStringLiteral("path"),
                         QStringLiteral("display_logging_module/SW/config/ecm_simulator.json")});
    arguments.addOption({QStringLiteral("plugin"), QStringLiteral("Qt CAN plugin"),
                         QStringLiteral("name"), QStringLiteral("virtualcan")});
    arguments.addOption({QStringLiteral("interface"), QStringLiteral("CAN interface/channel"),
                         QStringLiteral("name"), QStringLiteral("can0")});
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

    const QString dbcPath = arguments.value(QStringLiteral("dbc"));
    DbcDecoder codec;
    QString error;
    if (!codec.load(dbcPath, &error)) {
        qCritical().noquote() << "DBC load failed:" << error;
        return 2;
    }

    EcmSimulator simulator(&codec);
    if (!simulator.watchScenario(arguments.value(QStringLiteral("scenario")), &error)) {
        qCritical().noquote() << "Scenario load failed:" << error;
        return 3;
    }

    CanBusEndpoint endpoint;
    if (!arguments.isSet(noBusOption)
        && !endpoint.start(
            arguments.value(QStringLiteral("plugin")),
            arguments.value(QStringLiteral("interface")),
            &error)) {
        qCritical().noquote() << "CAN endpoint start failed:" << error;
        return 4;
    }

    SessionLogSink recorder;
    if (arguments.isSet(QStringLiteral("record"))
        && !recorder.openFiles(arguments.value(QStringLiteral("record")), {}, dbcPath, &error)) {
        qCritical().noquote() << "Simulator recording failed:" << error;
        return 5;
    }

    QObject::connect(
        &simulator,
        &EcmSimulator::frameGenerated,
        &application,
        [&](const miata::data::CanFrameRecord& generated) {
            miata::data::CanFrameRecord record = generated;
            record.interfaceName = arguments.value(QStringLiteral("interface"));
            if (!arguments.isSet(noBusOption) && !endpoint.writeFrame(record.frame, &error)) {
                qWarning().noquote() << "CAN write failed:" << error;
            }
            if (recorder.isOpen() && !recorder.writeRawFrame(record, &error)) {
                qWarning().noquote() << "Simulator log write failed:" << error;
            }
        });
    QObject::connect(
        &simulator,
        &EcmSimulator::scenarioReloaded,
        [](const QString& path) { qInfo().noquote() << "Reloaded scenario" << path; });
    QObject::connect(
        &simulator,
        &EcmSimulator::simulatorError,
        [](const QString& message) { qWarning().noquote() << message; });
    QObject::connect(
        &endpoint,
        &CanBusEndpoint::endpointError,
        [](const QString& message) { qWarning().noquote() << "CAN endpoint error:" << message; });

    QTimer flushTimer;
    flushTimer.setInterval(1000);
    QObject::connect(&flushTimer, &QTimer::timeout, [&] {
        if (recorder.isOpen() && !recorder.flush(&error)) {
            qWarning().noquote() << error;
        }
    });
    flushTimer.start();

    QObject::connect(&application, &QCoreApplication::aboutToQuit, [&] { recorder.flush(); });
    simulator.start();
    qInfo() << "ECM simulator running at" << simulator.rateHz() << "Hz using"
            << arguments.value(QStringLiteral("plugin"))
            << arguments.value(QStringLiteral("interface"));
    return application.exec();
}
