#pragma once

#include "can/can_frame_record.h"

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

namespace miata::data {

class CanReplaySource final : public QObject {
    Q_OBJECT

public:
    enum class TimingMode {
        Realtime,
        Fast,
    };

    explicit CanReplaySource(QObject* parent = nullptr);

    bool load(const QString& path, QString* errorMessage = nullptr);
    bool start(TimingMode mode, double speedFactor = 1.0, QString* errorMessage = nullptr);
    void stop();

    [[nodiscard]] qsizetype frameCount() const;
    [[nodiscard]] bool isRunning() const;

signals:
    void frameReceived(const miata::data::CanFrameRecord& record);
    void replayFinished();
    void sourceError(const QString& message);

private slots:
    void emitNextBatch();

private:
    void scheduleNext();

    QList<CanFrameRecord> records_;
    qsizetype nextIndex_ = 0;
    TimingMode timingMode_ = TimingMode::Realtime;
    double speedFactor_ = 1.0;
    QElapsedTimer replayClock_;
    QTimer timer_;
    bool running_ = false;
};

}  // namespace miata::data
