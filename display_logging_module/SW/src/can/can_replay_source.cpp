#include "can/can_replay_source.h"

#include "can/candump_codec.h"

#include <QFile>
#include <QTextStream>

#include <algorithm>
#include <cmath>

namespace miata::data {
namespace {

constexpr qsizetype kFastBatchSize = 1000;

}  // namespace

CanReplaySource::CanReplaySource(QObject* parent) : QObject(parent) {
    timer_.setSingleShot(true);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &CanReplaySource::emitNextBatch);
}

bool CanReplaySource::load(const QString& path, QString* errorMessage) {
    stop();
    records_.clear();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = file.errorString();
        }
        return false;
    }

    QTextStream stream(&file);
    qsizetype lineNumber = 0;
    qint64 firstTimestampNs = -1;
    qint64 previousTimestampNs = -1;
    while (!stream.atEnd()) {
        ++lineNumber;
        QString parseError;
        bool ignored = false;
        auto record = CandumpCodec::parseLine(stream.readLine(), &parseError, &ignored);
        if (ignored) {
            continue;
        }
        if (!record.has_value()) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("%1:%2: %3").arg(path).arg(lineNumber).arg(parseError);
            }
            records_.clear();
            return false;
        }
        if (previousTimestampNs > record->monotonicTimestampNs) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("%1:%2: timestamps are not monotonic")
                                    .arg(path)
                                    .arg(lineNumber);
            }
            records_.clear();
            return false;
        }
        if (firstTimestampNs < 0) {
            firstTimestampNs = record->monotonicTimestampNs;
        }
        previousTimestampNs = record->monotonicTimestampNs;
        record->monotonicTimestampNs -= firstTimestampNs;
        records_.append(*record);
    }

    if (records_.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("replay contains no CAN frames");
        }
        return false;
    }
    return true;
}

bool CanReplaySource::start(TimingMode mode, double speedFactor, QString* errorMessage) {
    if (records_.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("no replay is loaded");
        }
        return false;
    }
    if (!std::isfinite(speedFactor) || speedFactor <= 0.0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("replay speed must be greater than zero");
        }
        return false;
    }

    stop();
    timingMode_ = mode;
    speedFactor_ = speedFactor;
    nextIndex_ = 0;
    running_ = true;
    replayClock_.start();
    timer_.start(0);
    return true;
}

void CanReplaySource::stop() {
    timer_.stop();
    running_ = false;
}

qsizetype CanReplaySource::frameCount() const {
    return records_.size();
}

bool CanReplaySource::isRunning() const {
    return running_;
}

void CanReplaySource::emitNextBatch() {
    if (!running_) {
        return;
    }

    const qsizetype endIndex = timingMode_ == TimingMode::Fast
        ? std::min(nextIndex_ + kFastBatchSize, records_.size())
        : std::min(nextIndex_ + 1, records_.size());
    while (nextIndex_ < endIndex) {
        emit frameReceived(records_.at(nextIndex_));
        ++nextIndex_;
    }

    if (nextIndex_ >= records_.size()) {
        running_ = false;
        emit replayFinished();
        return;
    }
    scheduleNext();
}

void CanReplaySource::scheduleNext() {
    if (timingMode_ == TimingMode::Fast) {
        timer_.start(0);
        return;
    }

    const qint64 targetNs = static_cast<qint64>(
        static_cast<double>(records_.at(nextIndex_).monotonicTimestampNs) / speedFactor_);
    const qint64 remainingNs = std::max<qint64>(0, targetNs - replayClock_.nsecsElapsed());
    const int delayMs = static_cast<int>(std::ceil(static_cast<double>(remainingNs) / 1'000'000.0));
    timer_.start(delayMs);
}

}  // namespace miata::data
