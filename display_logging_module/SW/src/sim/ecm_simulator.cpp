#include "sim/ecm_simulator.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>

namespace miata::data {
namespace {

constexpr int kScenarioReloadDebounceMs = 100;

}  // namespace

EcmSimulator::EcmSimulator(DbcDecoder* codec, QObject* parent)
    : QObject(parent), codec_(codec) {
    if (codec_ != nullptr) {
        messages_ = codec_->messageInfos(QStringLiteral("ECM"));
    }
    installDefaultValues();
    frameTimer_.setTimerType(Qt::PreciseTimer);
    scenarioReloadTimer_.setSingleShot(true);
    connect(&frameTimer_, &QTimer::timeout, this, &EcmSimulator::generateFrames);
    connect(&scenarioReloadTimer_, &QTimer::timeout, this, &EcmSimulator::reloadWatchedScenario);
    connect(
        &scenarioWatcher_,
        &QFileSystemWatcher::fileChanged,
        this,
        &EcmSimulator::scheduleScenarioReload);
}

bool EcmSimulator::loadScenario(const QString& path, QString* errorMessage) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = file.errorString();
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (document.isNull() || !document.isObject()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("invalid JSON at offset %1: %2")
                                .arg(parseError.offset)
                                .arg(parseError.errorString());
        }
        return false;
    }

    const QJsonObject root = document.object();
    const int newRateHz = root.value(QStringLiteral("rate_hz")).toInt(20);
    if (newRateHz < 1 || newRateHz > 1000) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("rate_hz must be between 1 and 1000");
        }
        return false;
    }

    QVariantMap newValues = defaultSignalValues_;
    QSet<QString> newLockedSignals;
    const QJsonObject signalOverrides = root.value(QStringLiteral("signals")).toObject();
    const QStringList knownSignals = codec_ != nullptr ? codec_->canonicalSignalNames() : QStringList{};
    for (auto iterator = signalOverrides.begin(); iterator != signalOverrides.end(); ++iterator) {
        if (!iterator.key().startsWith(QStringLiteral("ECM."))
            || !knownSignals.contains(iterator.key())) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("unknown ECM signal '%1'").arg(iterator.key());
            }
            return false;
        }
        if (!iterator.value().isDouble() && !iterator.value().isBool()) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("signal '%1' must be numeric or boolean")
                                    .arg(iterator.key());
            }
            return false;
        }
        newValues.insert(iterator.key(), iterator.value().toVariant());
        newLockedSignals.insert(iterator.key());
    }

    QSet<quint32> newDroppedFrames;
    QHash<quint32, int> newDlcOverrides;
    const QJsonObject faults = root.value(QStringLiteral("faults")).toObject();
    for (const QJsonValue& value : faults.value(QStringLiteral("drop_frames")).toArray()) {
        quint32 frameId = 0;
        if (!parseFrameId(value, &frameId)) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("drop_frames contains an invalid CAN identifier");
            }
            return false;
        }
        if (std::none_of(messages_.cbegin(), messages_.cend(), [frameId](const auto& message) {
                return message.frameId == frameId;
            })) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("drop_frames contains non-ECM CAN ID 0x%1")
                                    .arg(frameId, 0, 16);
            }
            return false;
        }
        newDroppedFrames.insert(frameId);
    }

    const QJsonObject dlcOverrides = faults.value(QStringLiteral("dlc_overrides")).toObject();
    for (auto iterator = dlcOverrides.begin(); iterator != dlcOverrides.end(); ++iterator) {
        quint32 frameId = 0;
        if (!parseFrameId(iterator.key(), &frameId)) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("invalid DLC override CAN identifier '%1'")
                                    .arg(iterator.key());
            }
            return false;
        }
        if (std::none_of(messages_.cbegin(), messages_.cend(), [frameId](const auto& message) {
                return message.frameId == frameId;
            })) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("DLC override contains non-ECM CAN ID 0x%1")
                                    .arg(frameId, 0, 16);
            }
            return false;
        }
        const int dlc = iterator.value().toInt(-1);
        if (dlc < 0 || dlc > 8) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("DLC override for 0x%1 must be between 0 and 8")
                                    .arg(frameId, 0, 16);
            }
            return false;
        }
        newDlcOverrides.insert(frameId, dlc);
    }

    signalValues_ = newValues;
    lockedSignalNames_ = newLockedSignals;
    droppedFrameIds_ = newDroppedFrames;
    dlcOverrides_ = newDlcOverrides;
    rateHz_ = newRateHz;
    automaticValues_ = root.value(QStringLiteral("automatic_values")).toBool(false);
    paused_ = faults.value(QStringLiteral("paused")).toBool(false);
    if (frameTimer_.isActive()) {
        frameTimer_.setInterval(std::max(1, 1000 / rateHz_));
    }
    return true;
}

