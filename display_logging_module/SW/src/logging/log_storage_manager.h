#pragma once

#include "core/signal_definition.h"
#include "core/signal_sample.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace miata::data {

class LogStorageManager final {
public:
    bool loadConfiguration(const QString& configPath, QString* errorMessage = nullptr);
    bool enforce(
        const QString& directoryPath,
        const QString& activeMdfPath = {},
        QString* errorMessage = nullptr);

    [[nodiscard]] int cleanupIntervalMs() const;
    [[nodiscard]] bool healthy() const;
    [[nodiscard]] qint64 availableBytes() const;
    [[nodiscard]] qint64 managedBytes() const;
    [[nodiscard]] int sessionCount() const;
    [[nodiscard]] int incompleteSessionCount() const;
    [[nodiscard]] quint64 cleanupErrorCount() const;
    [[nodiscard]] QString lastError() const;
    [[nodiscard]] QStringList lastDeletedSessions() const;

    [[nodiscard]] QList<SignalSample> samples(
        qint64 monotonicTimestampNs, bool loggingActive) const;
    static QStringList canonicalSignalNames();
    static QList<SignalDefinition> signalDefinitions();

private:
    qint64 minimumFreeBytes_ = 0;
    qint64 maximumTotalBytes_ = 0;
    qint64 maximumAgeSeconds_ = 0;
    int cleanupIntervalMs_ = 60'000;
    bool configured_ = false;
    bool healthy_ = false;
    qint64 availableBytes_ = -1;
    qint64 managedBytes_ = 0;
    int sessionCount_ = 0;
    int incompleteSessionCount_ = 0;
    quint64 cleanupErrorCount_ = 0;
    QString lastError_;
    QStringList lastDeletedSessions_;
};

}  // namespace miata::data
