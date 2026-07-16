#pragma once

#include "core/signal_sample.h"
#include "vn300/vn300_binary_parser.h"
#include "vn300/vn300_packet_record.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

namespace miata::data {

class Vn300ReplaySource final : public QObject {
    Q_OBJECT

public:
    enum class TimingMode { Realtime, Fast };

    explicit Vn300ReplaySource(QObject* parent = nullptr);
    bool load(const QString& path, QString* errorMessage = nullptr);
    bool start(TimingMode mode, double speedFactor = 1.0, QString* errorMessage = nullptr);
    void stop();
    void pause();
    void resume();
    bool seekToNs(qint64 positionNs, QString* errorMessage = nullptr);
    bool setSpeedFactor(double speedFactor, QString* errorMessage = nullptr);
    [[nodiscard]] qsizetype packetCount() const;
    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] bool isPaused() const;
    [[nodiscard]] qint64 positionNs() const;
    [[nodiscard]] qint64 durationNs() const;
    [[nodiscard]] double speedFactor() const;

signals:
    void samplesReceived(const QList<miata::data::SignalSample>& samples);
    void replayFinished();
    void sourceError(const QString& message);

private slots:
    void emitNextBatch();

private:
    void scheduleNext();

    QList<Vn300PacketRecord> records_;
    qsizetype nextIndex_ = 0;
    TimingMode timingMode_ = TimingMode::Realtime;
    double speedFactor_ = 1.0;
    qint64 playbackBaseNs_ = 0;
    QElapsedTimer replayClock_;
    QTimer timer_;
    Vn300BinaryParser parser_;
    bool running_ = false;
    bool paused_ = false;
};

}  // namespace miata::data
