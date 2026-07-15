#pragma once

#include "can/can_frame_record.h"
#include "can/dbc_decoder.h"

#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVariantMap>

namespace miata::data {

class EcmSimulator final : public QObject {
    Q_OBJECT

public:
    explicit EcmSimulator(DbcDecoder* codec, QObject* parent = nullptr);

    bool loadScenario(const QString& path, QString* errorMessage = nullptr);
    bool watchScenario(const QString& path, QString* errorMessage = nullptr);
    void start();
    void stop();

    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] int rateHz() const;

signals:
    void frameGenerated(const miata::data::CanFrameRecord& record);
    void scenarioReloaded(const QString& path);
    void simulatorError(const QString& message);

private slots:
    void generateFrames();
    void scheduleScenarioReload(const QString& path);
    void reloadWatchedScenario();

private:
    static bool parseFrameId(const QJsonValue& value, quint32* frameId);
    QVariantMap valuesForCurrentTick() const;
    void applyAutomaticValues(QVariantMap* values, double elapsedSeconds) const;
    void installDefaultValues();

    DbcDecoder* codec_ = nullptr;
    QList<DbcDecoder::CanMessageInfo> messages_;
    QVariantMap defaultSignalValues_;
    QVariantMap signalValues_;
    QSet<QString> lockedSignalNames_;
    QSet<quint32> droppedFrameIds_;
    QHash<quint32, int> dlcOverrides_;
    QTimer frameTimer_;
    QTimer scenarioReloadTimer_;
    QElapsedTimer simulationClock_;
    QFileSystemWatcher scenarioWatcher_;
    QString scenarioPath_;
    int rateHz_ = 20;
    bool automaticValues_ = false;
    bool paused_ = false;
};

}  // namespace miata::data