bool EcmSimulator::watchScenario(const QString& path, QString* errorMessage) {
    if (!loadScenario(path, errorMessage)) {
        return false;
    }
    scenarioPath_ = path;
    if (!scenarioWatcher_.files().isEmpty()) {
        scenarioWatcher_.removePaths(scenarioWatcher_.files());
    }
    if (!scenarioWatcher_.addPath(path)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("could not watch scenario file %1").arg(path);
        }
        return false;
    }
    return true;
}

void EcmSimulator::start() {
    simulationClock_.restart();
    frameTimer_.start(std::max(1, 1000 / rateHz_));
}

void EcmSimulator::stop() {
    frameTimer_.stop();
}

bool EcmSimulator::isRunning() const {
    return frameTimer_.isActive();
}

int EcmSimulator::rateHz() const {
    return rateHz_;
}

void EcmSimulator::generateFrames() {
    if (paused_ || codec_ == nullptr) {
        return;
    }

    const QVariantMap values = valuesForCurrentTick();
    const qint64 timestampNs = simulationClock_.nsecsElapsed();
    for (const DbcDecoder::CanMessageInfo& message : messages_) {
        if (droppedFrameIds_.contains(message.frameId)) {
            continue;
        }

        QString error;
        QCanBusFrame frame = codec_->encodeMessage(message.frameId, values, &error);
        if (!error.isEmpty() || !frame.isValid()) {
            emit simulatorError(QStringLiteral("could not encode %1: %2").arg(message.name, error));
            continue;
        }
        const auto dlc = dlcOverrides_.constFind(message.frameId);
        if (dlc != dlcOverrides_.cend()) {
            QByteArray payload = frame.payload();
            payload.resize(*dlc);
            frame.setPayload(payload);
        }
        emit frameGenerated(CanFrameRecord{frame, timestampNs, QStringLiteral("can0")});
    }
}

void EcmSimulator::scheduleScenarioReload(const QString&) {
    scenarioReloadTimer_.start(kScenarioReloadDebounceMs);
}

void EcmSimulator::reloadWatchedScenario() {
    QString error;
    if (!loadScenario(scenarioPath_, &error)) {
        emit simulatorError(QStringLiteral("scenario reload failed: %1").arg(error));
    } else {
        emit scenarioReloaded(scenarioPath_);
    }

    // Editors often replace a file atomically, which removes the old watch.
    if (!scenarioWatcher_.files().contains(scenarioPath_)) {
        scenarioWatcher_.addPath(scenarioPath_);
    }
}

bool EcmSimulator::parseFrameId(const QJsonValue& value, quint32* frameId) {
    if (value.isDouble()) {
        const double numeric = value.toDouble(-1.0);
        if (numeric < 0.0 || numeric > 0x1FFFFFFF || std::floor(numeric) != numeric) {
            return false;
        }
        *frameId = static_cast<quint32>(numeric);
        return true;
    }
    if (!value.isString()) {
        return false;
    }
    QString text = value.toString().trimmed();
    int base = 10;
    if (text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        text.remove(0, 2);
        base = 16;
    }
    bool ok = false;
    const quint32 parsed = text.toUInt(&ok, base);
    if (!ok || parsed > 0x1FFFFFFFU) {
        return false;
    }
    *frameId = parsed;
    return true;
}

QVariantMap EcmSimulator::valuesForCurrentTick() const {
    QVariantMap values = signalValues_;
    if (automaticValues_) {
        applyAutomaticValues(&values, static_cast<double>(simulationClock_.nsecsElapsed()) / 1.0e9);
    }
    return values;
}

