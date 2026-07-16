#include "vn300/vn300_replay_source.h"

#include "vn300/vn300_log_codec.h"

#include <QFile>
#include <QTextStream>

#include <algorithm>
#include <cmath>

namespace miata::data {
namespace {
constexpr qsizetype kFastBatchSize = 500;
}

Vn300ReplaySource::Vn300ReplaySource(QObject* parent) : QObject(parent) {
    timer_.setSingleShot(true);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &Vn300ReplaySource::emitNextBatch);
}

bool Vn300ReplaySource::load(const QString& path, QString* errorMessage) {
    stop();
    records_.clear();
    playbackBaseNs_ = 0;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }
    QTextStream stream(&file);
    qint64 firstTimestamp = -1;
    qint64 previousTimestamp = -1;
    qsizetype lineNumber = 0;
    while (!stream.atEnd()) {
        ++lineNumber;
        QString parseError;
        bool ignored = false;
        auto record = Vn300LogCodec::parseLine(stream.readLine(), &parseError, &ignored);
        if (ignored) continue;
        if (!record || record->monotonicTimestampNs < previousTimestamp) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("%1:%2: %3")
                    .arg(path).arg(lineNumber)
                    .arg(record ? QStringLiteral("timestamps are not monotonic") : parseError);
            }
            records_.clear();
            return false;
        }
        if (firstTimestamp < 0) firstTimestamp = record->monotonicTimestampNs;
        previousTimestamp = record->monotonicTimestampNs;
        record->monotonicTimestampNs -= firstTimestamp;
        records_.append(*record);
    }
    if (records_.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("replay contains no VN300 packets");
        return false;
    }
    return true;
}

bool Vn300ReplaySource::start(TimingMode mode, double speedFactor, QString* errorMessage) {
    if (records_.isEmpty() || !std::isfinite(speedFactor) || speedFactor <= 0.0) {
        if (errorMessage) *errorMessage = records_.isEmpty()
            ? QStringLiteral("no VN300 replay is loaded")
            : QStringLiteral("replay speed must be greater than zero");
        return false;
    }
    stop();
    parser_.reset();
    timingMode_ = mode;
    speedFactor_ = speedFactor;
    nextIndex_ = 0;
    playbackBaseNs_ = 0;
    running_ = true;
    paused_ = false;
    replayClock_.start();
    timer_.start(0);
    return true;
}

void Vn300ReplaySource::stop() { timer_.stop(); running_ = false; paused_ = false; }
void Vn300ReplaySource::pause() {
    if (!running_ || paused_) return;
    playbackBaseNs_ = positionNs();
    timer_.stop();
    paused_ = true;
}
void Vn300ReplaySource::resume() {
    if (!running_ || !paused_) return;
    if (nextIndex_ >= records_.size()) {
        running_ = false;
        paused_ = false;
        emit replayFinished();
        return;
    }
    paused_ = false;
    replayClock_.restart();
    scheduleNext();
}
bool Vn300ReplaySource::seekToNs(qint64 position, QString* errorMessage) {
    if (records_.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("no replay is loaded");
        return false;
    }
    const qint64 bounded = std::clamp<qint64>(position, 0, durationNs());
    playbackBaseNs_ = bounded;
    nextIndex_ = bounded >= durationNs() ? records_.size()
        : std::lower_bound(records_.cbegin(), records_.cend(), bounded,
              [](const Vn300PacketRecord& record, qint64 timestamp) {
                  return record.monotonicTimestampNs < timestamp;
              }) - records_.cbegin();
    parser_.reset();
    timer_.stop();
    if (running_ && !paused_) {
        replayClock_.restart();
        if (nextIndex_ >= records_.size()) {
            running_ = false;
            emit replayFinished();
        } else {
            scheduleNext();
        }
    }
    return true;
}
bool Vn300ReplaySource::setSpeedFactor(double speedFactor, QString* errorMessage) {
    if (!std::isfinite(speedFactor) || speedFactor <= 0.0 || speedFactor > 100.0) {
        if (errorMessage) *errorMessage = QStringLiteral("replay speed must be greater than zero and at most 100");
        return false;
    }
    const qint64 currentPosition = positionNs();
    speedFactor_ = speedFactor;
    playbackBaseNs_ = currentPosition;
    if (running_ && !paused_) {
        replayClock_.restart();
        timer_.stop();
        scheduleNext();
    }
    return true;
}
qsizetype Vn300ReplaySource::packetCount() const { return records_.size(); }
bool Vn300ReplaySource::isRunning() const { return running_; }
bool Vn300ReplaySource::isPaused() const { return paused_; }
qint64 Vn300ReplaySource::positionNs() const {
    if (!running_ || paused_) return playbackBaseNs_;
    const qint64 elapsedMediaNs = static_cast<qint64>(
        static_cast<double>(replayClock_.nsecsElapsed()) * speedFactor_);
    return std::min(durationNs(), playbackBaseNs_ + elapsedMediaNs);
}
qint64 Vn300ReplaySource::durationNs() const {
    return records_.isEmpty() ? 0 : records_.back().monotonicTimestampNs;
}
double Vn300ReplaySource::speedFactor() const { return speedFactor_; }

void Vn300ReplaySource::emitNextBatch() {
    if (!running_) return;
    const qsizetype end = timingMode_ == TimingMode::Fast
        ? std::min(nextIndex_ + kFastBatchSize, records_.size())
        : std::min(nextIndex_ + 1, records_.size());
    while (nextIndex_ < end) {
        const auto& record = records_.at(nextIndex_++);
        const auto samples = parser_.consume(record.packet, record.monotonicTimestampNs);
        if (samples.isEmpty()) {
            emit sourceError(QStringLiteral("VN300 replay packet failed CRC or format validation"));
        } else {
            emit samplesReceived(samples);
        }
    }
    if (nextIndex_ >= records_.size()) {
        playbackBaseNs_ = durationNs();
        running_ = false;
        emit replayFinished();
        return;
    }
    scheduleNext();
}

void Vn300ReplaySource::scheduleNext() {
    if (timingMode_ == TimingMode::Fast) { timer_.start(0); return; }
    const qint64 relativeMediaNs = std::max<qint64>(
        0, records_.at(nextIndex_).monotonicTimestampNs - playbackBaseNs_);
    const qint64 targetNs = static_cast<qint64>(
        static_cast<double>(relativeMediaNs) / speedFactor_);
    const qint64 remaining = std::max<qint64>(0, targetNs - replayClock_.nsecsElapsed());
    timer_.start(static_cast<int>(std::ceil(static_cast<double>(remaining) / 1'000'000.0)));
}

}  // namespace miata::data
