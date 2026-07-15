#include "can/dbc_decoder.h"
#include "logging/async_session_logger.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTextStream>
#include <QThread>

#include <cmath>

using miata::data::AsyncSessionLogger;
using miata::data::DbcDecoder;
using miata::data::SignalSample;
using miata::data::SignalSource;

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("logger_benchmark"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.3.0"));

    QCommandLineParser arguments;
    arguments.setApplicationDescription(
        QStringLiteral("Synthetic decoded-signal MDF4 throughput benchmark"));
    arguments.addHelpOption();
    arguments.addVersionOption();
    arguments.addOption({QStringLiteral("dbc"), QStringLiteral("Vehicle-wide DBC file"),
                         QStringLiteral("path"), QStringLiteral("shared/miata.dbc")});
    arguments.addOption({QStringLiteral("logging-config"),
                         QStringLiteral("Logging config embedded into the MDF4 file"),
                         QStringLiteral("path"),
                         QStringLiteral("display_logging_module/SW/config/logging.json")});
    arguments.addOption({QStringLiteral("output-directory"),
                         QStringLiteral("Benchmark output directory"),
                         QStringLiteral("path"), QStringLiteral("logs/benchmark")});
    arguments.addOption({QStringLiteral("records"),
                         QStringLiteral("Number of decoded records to generate"),
                         QStringLiteral("count"), QStringLiteral("100000")});
    arguments.addOption({QStringLiteral("aggregate-rate"),
                         QStringLiteral("Simulated aggregate timestamp rate in records/second"),
                         QStringLiteral("rate"), QStringLiteral("10000")});
    arguments.addOption({QStringLiteral("queue-capacity"),
                         QStringLiteral("Maximum asynchronous queue records"),
                         QStringLiteral("records"), QStringLiteral("65536")});
    const QCommandLineOption realtimeOption(
        QStringLiteral("realtime"),
        QStringLiteral("Pace generation in real time instead of testing maximum throughput"));
    arguments.addOption(realtimeOption);
    arguments.process(application);

    bool recordsOk = false;
    bool rateOk = false;
    bool capacityOk = false;
    const qint64 recordCount = arguments.value(QStringLiteral("records")).toLongLong(&recordsOk);
    const double aggregateRate =
        arguments.value(QStringLiteral("aggregate-rate")).toDouble(&rateOk);
    const qint64 queueCapacity =
        arguments.value(QStringLiteral("queue-capacity")).toLongLong(&capacityOk);
    if (!recordsOk || recordCount <= 0 || !rateOk || !std::isfinite(aggregateRate)
        || aggregateRate <= 0.0 || !capacityOk || queueCapacity <= 0) {
        qCritical() << "records, aggregate-rate, and queue-capacity must be positive";
        return 1;
    }

    DbcDecoder decoder;
    QString error;
    const QString dbcPath = arguments.value(QStringLiteral("dbc"));
    if (!decoder.load(dbcPath, &error)) {
        qCritical().noquote() << "DBC load failed:" << error;
        return 2;
    }
    const auto definitions = decoder.signalDefinitions();
    if (definitions.isEmpty()) {
        qCritical() << "DBC defines no signals";
        return 2;
    }

    AsyncSessionLogger logger;
    const QString loggingConfig = arguments.value(QStringLiteral("logging-config"));
    if (!logger.start(
            arguments.value(QStringLiteral("output-directory")),
            dbcPath,
            definitions,
            {dbcPath, loggingConfig},
            false,
            queueCapacity,
            &error)) {
        qCritical().noquote() << "Logger start failed:" << error;
        return 3;
    }
    const QString mdfPath = logger.mdfPath();
    const qint64 intervalNs = static_cast<qint64>(std::ceil(1.0e9 / aggregateRate));

    QElapsedTimer totalTimer;
    QElapsedTimer producerTimer;
    totalTimer.start();
    producerTimer.start();
    for (qint64 index = 0; index < recordCount; ++index) {
        const qint64 timestampNs = index * intervalNs;
        if (arguments.isSet(realtimeOption)) {
            while (producerTimer.nsecsElapsed() < timestampNs) {
                const qint64 remainingNs = timestampNs - producerTimer.nsecsElapsed();
                if (remainingNs > 200'000) {
                    QThread::usleep(static_cast<unsigned long>(remainingNs / 2'000));
                } else {
                    QThread::yieldCurrentThread();
                }
            }
        }

        const auto& definition = definitions.at(index % definitions.size());
        // A deterministic, non-constant value prevents an unrealistically easy
        // all-zero compression workload while keeping benchmark runs repeatable.
        const double value = std::sin(static_cast<double>(index) * 0.017)
            * 1000.0 + static_cast<double>(index % 997);
        logger.writeSignalSample(SignalSample{
            definition.canonicalName,
            value,
            definition.unit,
            timestampNs,
            SignalSource::Derived});
    }
    const qint64 producerElapsedNs = producerTimer.nsecsElapsed();
    logger.close();
    const qint64 totalElapsedNs = totalTimer.nsecsElapsed();

    const quint64 dropped = logger.takeDroppedRecordCount();
    const qsizetype maximumQueueDepth = logger.maximumQueueDepth();
    const QString workerError = logger.takeWorkerError();
    const qint64 fileBytes = QFileInfo(mdfPath).size();
    const double producerSeconds = static_cast<double>(producerElapsedNs) / 1.0e9;
    const double totalSeconds = static_cast<double>(totalElapsedNs) / 1.0e9;
    QTextStream output(stdout);
    output << "MDF4 path: " << mdfPath << '\n'
           << "Records requested: " << recordCount << '\n'
           << "Records dropped: " << dropped << '\n'
           << "Maximum queue depth: " << maximumQueueDepth << '\n'
           << "Producer seconds: " << producerSeconds << '\n'
           << "Total seconds including finalization: " << totalSeconds << '\n'
           << "Producer records/second: "
           << static_cast<double>(recordCount) / producerSeconds << '\n'
           << "MDF4 bytes: " << fileBytes << '\n';
    output.flush();

    if (!workerError.isEmpty()) {
        qCritical().noquote() << "Logging worker error:" << workerError;
        return 4;
    }
    return dropped == 0 ? 0 : 5;
}
