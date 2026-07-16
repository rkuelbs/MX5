#pragma once

#include "core/signal_definition.h"
#include "core/signal_sample.h"

#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>

namespace miata::service {

class ServiceDataStore final : public QObject {
    Q_OBJECT

public:
    explicit ServiceDataStore(QObject* parent = nullptr);

    void setLogDirectory(const QString& path);
    void setConfigurationPaths(
        const QString& dbcPath, const QString& loggingPath, const QString& dashPath);
    void setLoggerConnected(bool connected);
    void updateDefinitions(const QList<miata::data::SignalDefinition>& definitions);
    void updateSamples(const QList<miata::data::SignalSample>& samples);

    [[nodiscard]] QJsonObject statusJson(
        bool actionsAvailable, bool actionBusy, const QString& lastActionResult) const;
    [[nodiscard]] QJsonArray signalsJson() const;

private:
    struct LatestSignal {
        QVariant value;
        QString unit;
        QString source;
        qint64 receiptNs = -1;
    };

    static QString sourceName(miata::data::SignalSource source);
    static QString fileSha256(const QString& path);
    static QJsonObject storagePolicy(const QString& path);

    QHash<QString, QString> unitsByName_;
    QHash<QString, LatestSignal> latestByName_;
    QElapsedTimer clock_;
    QString logDirectory_;
    QString dbcPath_;
    QString loggingPath_;
    QString dashPath_;
    qint64 lastBatchReceiptNs_ = -1;
    bool loggerConnected_ = false;
};

}  // namespace miata::service
