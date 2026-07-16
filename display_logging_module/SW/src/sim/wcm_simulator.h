#pragma once

#include "can/can_frame_record.h"
#include "can/dbc_decoder.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include <array>

namespace miata::data {

class WcmSimulator final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int inputs READ inputs NOTIFY stateChanged)
    Q_PROPERTY(int counter READ counter NOTIFY stateChanged)
    Q_PROPERTY(int boostEncoder READ boostEncoder WRITE setBoostEncoder NOTIFY stateChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY stateChanged)
    Q_PROPERTY(bool dropStatus READ dropStatus WRITE setDropStatus NOTIFY stateChanged)
    Q_PROPERTY(bool dropEvents READ dropEvents WRITE setDropEvents NOTIFY stateChanged)
    Q_PROPERTY(bool freezeCounter READ freezeCounter WRITE setFreezeCounter NOTIFY stateChanged)
    Q_PROPERTY(int counterStep READ counterStep WRITE setCounterStep NOTIFY stateChanged)
    Q_PROPERTY(int statusFramesGenerated READ statusFramesGenerated NOTIFY statisticsChanged)
    Q_PROPERTY(int eventFramesGenerated READ eventFramesGenerated NOTIFY statisticsChanged)
    Q_PROPERTY(QString lastEvent READ lastEvent NOTIFY statisticsChanged)

public:
    explicit WcmSimulator(DbcDecoder* codec, QObject* parent = nullptr);

    bool setStatusRateHz(int rateHz, QString* errorMessage = nullptr);
    void start();
    void stop();
    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] int statusRateHz() const;

    [[nodiscard]] int inputs() const;
    [[nodiscard]] int counter() const;
    [[nodiscard]] int boostEncoder() const;
    [[nodiscard]] bool paused() const;
    [[nodiscard]] bool dropStatus() const;
    [[nodiscard]] bool dropEvents() const;
    [[nodiscard]] bool freezeCounter() const;
    [[nodiscard]] int counterStep() const;
    [[nodiscard]] int statusFramesGenerated() const;
    [[nodiscard]] int eventFramesGenerated() const;
    [[nodiscard]] QString lastEvent() const;

    Q_INVOKABLE void setButtonPressed(int buttonId, bool pressed);
    Q_INVOKABLE bool buttonPressed(int buttonId) const;
    Q_INVOKABLE void releaseAllButtons();
    Q_INVOKABLE void sendStatusNow();
    Q_INVOKABLE void setCounter(int counter);

    void setBoostEncoder(int value);
    void setPaused(bool paused);
    void setDropStatus(bool drop);
    void setDropEvents(bool drop);
    void setFreezeCounter(bool freeze);
    void setCounterStep(int step);

signals:
    void frameGenerated(const miata::data::CanFrameRecord& record);
    void stateChanged();
    void statisticsChanged();
    void simulatorError(const QString& message);

private:
    void generateEvent(int buttonId, bool pressed, int lengthMs);
    void generateStatus();
    void emitEncodedFrame(quint32 frameId, const QVariantMap& values, bool eventFrame);
    static QString buttonName(int buttonId);

    DbcDecoder* codec_ = nullptr;
    QTimer statusTimer_;
    QElapsedTimer clock_;
    std::array<qint64, 8> pressStartedNs_{};
    quint8 inputs_ = 0;
    quint8 counter_ = 0;
    quint8 boostEncoder_ = 0;
    int statusRateHz_ = 20;
    int counterStep_ = 1;
    int statusFramesGenerated_ = 0;
    int eventFramesGenerated_ = 0;
    bool paused_ = false;
    bool dropStatus_ = false;
    bool dropEvents_ = false;
    bool freezeCounter_ = false;
    QString lastEvent_ = QStringLiteral("None");
};

}  // namespace miata::data
