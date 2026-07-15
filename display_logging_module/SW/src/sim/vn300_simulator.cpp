#include "sim/vn300_simulator.h"

#include "vn300/vn300_binary_parser.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>

#include <bit>
#include <algorithm>
#include <cmath>

namespace miata::data {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthRadiusM = 6'378'137.0;
constexpr int kReloadDebounceMs = 100;

template <typename T>
void appendLittle(QByteArray& packet, T value) {
    if constexpr (sizeof(T) == 2) {
        const quint16 bits = qToLittleEndian(std::bit_cast<quint16>(value));
        packet.append(reinterpret_cast<const char*>(&bits), sizeof(bits));
    } else if constexpr (sizeof(T) == 4) {
        const quint32 bits = qToLittleEndian(std::bit_cast<quint32>(value));
        packet.append(reinterpret_cast<const char*>(&bits), sizeof(bits));
    } else if constexpr (sizeof(T) == 8) {
        const quint64 bits = qToLittleEndian(std::bit_cast<quint64>(value));
        packet.append(reinterpret_cast<const char*>(&bits), sizeof(bits));
    }
}

double value(const QHash<QString, double>& values, const char* name) {
    return values.value(QString::fromLatin1(name));
}

}  // namespace

Vn300Simulator::Vn300Simulator(QObject* parent) : QObject(parent) {
    packetTimer_.setTimerType(Qt::PreciseTimer);
    reloadTimer_.setSingleShot(true);
    connect(&packetTimer_, &QTimer::timeout, this, &Vn300Simulator::generatePacket);
    connect(&reloadTimer_, &QTimer::timeout, this, &Vn300Simulator::reloadWatchedScenario);
    connect(&watcher_, &QFileSystemWatcher::fileChanged,
            this, &Vn300Simulator::scheduleScenarioReload);
}

bool Vn300Simulator::loadScenario(const QString& path, QString* errorMessage) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!document.isObject()) {
        if (errorMessage) *errorMessage = QStringLiteral("invalid JSON at offset %1: %2")
            .arg(parseError.offset).arg(parseError.errorString());
        return false;
    }
    const QJsonObject root = document.object();
    const int rate = root.value(QStringLiteral("rate_hz")).toInt(50);
    if (rate < 1 || rate > 400) {
        if (errorMessage) *errorMessage = QStringLiteral("rate_hz must be between 1 and 400");
        return false;
    }
    const QJsonObject circle = root.value(QStringLiteral("circle")).toObject();
    const double radius = circle.value(QStringLiteral("radius_m")).toDouble(60.0);
    const double speed = circle.value(QStringLiteral("speed_mps")).toDouble(15.0);
    if (!std::isfinite(radius) || !std::isfinite(speed) || radius <= 0.0 || speed < 0.0) {
        if (errorMessage) *errorMessage = QStringLiteral("circle radius must be positive and speed nonnegative");
        return false;
    }

    const QStringList knownNames = Vn300BinaryParser::canonicalSignalNames();
    const QSet<QString> known(knownNames.cbegin(), knownNames.cend());
    QHash<QString, double> locks;
    const QJsonObject signalOverrides = root.value(QStringLiteral("signals")).toObject();
    for (auto it = signalOverrides.begin(); it != signalOverrides.end(); ++it) {
        if (!known.contains(it.key()) || !it.value().isDouble()) {
            if (errorMessage) *errorMessage = QStringLiteral("unknown or nonnumeric VN300 signal '%1'")
                .arg(it.key());
            return false;
        }
        locks.insert(it.key(), it.value().toDouble());
    }
    const QJsonObject faults = root.value(QStringLiteral("faults")).toObject();
    const int corruptEvery = faults.value(QStringLiteral("corrupt_crc_every")).toInt(0);
    const int dropEvery = faults.value(QStringLiteral("drop_every")).toInt(0);
    if (corruptEvery < 0 || dropEvery < 0) {
        if (errorMessage) *errorMessage = QStringLiteral("fault intervals cannot be negative");
        return false;
    }

    rateHz_ = rate;
    radiusM_ = radius;
    speedMps_ = speed;
    latitudeDeg_ = circle.value(QStringLiteral("origin_latitude_deg")).toDouble(39.0);
    longitudeDeg_ = circle.value(QStringLiteral("origin_longitude_deg")).toDouble(-96.0);
    altitudeM_ = circle.value(QStringLiteral("altitude_m")).toDouble(300.0);
    lockedValues_ = locks;
    corruptCrcEvery_ = corruptEvery;
    dropEvery_ = dropEvery;
    paused_ = faults.value(QStringLiteral("paused")).toBool(false);
    if (packetTimer_.isActive()) packetTimer_.setInterval(std::max(1, 1000 / rateHz_));
    return true;
}

