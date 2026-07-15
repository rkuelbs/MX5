#include "can/dbc_decoder.h"

#include <QCanDbcFileParser>
#include <QCanMessageDescription>
#include <QCanSignalDescription>
#include <QCanUniqueIdDescription>
#include <QSet>

#include <algorithm>

namespace miata::data {

bool DbcDecoder::load(const QString& dbcPath, QString* errorMessage) {
    loaded_ = false;
    warnings_.clear();
    senderByFrameId_.clear();
    messageInfoByFrameId_.clear();
    metadataByCanonicalName_.clear();
    processor_.clearMessageDescriptions();

    QCanDbcFileParser parser;
    if (!parser.parse(dbcPath)) {
        if (errorMessage != nullptr) {
            *errorMessage = parser.errorString();
        }
        return false;
    }

    warnings_ = parser.warnings();
    const QList<QCanMessageDescription> messages = parser.messageDescriptions();
    if (messages.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("DBC contains no decodable CAN messages");
        }
        return false;
    }

    QSet<QString> canonicalNames;
    for (const QCanMessageDescription& message : messages) {
        const QString sender = message.transmitter().trimmed();
        if (sender.isEmpty()) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("CAN message %1 has no transmitter").arg(message.name());
            }
            return false;
        }

        const quint32 frameId = static_cast<quint32>(message.uniqueId());
        senderByFrameId_.insert(frameId, sender);
        CanMessageInfo messageInfo{frameId, message.name(), sender, {}};

        for (const QCanSignalDescription& signal : message.signalDescriptions()) {
            // Message names are deliberately excluded. Public names are Sender.signal.
            const QString canonicalName = sender + QLatin1Char('.') + signal.name();
            if (canonicalNames.contains(canonicalName)) {
                if (errorMessage != nullptr) {
                    *errorMessage = QStringLiteral(
                        "Duplicate canonical signal %1; signal names must be unique per sender")
                                        .arg(canonicalName);
                }
                return false;
            }

            canonicalNames.insert(canonicalName);
            messageInfo.canonicalSignalNames.append(canonicalName);
            metadataByCanonicalName_.insert(
                canonicalName,
                SignalMetadata{canonicalName, signal.physicalUnit()});
        }
        messageInfoByFrameId_.insert(frameId, messageInfo);
    }

    processor_.setUniqueIdDescription(QCanDbcFileParser::uniqueIdDescription());
    processor_.setMessageDescriptions(messages);
    loaded_ = true;
    return true;
}

bool DbcDecoder::isLoaded() const {
    return loaded_;
}

QStringList DbcDecoder::warnings() const {
    return warnings_;
}

QStringList DbcDecoder::canonicalSignalNames() const {
    QStringList names = metadataByCanonicalName_.keys();
    std::sort(names.begin(), names.end());
    return names;
}

QList<SignalDefinition> DbcDecoder::signalDefinitions() const {
    QList<SignalDefinition> definitions;
    definitions.reserve(metadataByCanonicalName_.size());
    for (auto iterator = metadataByCanonicalName_.cbegin();
         iterator != metadataByCanonicalName_.cend();
         ++iterator) {
        definitions.append(SignalDefinition{iterator->canonicalName, iterator->unit});
    }
    std::sort(
        definitions.begin(),
        definitions.end(),
        [](const SignalDefinition& left, const SignalDefinition& right) {
            return left.canonicalName < right.canonicalName;
        });
    return definitions;
}

QList<DbcDecoder::CanMessageInfo> DbcDecoder::messageInfos(const QString& sender) const {
    QList<CanMessageInfo> messages;
    for (const CanMessageInfo& message : messageInfoByFrameId_) {
        if (sender.isEmpty() || message.sender == sender) {
            messages.append(message);
        }
    }
    std::sort(messages.begin(), messages.end(), [](const auto& left, const auto& right) {
        return left.frameId < right.frameId;
    });
    return messages;
}

QList<SignalSample> DbcDecoder::decode(
    const QCanBusFrame& frame,
    qint64 monotonicTimestampNs,
    QString* errorMessage,
    SignalSource source) {
    QList<SignalSample> samples;
    if (!loaded_) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("DBC decoder is not loaded");
        }
        return samples;
    }

    const auto senderIterator = senderByFrameId_.constFind(frame.frameId());
    if (senderIterator == senderByFrameId_.cend()) {
        return samples;
    }

    const QCanFrameProcessor::ParseResult result = processor_.parseFrame(frame);
    if (processor_.error() != QCanFrameProcessor::Error::None) {
        if (errorMessage != nullptr) {
            *errorMessage = processor_.errorString();
        }
        return samples;
    }

    const QString prefix = senderIterator.value() + QLatin1Char('.');
    for (auto iterator = result.signalValues.cbegin(); iterator != result.signalValues.cend(); ++iterator) {
        const QString canonicalName = prefix + iterator.key();
        const auto metadata = metadataByCanonicalName_.constFind(canonicalName);
        if (metadata == metadataByCanonicalName_.cend()) {
            continue;
        }

        samples.append(SignalSample{
            canonicalName,
            iterator.value(),
            metadata->unit,
            monotonicTimestampNs,
            source,
        });
    }

    return samples;
}

QCanBusFrame DbcDecoder::encodeMessage(
    quint32 frameId,
    const QVariantMap& canonicalValues,
    QString* errorMessage) {
    if (!loaded_) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("DBC decoder is not loaded");
        }
        return QCanBusFrame();
    }

    const auto message = messageInfoByFrameId_.constFind(frameId);
    if (message == messageInfoByFrameId_.cend()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("DBC contains no message with CAN ID 0x%1")
                                .arg(frameId, 0, 16);
        }
        return QCanBusFrame();
    }

    QVariantMap localValues;
    for (auto iterator = canonicalValues.cbegin(); iterator != canonicalValues.cend(); ++iterator) {
        if (!message->canonicalSignalNames.contains(iterator.key())) {
            continue;
        }
        const qsizetype separator = iterator.key().indexOf(QLatin1Char('.'));
        localValues.insert(iterator.key().mid(separator + 1), iterator.value());
    }

    QCanBusFrame frame = processor_.prepareFrame(
        static_cast<QtCanBus::UniqueId>(frameId), localValues);
    if (processor_.error() != QCanFrameProcessor::Error::None || !frame.isValid()) {
        if (errorMessage != nullptr) {
            *errorMessage = processor_.errorString();
        }
        return QCanBusFrame();
    }
    return frame;
}

}  // namespace miata::data
