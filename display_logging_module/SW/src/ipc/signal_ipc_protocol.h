#pragma once

#include "core/signal_definition.h"
#include "core/signal_sample.h"

#include <QByteArray>
#include <QList>
#include <QString>

namespace miata::ipc {

inline constexpr quint16 kSignalIpcProtocolVersion = 1;
inline constexpr qsizetype kMaximumSignalIpcFrameBytes = 16 * 1024 * 1024;
inline const QString kDefaultSignalIpcServerName = QStringLiteral("miata-vehicle-data-v1");

enum class SignalIpcMessageType : quint8 {
    Definitions = 1,
    Samples = 2,
};

struct SignalIpcMessage {
    SignalIpcMessageType type = SignalIpcMessageType::Definitions;
    QList<miata::data::SignalDefinition> definitions;
    QList<miata::data::SignalSample> samples;
};

[[nodiscard]] QByteArray encodeDefinitions(
    const QList<miata::data::SignalDefinition>& definitions);
[[nodiscard]] QByteArray encodeSamples(const QList<miata::data::SignalSample>& samples);

// Consumes every complete length-prefixed message and retains a partial tail.
// Returns false only for malformed or unsupported input.
bool decodeAvailableMessages(
    QByteArray* buffer,
    QList<SignalIpcMessage>* messages,
    QString* errorMessage = nullptr);

}  // namespace miata::ipc
