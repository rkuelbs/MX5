#include "can/can_bus_endpoint.h"
#include "can/can_replay_source.h"
#include "can/dbc_decoder.h"
#include "core/signal_registry.h"
#include "core/source_health_monitor.h"
#include "logging/async_session_logger.h"
#include "logging/decoded_log_policy.h"
#include "vn300/vn300_binary_parser.h"
#include "vn300/vn300_serial_source.h"
#include "vn300/vn300_replay_source.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QTimer>

using miata::data::CanBusEndpoint;
using miata::data::CanFrameRecord;
using miata::data::CanReplaySource;
using miata::data::DbcDecoder;
using miata::data::DecodedLogPolicy;
using miata::data::AsyncSessionLogger;
using miata::data::SignalRegistry;
using miata::data::SignalSource;
using miata::data::SourceHealthMonitor;
using miata::data::Vn300BinaryParser;
using miata::data::Vn300SerialSource;
using miata::data::Vn300ReplaySource;

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vehicle_loggerd"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.3.0"));

#ifdef Q_OS_WIN
    const QString defaultPlugin = QStringLiteral("virtualcan");
#else
    const QString defaultPlugin = QStringLiteral("socketcan");
#endif

    QCommandLineParser arguments;
    arguments.setApplicationDescription(QStringLiteral("Miata vehicle data acquisition service"));
    arguments.addHelpOption();
    arguments.addVersionOption();
    arguments.addOption({QStringLiteral("dbc"), QStringLiteral("Vehicle-wide DBC file"),
                         QStringLiteral("path"), QStringLiteral("shared/miata.dbc")});
    arguments.addOption({QStringLiteral("plugin"), QStringLiteral("Qt CAN plugin for live input"),
                         QStringLiteral("name"), defaultPlugin});
    arguments.addOption({QStringLiteral("can-interface"), QStringLiteral("CAN interface/channel"),
                         QStringLiteral("name"), QStringLiteral("can0")});
    arguments.addOption({QStringLiteral("replay"), QStringLiteral("Replay a candump file instead of live CAN"),
                         QStringLiteral("path")});
    arguments.addOption({QStringLiteral("replay-speed"), QStringLiteral("Realtime replay speed multiplier"),
                         QStringLiteral("factor"), QStringLiteral("1.0")});
    const QCommandLineOption replayFastOption(
        QStringLiteral("replay-fast"),
        QStringLiteral("Replay as quickly as possible while yielding between batches"));
    const QCommandLineOption noLogOption(
        QStringLiteral("no-log"),
        QStringLiteral("Disable decoded session logging"));
    const QCommandLineOption rawCanLogOption(
        QStringLiteral("raw-can-log"),
        QStringLiteral("Also record every raw CAN frame for diagnostics"));
    const QCommandLineOption noCanOption(
        QStringLiteral("no-can"),
        QStringLiteral("Disable CAN input (for VN300-only simulation or replay)"));
    arguments.addOption(replayFastOption);
    arguments.addOption(noLogOption);
    arguments.addOption(rawCanLogOption);
    arguments.addOption(noCanOption);
    arguments.addOption({QStringLiteral("log-directory"), QStringLiteral("Session log directory"),
                         QStringLiteral("path"), QStringLiteral("logs")});
    arguments.addOption({QStringLiteral("logging-config"),
                         QStringLiteral("Decoded-signal logging rate configuration"),
                         QStringLiteral("path"),
                         QStringLiteral("display_logging_module/SW/config/logging.json")});
    arguments.addOption({QStringLiteral("log-queue-capacity"),
                         QStringLiteral("Maximum queued log records before dropping new records"),
                         QStringLiteral("records"), QStringLiteral("65536")});
    arguments.addOption({QStringLiteral("vn300-port"),
                         QStringLiteral("VN300 serial port; omit to disable VN300 input"),
                         QStringLiteral("name")});
    arguments.addOption({QStringLiteral("vn300-baud"),
                         QStringLiteral("VN300 serial baud rate"),
                         QStringLiteral("baud"), QStringLiteral("921600")});
    arguments.addOption({QStringLiteral("vn300-replay"),
                         QStringLiteral("Replay a timestamped VN300 packet capture"),
                         QStringLiteral("path")});
    arguments.addOption({QStringLiteral("can-stale-ms"),
                         QStringLiteral("CAN receive timeout used by source health"),
                         QStringLiteral("ms"), QStringLiteral("500")});
    arguments.addOption({QStringLiteral("vn300-stale-ms"),
                         QStringLiteral("VN300 receive timeout used by source health"),
                         QStringLiteral("ms"), QStringLiteral("500")});
    arguments.process(application);

    bool speedOk = false;
    const double replaySpeed = arguments.value(QStringLiteral("replay-speed")).toDouble(&speedOk);
    if (!speedOk || replaySpeed <= 0.0) {
        qCritical() << "--replay-speed must be greater than zero";
        return 1;
    }
    bool queueCapacityOk = false;
    const qint64 queueCapacity =
        arguments.value(QStringLiteral("log-queue-capacity")).toLongLong(&queueCapacityOk);
    if (!queueCapacityOk || queueCapacity <= 0) {
        qCritical() << "--log-queue-capacity must be a positive integer";
        return 1;
    }
    bool vnBaudOk = false;
    const qint32 vnBaud = arguments.value(QStringLiteral("vn300-baud")).toInt(&vnBaudOk);
    if (!vnBaudOk || vnBaud <= 0) {
        qCritical() << "--vn300-baud must be a positive integer";
        return 1;
    }
    bool canStaleOk = false;
    bool vnStaleOk = false;
    const qint64 canStaleMs = arguments.value(QStringLiteral("can-stale-ms")).toLongLong(&canStaleOk);
    const qint64 vnStaleMs = arguments.value(QStringLiteral("vn300-stale-ms")).toLongLong(&vnStaleOk);
    if (!canStaleOk || !vnStaleOk || canStaleMs <= 0 || vnStaleMs <= 0) {
        qCritical() << "source stale timeouts must be positive integers";
        return 1;
    }
    const bool canReplayEnabled = arguments.isSet(QStringLiteral("replay"));
    const bool vnSerialEnabled = arguments.isSet(QStringLiteral("vn300-port"));
    const bool vnReplayEnabled = arguments.isSet(QStringLiteral("vn300-replay"));
    if ((canReplayEnabled && vnSerialEnabled) || (vnSerialEnabled && vnReplayEnabled)
        || (canReplayEnabled && vnReplayEnabled)) {
        qCritical() << "live/replayed sources cannot be mixed until a merged replay timeline is implemented";
        return 1;
    }
    if (vnReplayEnabled && !arguments.isSet(noCanOption)) {
        qCritical() << "--vn300-replay currently requires --no-can";
        return 1;
    }
    if (arguments.isSet(noCanOption) && canReplayEnabled) {
        qCritical() << "--no-can cannot be combined with --replay";
        return 1;
    }
    if (arguments.isSet(noCanOption) && !vnSerialEnabled && !vnReplayEnabled) {
        qCritical() << "--no-can requires a VN300 input or replay";
        return 1;
    }

    const QString dbcPath = arguments.value(QStringLiteral("dbc"));
    DbcDecoder decoder;
    QString error;
    if (!decoder.load(dbcPath, &error)) {
        qCritical().noquote() << "DBC load failed:" << error;
        return 2;
    }
    for (const QString& warning : decoder.warnings()) {
        qWarning().noquote() << "DBC warning:" << warning;
    }
    QStringList canonicalNames = decoder.canonicalSignalNames();
    canonicalNames.append(Vn300BinaryParser::canonicalSignalNames());
    canonicalNames.append(SourceHealthMonitor::canonicalSignalNames());
    QList<miata::data::SignalDefinition> signalDefinitions = decoder.signalDefinitions();
    signalDefinitions.append(Vn300BinaryParser::signalDefinitions());
    signalDefinitions.append(SourceHealthMonitor::signalDefinitions());
    qInfo() << "Loaded" << canonicalNames.size() << "canonical signals";

    DecodedLogPolicy logPolicy;
    const bool loggingEnabled = !arguments.isSet(noLogOption);
    if (loggingEnabled
        && !logPolicy.load(
            arguments.value(QStringLiteral("logging-config")),
            canonicalNames,
            &error)) {
        qCritical().noquote() << "Logging config load failed:" << error;
        return 3;
    }
    for (const QString& warning : logPolicy.warnings()) {
        qWarning().noquote() << "Logging config warning:" << warning;
    }

    AsyncSessionLogger logSink;
    if (loggingEnabled
        && !logSink.start(
            arguments.value(QStringLiteral("log-directory")),
            dbcPath,
            signalDefinitions,
            {dbcPath, arguments.value(QStringLiteral("logging-config"))},
            arguments.isSet(rawCanLogOption),
            queueCapacity,
            &error)) {
        qCritical().noquote() << "Log start failed:" << error;
        return 4;
    }
    if (logSink.isOpen()) {
        qInfo().noquote() << "MDF4 signal log:" << logSink.mdfPath();
        if (!logSink.rawCanPath().isEmpty()) {
            qInfo().noquote() << "Raw CAN diagnostic log:" << logSink.rawCanPath();
        }
    }

    SignalRegistry registry;
    SourceHealthMonitor sourceHealth;
    sourceHealth.setTimeouts(canStaleMs * 1'000'000, vnStaleMs * 1'000'000);
    sourceHealth.setCanState(!arguments.isSet(noCanOption), canReplayEnabled);
    sourceHealth.setVn300State(vnSerialEnabled || vnReplayEnabled, vnReplayEnabled);
    auto processSamples = [&](const QList<miata::data::SignalSample>& samples) {
        for (const auto& sample : samples) {
            registry.update(sample);
            if (logPolicy.shouldLog(sample)) logSink.writeSignalSample(sample);
        }
    };
    auto processRecord = [&](const CanFrameRecord& record, SignalSource source) {
        sourceHealth.noteCanReception(record.monotonicTimestampNs);
        logSink.writeRawFrame(record);

        QString decodeError;
        const auto samples = decoder.decode(
            record.frame, record.monotonicTimestampNs, &decodeError, source);
        if (!decodeError.isEmpty()) {
            sourceHealth.noteCanDecodeError();
            qWarning().noquote() << "CAN decode failed:" << decodeError;
            return;
        }
        processSamples(samples);
        if (source == SignalSource::Replay) {
            processSamples(sourceHealth.samples(record.monotonicTimestampNs));
        }
    };

    auto reportLoggerHealth = [&] {
        const quint64 droppedRecords = logSink.takeDroppedRecordCount();
        if (droppedRecords > 0) {
            qWarning() << "Logging queue overflow; dropped" << droppedRecords << "records";
        }
        const QString workerError = logSink.takeWorkerError();
        if (!workerError.isEmpty()) {
            qCritical().noquote() << "Logging worker error:" << workerError;
        }
    };

    QTimer loggerHealthTimer;
    loggerHealthTimer.setInterval(1000);
    QObject::connect(&loggerHealthTimer, &QTimer::timeout, reportLoggerHealth);
    loggerHealthTimer.start();
    QObject::connect(&application, &QCoreApplication::aboutToQuit, [&] {
        logSink.close();
        reportLoggerHealth();
    });

    QElapsedTimer acquisitionClock;
    acquisitionClock.start();
    CanBusEndpoint liveEndpoint;
    CanReplaySource replaySource;
    Vn300SerialSource vn300Source;
    Vn300ReplaySource vn300ReplaySource;
    liveEndpoint.setTimestampClock(&acquisitionClock);
    vn300Source.setTimestampClock(&acquisitionClock);
    quint64 reportedVnCrcErrors = 0;
    quint64 reportedVnFormatErrors = 0;
    QObject::connect(&loggerHealthTimer, &QTimer::timeout, &application, [&] {
        const auto& parser = vn300Source.parser();
        if (parser.crcErrorCount() != reportedVnCrcErrors) {
            qWarning() << "VN300 CRC failures:" << parser.crcErrorCount();
            reportedVnCrcErrors = parser.crcErrorCount();
        }
        if (parser.formatErrorCount() != reportedVnFormatErrors) {
            qWarning() << "VN300 unsupported/malformed packets:" << parser.formatErrorCount();
            reportedVnFormatErrors = parser.formatErrorCount();
        }
    });
    auto processVnSamples = [&](const QList<miata::data::SignalSample>& samples) {
        if (!samples.isEmpty()) sourceHealth.noteVn300Reception(samples.front().monotonicTimestampNs);
        processSamples(samples);
        if (vnReplayEnabled && !samples.isEmpty()) {
            processSamples(sourceHealth.samples(samples.front().monotonicTimestampNs));
        }
    };
    QObject::connect(&vn300Source, &Vn300SerialSource::samplesReceived,
                     &application, processVnSamples);
    QObject::connect(&vn300Source, &Vn300SerialSource::sourceError,
                     &application, [&](const QString& message) {
        sourceHealth.setVn300State(true, false);
        qCritical().noquote() << "VN300 serial error:" << message;
    });
    if (arguments.isSet(QStringLiteral("vn300-port"))) {
        if (!vn300Source.start(arguments.value(QStringLiteral("vn300-port")), vnBaud, &error)) {
            qCritical().noquote() << "VN300 serial start failed:" << error;
            return 5;
        }
        sourceHealth.setVn300State(true, true);
        qInfo() << "VN300 input:" << arguments.value(QStringLiteral("vn300-port")) << "at" << vnBaud << "baud";
    }
    QTimer sourceHealthTimer;
    sourceHealthTimer.setInterval(100);
    QObject::connect(&sourceHealthTimer, &QTimer::timeout, &application, [&] {
        if (canReplayEnabled || vnReplayEnabled) return;
        sourceHealth.setCanState(!arguments.isSet(noCanOption),
                                 canReplayEnabled ? replaySource.isRunning() : liveEndpoint.isConnected());
        sourceHealth.setVn300State(vnSerialEnabled || vnReplayEnabled,
                                   vnReplayEnabled ? vn300ReplaySource.isRunning() : vn300Source.isOpen());
        sourceHealth.setVn300ErrorCounts(vn300Source.parser().crcErrorCount(),
                                         vn300Source.parser().formatErrorCount());
        processSamples(sourceHealth.samples(acquisitionClock.nsecsElapsed()));
    });
    sourceHealthTimer.start();

    if (canReplayEnabled) {
        if (!replaySource.load(arguments.value(QStringLiteral("replay")), &error)) {
            qCritical().noquote() << "Replay load failed:" << error;
            return 5;
        }
        QObject::connect(
            &replaySource,
            &CanReplaySource::frameReceived,
            &application,
            [&](const CanFrameRecord& record) { processRecord(record, SignalSource::Replay); });
        QObject::connect(&replaySource, &CanReplaySource::replayFinished, &application, [&] {
            logSink.close();
            reportLoggerHealth();
            qInfo() << "Replay complete:" << replaySource.frameCount() << "frames";
            application.quit();
        });
        QObject::connect(
            &replaySource,
            &CanReplaySource::sourceError,
            [](const QString& message) { qCritical().noquote() << "Replay error:" << message; });

        const auto timing = arguments.isSet(replayFastOption)
            ? CanReplaySource::TimingMode::Fast
            : CanReplaySource::TimingMode::Realtime;
        if (!replaySource.start(timing, replaySpeed, &error)) {
            qCritical().noquote() << "Replay start failed:" << error;
            return 6;
        }
    } else if (vnReplayEnabled) {
        if (!vn300ReplaySource.load(arguments.value(QStringLiteral("vn300-replay")), &error)) {
            qCritical().noquote() << "VN300 replay load failed:" << error;
            return 7;
        }
        QObject::connect(&vn300ReplaySource, &Vn300ReplaySource::samplesReceived,
                         &application, processVnSamples);
        QObject::connect(&vn300ReplaySource, &Vn300ReplaySource::sourceError,
                         [](const QString& message) { qWarning().noquote() << "VN300 replay error:" << message; });
        QObject::connect(&vn300ReplaySource, &Vn300ReplaySource::replayFinished,
                         &application, [&] {
            logSink.close();
            reportLoggerHealth();
            qInfo() << "VN300 replay complete:" << vn300ReplaySource.packetCount() << "packets";
            application.quit();
        });
        const auto timing = arguments.isSet(replayFastOption)
            ? Vn300ReplaySource::TimingMode::Fast
            : Vn300ReplaySource::TimingMode::Realtime;
        if (!vn300ReplaySource.start(timing, replaySpeed, &error)) {
            qCritical().noquote() << "VN300 replay start failed:" << error;
            return 8;
        }
    } else if (!arguments.isSet(noCanOption)) {
        QObject::connect(
            &liveEndpoint,
            &CanBusEndpoint::frameReceived,
            &application,
            [&](const CanFrameRecord& record) { processRecord(record, SignalSource::Can); });
        QObject::connect(
            &liveEndpoint,
            &CanBusEndpoint::endpointError,
            [](const QString& message) { qCritical().noquote() << "CAN endpoint error:" << message; });
        if (!liveEndpoint.start(
            arguments.value(QStringLiteral("plugin")),
            arguments.value(QStringLiteral("can-interface")),
            &error)) {
            qCritical().noquote() << "CAN endpoint start failed:" << error;
            return 7;
        }
        sourceHealth.setCanState(true, true);
    }

    return application.exec();
}
