#include "ipc/signal_ipc_protocol.h"

#include <QDataStream>
#include <QIODevice>
#include <QMetaType>

namespace miata::ipc {
namespace {

constexpr quint32 kMagic = 0x4D495043;  // MIPC

enum class WireValueType : quint8 {
    Invalid = 0,
    Number = 1,
    SignedInteger = 2,
    UnsignedInteger = 3,
    Boolean = 4,
    Text = 5,
};

void configure(QDataStream& stream) {
    stream.setVersion(QDataStream::Qt_6_5);
    stream.setByteOrder(QDataStream::BigEndian);
}

QByteArray framePayload(const QByteArray& payload) {
    QByteArray frame;
    QDataStream stream(&frame, QIODevice::WriteOnly);
    configure(stream);
    stream << quint32(payload.size());
    frame.append(payload);
    return frame;
}

void writeHeader(QDataStream& stream, SignalIpcMessageType type) {
    stream << kMagic << kSignalIpcProtocolVersion << quint8(type) << quint8(0);
}

void writeValue(QDataStream& stream, const QVariant& value) {
    const int type = value.metaType().id();
    switch (type) {
    case QMetaType::Bool:
        stream << quint8(WireValueType::Boolean) << value.toBool();
        break;
    case QMetaType::Char:
    case QMetaType::SChar:
    case QMetaType::Short:
    case QMetaType::Int:
    case QMetaType::Long:
    case QMetaType::LongLong:
        stream << quint8(WireValueType::SignedInteger) << value.toLongLong();
        break;
    case QMetaType::UChar:
    case QMetaType::UShort:
    case QMetaType::UInt:
    case QMetaType::ULong:
    case QMetaType::ULongLong:
        stream << quint8(WireValueType::UnsignedInteger) << value.toULongLong();
        break;
    case QMetaType::QString:
        stream << quint8(WireValueType::Text) << value.toString();
        break;
    case QMetaType::Float:
    case QMetaType::Double:
        stream << quint8(WireValueType::Number) << value.toDouble();
        break;
    default:
        if (value.canConvert<double>())
            stream << quint8(WireValueType::Number) << value.toDouble();
        else
            stream << quint8(WireValueType::Invalid);
        break;
    }
}

bool readValue(QDataStream& stream, QVariant* value) {
    quint8 rawType = 0;
    stream >> rawType;
    switch (static_cast<WireValueType>(rawType)) {
    case WireValueType::Invalid:
        *value = {};
        break;
    case WireValueType::Number: {
        double number = 0.0;
        stream >> number;
        *value = number;
        break;
    }
    case WireValueType::SignedInteger: {
        qint64 number = 0;
        stream >> number;
        *value = number;
        break;
    }
    case WireValueType::UnsignedInteger: {
        quint64 number = 0;
        stream >> number;
        *value = number;
        break;
    }
    case WireValueType::Boolean: {
        bool boolean = false;
        stream >> boolean;
        *value = boolean;
        break;
    }
    case WireValueType::Text: {
        QString text;
        stream >> text;
        *value = text;
        break;
    }
    default:
        return false;
    }
    return stream.status() == QDataStream::Ok;
}

bool decodePayload(const QByteArray& payload, SignalIpcMessage* message, QString* errorMessage) {
    QDataStream stream(payload);
    configure(stream);
    quint32 magic = 0;
    quint16 version = 0;
    quint8 rawType = 0;
    quint8 reserved = 0;
    stream >> magic >> version >> rawType >> reserved;
    if (magic != kMagic) {
        if (errorMessage) *errorMessage = QStringLiteral("IPC frame has an invalid magic value");
        return false;
    }
    if (version != kSignalIpcProtocolVersion) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unsupported IPC protocol version %1 (expected %2)")
                .arg(version).arg(kSignalIpcProtocolVersion);
        }
        return false;
    }