bool Vn300Simulator::watchScenario(const QString& path, QString* errorMessage) {
    if (!loadScenario(path, errorMessage)) return false;
    scenarioPath_ = path;
    if (!watcher_.files().isEmpty()) watcher_.removePaths(watcher_.files());
    if (!watcher_.addPath(path)) {
        if (errorMessage) *errorMessage = QStringLiteral("could not watch scenario file %1").arg(path);
        return false;
    }
    return true;
}

void Vn300Simulator::start() {
    generatedCount_ = 0;
    clock_.restart();
    packetTimer_.start(std::max(1, 1000 / rateHz_));
}

void Vn300Simulator::stop() { packetTimer_.stop(); }
int Vn300Simulator::rateHz() const { return rateHz_; }
bool Vn300Simulator::isRunning() const { return packetTimer_.isActive(); }

void Vn300Simulator::generatePacket() {
    if (paused_) return;
    ++generatedCount_;
    if (dropEvery_ > 0 && generatedCount_ % static_cast<quint64>(dropEvery_) == 0) return;
    const qint64 timestamp = clock_.nsecsElapsed();
    QByteArray packet = encodePacket(valuesAt(static_cast<double>(timestamp) * 1e-9));
    if (corruptCrcEvery_ > 0
        && generatedCount_ % static_cast<quint64>(corruptCrcEvery_) == 0
        && packet.size() > 8) {
        packet[packet.size() - 3] = static_cast<char>(packet.at(packet.size() - 3) ^ 0x01);
    }
    emit packetGenerated({packet, timestamp});
}

void Vn300Simulator::scheduleScenarioReload(const QString&) {
    reloadTimer_.start(kReloadDebounceMs);
}

void Vn300Simulator::reloadWatchedScenario() {
    QString error;
    if (!loadScenario(scenarioPath_, &error)) emit simulatorError(QStringLiteral("scenario reload failed: %1").arg(error));
    else emit scenarioReloaded(scenarioPath_);
    if (!watcher_.files().contains(scenarioPath_)) watcher_.addPath(scenarioPath_);
}

