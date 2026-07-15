#pragma once

#include "can/can_frame_record.h"
#include "core/signal_definition.h"
#include "core/signal_sample.h"

#include <QList>
#include <QMutex>
#include <QString>
#include <QStringList>
#include <QWaitCondition>

#include <deque>
#include <thread>
#include <variant>

namespace miata::data {

// A bounded, non-blocking producer queue around the session sinks. The worker
// thread constructs and exclusively owns the files and drains queued records
// before shutdown.
class AsyncSessionLogger final {
public:
    AsyncSessionLogger() = default;
    ~AsyncSessionLogger();

    AsyncSessionLogger(const AsyncSessionLogger&) = delete;
    AsyncSessionLogger& operator=(const AsyncSessionLogger&) = delete;

    bool start(
        const QString& directoryPath,
        const QString& dbcPath,
        const QList<SignalDefinition>& definitions,
        const QStringList& provenanceFiles,
        bool rawCanEnabled,
        qsizetype queueCapacity,
        QString* errorMessage = nullptr);
    void close();

    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] QString rawCanPath() const;
    [[nodiscard]] QString mdfPath() const;

    bool writeRawFrame(const CanFrameRecord& record, QString* errorMessage = nullptr);
    bool writeSignalSample(const SignalSample& sample, QString* errorMessage = nullptr);

    // Returns and clears health information accumulated since the prior call.
    [[nodiscard]] quint64 takeDroppedRecordCount();
    [[nodiscard]] QString takeWorkerError();
    [[nodiscard]] qsizetype maximumQueueDepth() const;

private:
    using QueuedRecord = std::variant<CanFrameRecord, SignalSample>;

    bool enqueue(QueuedRecord record, QString* errorMessage);
    void workerMain(
        QString directoryPath,
        QString dbcPath,
        QList<SignalDefinition> definitions,
        QStringList provenanceFiles,
        bool rawCanEnabled);
    void storeWorkerError(const QString& message);

    mutable QMutex mutex_;
    QWaitCondition workAvailable_;
    QWaitCondition startupCompleteCondition_;
    std::deque<QueuedRecord> queue_;
    std::thread worker_;
    qsizetype queueCapacity_ = 0;
    qsizetype maximumQueueDepth_ = 0;
    quint64 droppedRecordCount_ = 0;
    bool rawCanEnabled_ = false;
    bool acceptingRecords_ = false;
    bool stopRequested_ = false;
    bool startupComplete_ = false;
    bool workerStarted_ = false;
    QString workerError_;
    QString rawCanPath_;
    QString mdfPath_;
};

}  // namespace miata::data
