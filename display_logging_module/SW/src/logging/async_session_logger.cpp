#include "logging/async_session_logger.h"

#include "logging/mdf4_log_sink.h"
#include "logging/session_log_sink.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutexLocker>

#include <algorithm>
#include <chrono>
#include <type_traits>
#include <utility>

namespace miata::data {

AsyncSessionLogger::~AsyncSessionLogger() {
    close();
}

bool AsyncSessionLogger::start(
    const QString& directoryPath,
    const QString& dbcPath,
    const QList<SignalDefinition>& definitions,
    const QStringList& provenanceFiles,
    bool rawCanEnabled,
    qsizetype queueCapacity,
    QString* errorMessage) {
    close();
    if (queueCapacity <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("logging queue capacity must be greater than zero");
        }
        return false;
    }

    QMutexLocker locker(&mutex_);
    queueCapacity_ = queueCapacity;
    maximumQueueDepth_ = 0;
    droppedRecordCount_ = 0;
    rawCanEnabled_ = rawCanEnabled;
    stopRequested_ = false;
    startupComplete_ = false;
    workerStarted_ = false;
    workerError_.clear();
    rawCanPath_.clear();
    mdfPath_.clear();
    worker_ = std::thread(
        &AsyncSessionLogger::workerMain,
        this,
        directoryPath,
        dbcPath,
        definitions,
        provenanceFiles,
        rawCanEnabled);
    while (!startupComplete_) {
        startupCompleteCondition_.wait(&mutex_);
    }

    const bool started = workerStarted_;
    const QString startupError = workerError_;
    locker.unlock();
    if (!started && worker_.joinable()) {
        worker_.join();
    }
    if (!started && errorMessage != nullptr) {
        *errorMessage = startupError;
    }
    return started;
}

void AsyncSessionLogger::close() {
    {
        QMutexLocker locker(&mutex_);
        acceptingRecords_ = false;
        stopRequested_ = true;
        workAvailable_.wakeOne();
    }
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool AsyncSessionLogger::isOpen() const {
    QMutexLocker locker(&mutex_);
    return workerStarted_;
}

QString AsyncSessionLogger::rawCanPath() const {
    QMutexLocker locker(&mutex_);
    return rawCanPath_;
}

QString AsyncSessionLogger::mdfPath() const {
    QMutexLocker locker(&mutex_);
    return mdfPath_;
}

bool AsyncSessionLogger::writeRawFrame(
    const CanFrameRecord& record,
    QString* errorMessage) {
    {
        QMutexLocker locker(&mutex_);
        if (!rawCanEnabled_) {
            return true;
        }
    }
    return enqueue(record, errorMessage);
}

bool AsyncSessionLogger::writeSignalSample(
    const SignalSample& sample,
    QString* errorMessage) {
    return enqueue(sample, errorMessage);
}

quint64 AsyncSessionLogger::takeDroppedRecordCount() {
    QMutexLocker locker(&mutex_);
    return std::exchange(droppedRecordCount_, 0);
}

QString AsyncSessionLogger::takeWorkerError() {
    QMutexLocker locker(&mutex_);
    return std::exchange(workerError_, QString{});
}

qsizetype AsyncSessionLogger::maximumQueueDepth() const {
    QMutexLocker locker(&mutex_);
    return maximumQueueDepth_;
}

bool AsyncSessionLogger::enqueue(QueuedRecord record, QString* errorMessage) {
    QMutexLocker locker(&mutex_);
    if (!acceptingRecords_) {
        return true;
    }
    if (static_cast<qsizetype>(queue_.size()) >= queueCapacity_) {
        ++droppedRecordCount_;
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("logging queue full; record dropped");
        }
        return false;
    }
    queue_.push_back(std::move(record));
    maximumQueueDepth_ = std::max(
        maximumQueueDepth_, static_cast<qsizetype>(queue_.size()));
    workAvailable_.wakeOne();
    return true;
}