QHash<QString, double> Vn300Simulator::valuesAt(double time) const {
    QHash<QString, double> v;
    const double omega = speedMps_ / radiusM_;
    const double angle = omega * time;
    const double north = radiusM_ * std::cos(angle);
    const double east = radiusM_ * std::sin(angle);
    const double velNorth = -speedMps_ * std::sin(angle);
    const double velEast = speedMps_ * std::cos(angle);
    const double yaw = std::atan2(velEast, velNorth) * 180.0 / kPi;
    const double yawRad = yaw * kPi / 180.0;
    const double centripetal = radiusM_ > 0.0 ? speedMps_ * speedMps_ / radiusM_ : 0.0;

    v.insert(QStringLiteral("VN300.time_startup"), time);
    v.insert(QStringLiteral("VN300.time_gps"), 1'450'000'000.0 + time);
    v.insert(QStringLiteral("VN300.time_sync_in"), std::fmod(time, 1.0));
    v.insert(QStringLiteral("VN300.yaw"), yaw);
    v.insert(QStringLiteral("VN300.pitch"), 1.5 * std::sin(angle * 2.0));
    v.insert(QStringLiteral("VN300.roll"), -5.0 * std::min(1.0, centripetal / 9.80665));
    v.insert(QStringLiteral("VN300.quat_x"), 0.0);
    v.insert(QStringLiteral("VN300.quat_y"), 0.0);
    v.insert(QStringLiteral("VN300.quat_z"), std::sin(yawRad / 2.0));
    v.insert(QStringLiteral("VN300.quat_s"), std::cos(yawRad / 2.0));
    v.insert(QStringLiteral("VN300.angular_rate_x"), 0.0);
    v.insert(QStringLiteral("VN300.angular_rate_y"), 0.0);
    v.insert(QStringLiteral("VN300.angular_rate_z"), omega);
    v.insert(QStringLiteral("VN300.latitude"), latitudeDeg_ + north / kEarthRadiusM * 180.0 / kPi);
    v.insert(QStringLiteral("VN300.longitude"), longitudeDeg_ + east / (kEarthRadiusM * std::cos(latitudeDeg_ * kPi / 180.0)) * 180.0 / kPi);
    v.insert(QStringLiteral("VN300.altitude"), altitudeM_ + 1.5 * std::sin(angle * 0.5));
    v.insert(QStringLiteral("VN300.velocity_north"), velNorth);
    v.insert(QStringLiteral("VN300.velocity_east"), velEast);
    v.insert(QStringLiteral("VN300.velocity_down"), 0.0);
    for (const QString& prefix : {QStringLiteral("VN300.accel"), QStringLiteral("VN300.uncomp_accel")}) {
        v.insert(prefix + QStringLiteral("_x"), 0.15 * std::sin(time * 2.0));
        v.insert(prefix + QStringLiteral("_y"), centripetal);
        v.insert(prefix + QStringLiteral("_z"), -9.80665);
    }
    v.insert(QStringLiteral("VN300.uncomp_gyro_x"), 0.0);
    v.insert(QStringLiteral("VN300.uncomp_gyro_y"), 0.0);
    v.insert(QStringLiteral("VN300.uncomp_gyro_z"), omega);
    v.insert(QStringLiteral("VN300.mag_x"), 0.22 * std::cos(yawRad));
    v.insert(QStringLiteral("VN300.mag_y"), -0.22 * std::sin(yawRad));
    v.insert(QStringLiteral("VN300.mag_z"), 0.42);
    v.insert(QStringLiteral("VN300.temperature"), 32.0 + 2.0 * std::sin(time / 30.0));
    v.insert(QStringLiteral("VN300.pressure"), 98.5 + 0.1 * std::sin(time / 20.0));
    const double dt = 1.0 / rateHz_;
    v.insert(QStringLiteral("VN300.delta_time"), dt);
    v.insert(QStringLiteral("VN300.delta_theta_x"), 0.0);
    v.insert(QStringLiteral("VN300.delta_theta_y"), 0.0);
    v.insert(QStringLiteral("VN300.delta_theta_z"), omega * dt * 180.0 / kPi);
    v.insert(QStringLiteral("VN300.delta_velocity_x"), 0.0);
    v.insert(QStringLiteral("VN300.delta_velocity_y"), centripetal * dt);
    v.insert(QStringLiteral("VN300.delta_velocity_z"), 0.0);
    v.insert(QStringLiteral("VN300.ins_status"), 0x0006);
    v.insert(QStringLiteral("VN300.sync_in_count"), std::floor(time));
    v.insert(QStringLiteral("VN300.time_gps_pps"), std::fmod(time, 1.0));
    v.insert(QStringLiteral("VN300.lin_body_accel_x"), 0.15 * std::sin(time * 2.0));
    v.insert(QStringLiteral("VN300.lin_body_accel_y"), centripetal);
    v.insert(QStringLiteral("VN300.lin_body_accel_z"), 0.0);
    for (auto it = lockedValues_.cbegin(); it != lockedValues_.cend(); ++it) v.insert(it.key(), it.value());
    return v;
}

QByteArray Vn300Simulator::encodePacket(const QHash<QString, double>& v) const {
    QByteArray p;
    p.append(static_cast<char>(0xFA));
    p.append(static_cast<char>(0x11));
    appendLittle<quint16>(p, 0x7FFF);
    appendLittle<quint16>(p, 0x0040);
    for (const char* name : {"VN300.time_startup", "VN300.time_gps", "VN300.time_sync_in"})
        appendLittle<quint64>(p, static_cast<quint64>(value(v, name) * 1e9));
    for (const char* name : {"VN300.yaw", "VN300.pitch", "VN300.roll",
                             "VN300.quat_x", "VN300.quat_y", "VN300.quat_z", "VN300.quat_s",
                             "VN300.angular_rate_x", "VN300.angular_rate_y", "VN300.angular_rate_z"})
        appendLittle<float>(p, static_cast<float>(value(v, name)));
    for (const char* name : {"VN300.latitude", "VN300.longitude", "VN300.altitude"})
        appendLittle<double>(p, value(v, name));
    for (const char* name : {"VN300.velocity_north", "VN300.velocity_east", "VN300.velocity_down",
                             "VN300.accel_x", "VN300.accel_y", "VN300.accel_z",
                             "VN300.uncomp_accel_x", "VN300.uncomp_accel_y", "VN300.uncomp_accel_z",
                             "VN300.uncomp_gyro_x", "VN300.uncomp_gyro_y", "VN300.uncomp_gyro_z",
                             "VN300.mag_x", "VN300.mag_y", "VN300.mag_z",
                             "VN300.temperature", "VN300.pressure",
                             "VN300.delta_time", "VN300.delta_theta_x", "VN300.delta_theta_y",
                             "VN300.delta_theta_z", "VN300.delta_velocity_x",
                             "VN300.delta_velocity_y", "VN300.delta_velocity_z"})
        appendLittle<float>(p, static_cast<float>(value(v, name)));
    appendLittle<quint16>(p, static_cast<quint16>(value(v, "VN300.ins_status")));
    appendLittle<quint32>(p, static_cast<quint32>(value(v, "VN300.sync_in_count")));
    appendLittle<quint64>(p, static_cast<quint64>(value(v, "VN300.time_gps_pps") * 1e9));
    for (const char* name : {"VN300.lin_body_accel_x", "VN300.lin_body_accel_y", "VN300.lin_body_accel_z"})
        appendLittle<float>(p, static_cast<float>(value(v, name)));
    const quint16 crc = Vn300BinaryParser::calculateCrc(p.constData() + 1, p.size() - 1);
    p.append(static_cast<char>((crc >> 8) & 0xFFU));
    p.append(static_cast<char>(crc & 0xFFU));
    return p;
}

}  // namespace miata::data