    message->type = static_cast<SignalIpcMessageType>(rawType);
    quint32 count = 0;
    stream >> count;
    if (count > 100'000) {
        if (errorMessage) *errorMessage = QStringLiteral("IPC item count exceeds the safety limit");
        return false;
    }

    if (message->type == SignalIpcMessageType::Definitions) {
        message->definitions.reserve(static_cast<qsizetype>(count));
        for (quint32 index = 0; index < count; ++index) {
            miata::data::SignalDefinition definition;
            stream >> definition.canonicalName >> definition.unit;
            if (definition.canonicalName.isEmpty()) {
                if (errorMessage) *errorMessage = QStringLiteral("IPC definition has an empty name");
                return false;
            }
            message->definitions.append(std::move(definition));
        }
    } else if (message->type == SignalIpcMessageType::Samples) {
        message->samples.reserve(static_cast<qsizetype>(count));
        for (quint32 index = 0; index < count; ++index) {
            miata::data::SignalSample sample;
            quint8 rawSource = 0;
            stream >> sample.canonicalName;
            if (!readValue(stream, &sample.value)) {
                if (errorMessage) *errorMessage = QStringLiteral("IPC sample has an invalid value type");
                return false;
            }
            stream >> sample.monotonicTimestampNs >> rawSource;
            if (sample.canonicalName.isEmpty()
                || rawSource > quint8(miata::data::SignalSource::Derived)) {
                if (errorMessage) *errorMessage = QStringLiteral("IPC sample metadata is invalid");
                return false;
            }
            sample.source = static_cast<miata::data::SignalSource>(rawSource);
            message->samples.append(std::move(sample));
        }
    } else {
        if (errorMessage) *errorMessage = QStringLiteral("IPC message type is unknown");
        return false;
    }

    if (stream.status() != QDataStream::Ok || !stream.atEnd()) {
        if (errorMessage) *errorMessage = QStringLiteral("IPC payload is truncated or has trailing data");
        return false;
    }
    return true;
}

}  // namespace

QByteArray encodeDefinitions(const QList<miata::data::SignalDefinition>& definitions) {
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    configure(stream);
    writeHeader(stream, SignalIpcMessageType::Definitions);
    stream << quint32(definitions.size());
    for (const auto& definition : definitions)
        stream << definition.canonicalName << definition.unit;
    return framePayload(payload);
}

QByteArray encodeSamples(const QList<miata::data::SignalSample>& samples) {
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    configure(stream);
    writeHeader(stream, SignalIpcMessageType::Samples);
    stream << quint32(samples.size());
    for (const auto& sample : samples) {
        stream << sample.canonicalName;
        writeValue(stream, sample.value);
        stream << sample.monotonicTimestampNs << quint8(sample.source);
    }
    return framePayload(payload);
}

bool decodeAvailableMessages(
    QByteArray* buffer, QList<SignalIpcMessage>* messages, QString* errorMessage) {
    if (!buffer || !messages) {
        if (errorMessage) *errorMessage = QStringLiteral("IPC decoder requires output storage");
        return false;
    }

    while (buffer->size() >= qsizetype(sizeof(quint32))) {
        const QByteArray header = buffer->left(sizeof(quint32));
        QDataStream headerStream(header);
        configure(headerStream);
        quint32 payloadBytes = 0;
        headerStream >> payloadBytes;
        if (payloadBytes == 0 || payloadBytes > kMaximumSignalIpcFrameBytes) {
            if (errorMessage) *errorMessage = QStringLiteral("IPC frame length is invalid");
            return false;
        }
        const qsizetype frameBytes = qsizetype(sizeof(quint32)) + payloadBytes;
        if (buffer->size() < frameBytes) return true;

        SignalIpcMessage message;
        if (!decodePayload(buffer->mid(sizeof(quint32), payloadBytes), &message, errorMessage))
            return false;
        messages->append(std::move(message));
        buffer->remove(0, frameBytes);
    }
    return true;
}

}  // namespace miata::ipc