void AsyncSessionLogger::workerMain(
    QString directoryPath,
    QString dbcPath,
    QList<SignalDefinition> definitions,
    QStringList provenanceFiles,
    bool rawCanEnabled) {
    QDir directory(directoryPath);
    QString error;
    bool opened = directory.exists() || directory.mkpath(QStringLiteral("."));
    if (!opened) {
        error = QStringLiteral("could not create log directory %1").arg(directoryPath);
    }

    const QString stem = QStringLiteral("vehicle_%1")
                             .arg(QDateTime::currentDateTimeUtc().toString(
                                 QStringLiteral("yyyyMMdd_HHmmss_zzz")));
    const QString rawPath = rawCanEnabled
        ? directory.filePath(stem + QStringLiteral(".can.log"))
        : QString{};
    const QString measurementPath = directory.filePath(stem + QStringLiteral(".mf4"));
    const QString activeMarkerPath = directory.filePath(stem + QStringLiteral(".active"));

    if (opened) {
        QFile marker(activeMarkerPath);
        opened = marker.open(QIODevice::WriteOnly | QIODevice::NewOnly)
            && marker.write("active\n") == 7;
        if (!opened) {
            error = QStringLiteral("could not create active-session marker %1").arg(activeMarkerPath);
        }
    }

    SessionLogSink rawSink;
    if (opened && rawCanEnabled) {
        opened = rawSink.openFiles(rawPath, {}, dbcPath, &error);
    }
    Mdf4LogSink mdfSink;
    if (opened) {
        opened = mdfSink.open(measurementPath, definitions, provenanceFiles, &error);
    }
    if (!opened) {
        rawSink.close();
    }
    {
        QMutexLocker locker(&mutex_);
        workerStarted_ = opened;
        acceptingRecords_ = opened;
        startupComplete_ = true;
        if (opened) {
            rawCanPath_ = rawPath;
            mdfPath_ = measurementPath;
        } else {
            workerError_ = error;
        }
        startupCompleteCondition_.wakeOne();
    }
    if (!opened) {
        return;
    }

    using Clock = std::chrono::steady_clock;
    constexpr auto flushInterval = std::chrono::seconds(1);
    auto nextFlush = Clock::now() + flushInterval;

    while (true) {
        QueuedRecord record;
        bool haveRecord = false;
        {
            QMutexLocker locker(&mutex_);
            while (queue_.empty() && !stopRequested_) {
                const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                    nextFlush - Clock::now());
                if (remaining.count() <= 0) {
                    break;
                }
                workAvailable_.wait(&mutex_, static_cast<unsigned long>(remaining.count()));
            }
            if (!queue_.empty()) {
                record = std::move(queue_.front());
                queue_.pop_front();
                haveRecord = true;
            } else if (stopRequested_) {
                break;
            }
        }

        if (haveRecord) {
            bool written = std::visit(
                [&](const auto& value) {
                    using RecordType = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<RecordType, CanFrameRecord>) {
                        return rawSink.writeRawFrame(value, &error);
                    } else {
                        return mdfSink.writeSignalSample(value, &error);
                    }
                },
                record);
            if (!written) {
                storeWorkerError(error);
            }
        }

        if (Clock::now() >= nextFlush) {
            if (!rawSink.flush(&error)) {
                storeWorkerError(error);
            }
            nextFlush = Clock::now() + flushInterval;
        }
    }

    bool finalized = true;
    if (!rawSink.flush(&error)) {
        storeWorkerError(error);
        finalized = false;
    }
    if (!mdfSink.close(&error)) {
        storeWorkerError(error);
        finalized = false;
    }
    rawSink.close();
    if (finalized && !QFile::remove(activeMarkerPath)) {
        storeWorkerError(QStringLiteral("could not remove active-session marker %1")
                             .arg(activeMarkerPath));
    }
    QMutexLocker locker(&mutex_);
    workerStarted_ = false;
}

void AsyncSessionLogger::storeWorkerError(const QString& message) {
    QMutexLocker locker(&mutex_);
    workerError_ = message;
}

}  // namespace miata::data
