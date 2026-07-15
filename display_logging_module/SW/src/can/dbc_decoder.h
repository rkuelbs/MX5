#pragma once

#include "core/signal_sample.h"
#include "core/signal_definition.h"

#include <QCanBusFrame>
#include <QCanFrameProcessor>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace miata::data {

class DbcDecoder final {
public:
    struct CanMessageInfo {
        quint32 frameId = 0;
        QString name;
        QString sender;
        QStringList canonicalSignalNames;
    };

    bool load(const QString& dbcPath, QString* errorMessage = nullptr);

    [[nodiscard]] bool isLoaded() const;
    [[nodiscard]] QStringList warnings() const;
    [[nodiscard]] QStringList canonicalSignalNames() const;
    [[nodiscard]] QList<SignalDefinition> signalDefinitions() const;
    [[nodiscard]] QList<CanMessageInfo> messageInfos(const QString& sender = {}) const;

    [[nodiscard]] QList<SignalSample> decode(
        const QCanBusFrame& frame,
        qint64 monotonicTimestampNs,
        QString* errorMessage = nullptr,
        SignalSource source = SignalSource::Can);

    [[nodiscard]] QCanBusFrame encodeMessage(
        quint32 frameId,
        const QVariantMap& canonicalValues,
        QString* errorMessage = nullptr);

private:
    struct SignalMetadata {
        QString canonicalName;
        QString unit;
    };

    QCanFrameProcessor processor_;
    QHash<quint32, QString> senderByFrameId_;
    QHash<quint32, CanMessageInfo> messageInfoByFrameId_;
    QHash<QString, SignalMetadata> metadataByCanonicalName_;
    QStringList warnings_;
    bool loaded_ = false;
};

}  // namespace miata::data