void EcmSimulator::applyAutomaticValues(QVariantMap* values, double elapsedSeconds) const {
    const auto setAutomatic = [&](const QString& name, double value) {
        if (!lockedSignalNames_.contains(name)) values->insert(name, value);
    };
    const double lap = elapsedSeconds * 0.32;
    const double throttleWave = 0.5 + 0.5 * std::sin(lap);
    const double fastWave = std::sin(elapsedSeconds * 1.7);
    const double rpm = 1100.0 + 5200.0 * throttleWave + 180.0 * fastWave;
    const double tps = 5.0 + 88.0 * throttleWave;
    const double map = 38.0 + 145.0 * throttleWave;

    setAutomatic(QStringLiteral("ECM.seconds"), std::fmod(elapsedSeconds, 65535.0));
    setAutomatic(QStringLiteral("ECM.rpm"), rpm);
    setAutomatic(QStringLiteral("ECM.tps"), tps);
    setAutomatic(QStringLiteral("ECM.map"), map);
    setAutomatic(QStringLiteral("ECM.pw1"), 1.8 + 7.0 * throttleWave);
    setAutomatic(QStringLiteral("ECM.pw2"), 1.8 + 7.0 * throttleWave);
    setAutomatic(QStringLiteral("ECM.adv_deg"), 10.0 + 22.0 * throttleWave);
    setAutomatic(QStringLiteral("ECM.afrtgt1"), 14.7 - 2.8 * throttleWave);
    setAutomatic(QStringLiteral("ECM.afrtgt2"), 14.7 - 2.8 * throttleWave);
    setAutomatic(QStringLiteral("ECM.afr1_old"), 14.6 - 2.6 * throttleWave + 0.08 * fastWave);
    setAutomatic(QStringLiteral("ECM.afr2_old"), 14.6 - 2.5 * throttleWave - 0.08 * fastWave);
    setAutomatic(QStringLiteral("ECM.batt"), 13.7 + 0.15 * std::sin(elapsedSeconds * 0.4));
    setAutomatic(QStringLiteral("ECM.clt"), 170.0 + 25.0 * (1.0 - std::exp(-elapsedSeconds / 90.0)));
    setAutomatic(QStringLiteral("ECM.mat"), 82.0 + 12.0 * throttleWave);
    setAutomatic(QStringLiteral("ECM.fuel_pct"), std::max(0.0, 72.0 - elapsedSeconds / 1800.0));
    setAutomatic(QStringLiteral("ECM.TPSdot"), 14.08 * std::cos(lap));
    setAutomatic(QStringLiteral("ECM.MAPdot"), 23.2 * std::cos(lap));
    setAutomatic(QStringLiteral("ECM.RPMdot"), 832.0 * std::cos(lap));
}

void EcmSimulator::installDefaultValues() {
    defaultSignalValues_.insert(QStringLiteral("ECM.rpm"), 900.0);
    defaultSignalValues_.insert(QStringLiteral("ECM.pw1"), 2.5);
    defaultSignalValues_.insert(QStringLiteral("ECM.pw2"), 2.5);
    defaultSignalValues_.insert(QStringLiteral("ECM.adv_deg"), 12.0);
    defaultSignalValues_.insert(QStringLiteral("ECM.afrtgt1"), 14.7);
    defaultSignalValues_.insert(QStringLiteral("ECM.afrtgt2"), 14.7);
    defaultSignalValues_.insert(QStringLiteral("ECM.baro"), 100.0);
    defaultSignalValues_.insert(QStringLiteral("ECM.map"), 35.0);
    defaultSignalValues_.insert(QStringLiteral("ECM.mat"), 90.0);
    defaultSignalValues_.insert(QStringLiteral("ECM.clt"), 190.0);
    defaultSignalValues_.insert(QStringLiteral("ECM.tps"), 0.0);
    defaultSignalValues_.insert(QStringLiteral("ECM.batt"), 13.8);
    defaultSignalValues_.insert(QStringLiteral("ECM.afr1_old"), 14.7);
    defaultSignalValues_.insert(QStringLiteral("ECM.afr2_old"), 14.7);
    defaultSignalValues_.insert(QStringLiteral("ECM.fuel_pct"), 10.0);
    signalValues_ = defaultSignalValues_;
}

}  // namespace miata::data
