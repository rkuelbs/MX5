#pragma once

#include "vn300/vn300_packet_record.h"

#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QTimer>

namespace miata::data {

class Vn300Simulator final : public QObject {
    Q_OBJECT

public:
    explicit Vn300Simulator(QObject* parent = nullptr);
    bool loadScenario(const QString& path, QString* errorMessage = nullptr);
    bool watchScenario(const QString& path, QString* errorMessage = nullptr);
    void start();
    void stop();
    [[nodiscard]] int rateHz() const;
    [[nodiscard]] bool isRunning() const;

signals:
    void packetGenerated(const miata::data::Vn300PacketRecord& record);
    void scenarioReloaded(const QString& path);
    void simulatorError(const QString& message);

private slots:
    void generatePacket();
    void scheduleScenarioReload(const QString& path);
    void reloadWatchedScenario();

private:
    QHash<QString, double> valuesAt(double elapsedSeconds) const;
    QByteArray encodePacket(const QHash<QString, double>& values) const;

    QTimer packetTimer_;
    QTimer reloadTimer_;
    QElapsedTimer clock_;
    QFileSystemWatcher watcher_;
    QString scenarioPath_;
    QHash<QString, double> lockedValues_;
    int rateHz_ = 50;
    double radiusM_ = 60.0;
    double speedMps_ = 15.0;
    double latitudeDeg_ = 39.0;
    double longitudeDeg_ = -96.0;
    double altitudeM_ = 300.0;
    int corruptCrcEvery_ = 0;
    int dropEvery_ = 0;
    quint64 generatedCount_ = 0;
    bool paused_ = false;
};

}  // namespace miata::data
