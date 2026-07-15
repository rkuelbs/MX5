#include "backend/fake_signal_source.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>

namespace miata::dash {
namespace {

QString inferredUnit(const QString& name) {
    if (name == QStringLiteral("ECM.rpm")) return QStringLiteral("RPM");
    if (name.endsWith(QStringLiteral(".clt")) || name.endsWith(QStringLiteral(".mat")))
        return QStringLiteral("deg F");
    if (name.contains(QStringLiteral("temperature"), Qt::CaseInsensitive))
        return QStringLiteral("deg C");
    if (name.contains(QStringLiteral("pressure"), Qt::CaseInsensitive)
        || name.endsWith(QStringLiteral(".map")) || name.endsWith(QStringLiteral(".baro")))
        return QStringLiteral("kPa");
    if (name.contains(QStringLiteral("latitude")) || name.contains(QStringLiteral("longitude"))
        || name.endsWith(QStringLiteral(".yaw")) || name.endsWith(QStringLiteral(".pitch"))
        || name.endsWith(QStringLiteral(".roll")) || name.contains(QStringLiteral("advance"))
        || name.contains(QStringLiteral("theta")))
        return QStringLiteral("deg");
    if (name.contains(QStringLiteral("angular_rate")) || name.contains(QStringLiteral("gyro")))
        return QStringLiteral("rad/s");
    if (name.contains(QStringLiteral("accel"))) return QStringLiteral("m/s^2");
    if (name.contains(QStringLiteral("velocity"))) return QStringLiteral("m/s");
    if (name.contains(QStringLiteral("altitude"))) return QStringLiteral("m");
    if (name.contains(QStringLiteral("time")) || name.endsWith(QStringLiteral("_age")))
        return QStringLiteral("s");
    if (name.endsWith(QStringLiteral(".batt"))) return QStringLiteral("V");
    if (name.contains(QStringLiteral("fuel_pct")) || name.endsWith(QStringLiteral(".tps")))
        return QStringLiteral("%");
    return {};
}

miata::data::SignalSource inferredSource(const QString& name) {
    if (name.startsWith(QStringLiteral("VN300."))) return miata::data::SignalSource::Vn300;
    if (name.startsWith(QStringLiteral("LOGGER."))) return miata::data::SignalSource::Derived;
    return miata::data::SignalSource::Can;
}

}  // namespace

FakeSignalSource::FakeSignalSource(QObject* parent) : SignalDataSource(parent) {
    timer_.setInterval(50);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &FakeSignalSource::generateBatch);
}

void FakeSignalSource::start() {
    QString error;
    if (definitions_.isEmpty() && !loadDefinitions(&error)) {
        emit sourceError(error);
        return;
    }
    clock_.restart();
    emit definitionsAvailable(definitions_);
    emit connectedChanged(true);
    generateBatch();
    timer_.start();
}

void FakeSignalSource::stop() {
    if (!timer_.isActive()) return;
    timer_.stop();
    emit connectedChanged(false);
}

void FakeSignalSource::generateBatch() {
    const qint64 timestampNs = clock_.nsecsElapsed();
    const double seconds = static_cast<double>(timestampNs) * 1e-9;
    QList<miata::data::SignalSample> samples;
    samples.reserve(definitions_.size());
    for (int index = 0; index < definitions_.size(); ++index) {
        const auto& definition = definitions_.at(index);
        samples.append({definition.canonicalName,
                        valueFor(definition.canonicalName, seconds, index),
                        definition.unit,
                        timestampNs,
                        inferredSource(definition.canonicalName)});
    }
    emit samplesAvailable(samples);
}

bool FakeSignalSource::loadDefinitions(QString* errorMessage) {
    QFile file(QStringLiteral(":/dash/logging.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = QStringLiteral("embedded signal catalog is unavailable");
        return false;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!document.isObject()) {
        if (errorMessage) *errorMessage = QStringLiteral("embedded signal catalog is invalid: %1")
            .arg(parseError.errorString());
        return false;
    }
    const QJsonObject configured = document.object().value(QStringLiteral("signals")).toObject();
    for (auto it = configured.begin(); it != configured.end(); ++it)
        definitions_.append({it.key(), inferredUnit(it.key())});
    if (definitions_.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("embedded signal catalog is empty");
        return false;
    }
    return true;
}

double FakeSignalSource::valueFor(const QString& name, double t, int index) const {
    const double slow = std::sin(t * 0.45);
    const double fast = std::sin(t * 1.7 + index * 0.17);
    if (name == QStringLiteral("ECM.rpm")) return 3600.0 + 2600.0 * slow;
    if (name == QStringLiteral("ECM.tps")) return 48.0 + 42.0 * slow;
    if (name == QStringLiteral("ECM.map")) return 105.0 + 70.0 * std::max(0.0, slow);
    if (name == QStringLiteral("ECM.clt")) return 190.0 + 8.0 * std::sin(t * 0.08);
    if (name == QStringLiteral("ECM.mat")) return 90.0 + 10.0 * slow;
    if (name == QStringLiteral("ECM.batt")) return 13.8 + 0.15 * fast;
    if (name.contains(QStringLiteral("afr"), Qt::CaseInsensitive)) return 13.4 + 1.2 * fast;
    if (name == QStringLiteral("ECM.fuel_pct")) return 68.0 - std::fmod(t / 120.0, 5.0);
    if (name == QStringLiteral("VN300.time_startup")) return t;
    if (name == QStringLiteral("VN300.time_gps")) return 1'450'000'000.0 + t;
    if (name == QStringLiteral("VN300.yaw")) return std::fmod(90.0 + t * 12.0, 360.0);
    if (name == QStringLiteral("VN300.pitch")) return 2.0 * slow;
    if (name == QStringLiteral("VN300.roll")) return -4.0 + fast;
    if (name == QStringLiteral("VN300.latitude")) return 39.0 + 0.0005 * std::cos(t * 0.2);
    if (name == QStringLiteral("VN300.longitude")) return -96.0 + 0.0006 * std::sin(t * 0.2);
    if (name.contains(QStringLiteral("velocity"))) return 15.0 * slow;
    if (name.contains(QStringLiteral("accel"))) return 2.5 * fast;
    if (name.endsWith(QStringLiteral("enabled")) || name.endsWith(QStringLiteral("healthy"))) return 1.0;
    if (name.endsWith(QStringLiteral("errors"))) return 0.0;
    if (name.endsWith(QStringLiteral("_age"))) return 0.02 + 0.01 * std::abs(fast);
    if (name.contains(QStringLiteral("status"), Qt::CaseInsensitive)) return 0.0;
    return static_cast<double>((index * 17) % 100) + 3.0 * fast;
}

}  // namespace miata::dash
