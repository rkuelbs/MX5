#include "logging/mdf4_log_sink.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>

#include <mdf/iattachment.h>
#include <mdf/ichannel.h>
#include <mdf/ichannelgroup.h>
#include <mdf/idatagroup.h>
#include <mdf/ifilehistory.h>
#include <mdf/iheader.h>
#include <mdf/mdffactory.h>
#include <mdf/mdfwriter.h>

#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>

namespace miata::data {
namespace {

std::string utf8(const QString& value) {
    return value.toUtf8().toStdString();
}

QString sha256(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromLatin1(
        QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex());
}

bool fail(QString* errorMessage, const QString& message) {
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
    return false;
}

}  // namespace

class Mdf4LogSink::Impl final {
public:
    struct ChannelContext {
        mdf::IChannelGroup* group = nullptr;
        mdf::IChannel* valueChannel = nullptr;
    };

    std::unique_ptr<mdf::MdfWriter> writer;
    std::unordered_map<std::string, ChannelContext> channels;
    QString filePath;
    quint64 sessionStartUtcNs = 0;
    quint64 lastSampleUtcNs = 0;
};

Mdf4LogSink::Mdf4LogSink() : impl_(std::make_unique<Impl>()) {}

Mdf4LogSink::~Mdf4LogSink() {
    close();
}

bool Mdf4LogSink::open(
    const QString& path,
    const QList<SignalDefinition>& definitions,
    const QStringList& provenanceFiles,
    QString* errorMessage) {
    close();
    if (definitions.isEmpty()) {
        return fail(errorMessage, QStringLiteral("cannot create MDF4 without signal definitions"));
    }

    auto writer = mdf::MdfFactory::CreateMdfWriter(mdf::MdfWriterType::Mdf4Basic);
    if (!writer || !writer->Init(utf8(path))) {
        return fail(errorMessage, QStringLiteral("failed to initialize MDF4 file %1").arg(path));
    }
    writer->CompressData(true);
    writer->PreTrigTime(0.0);

    const quint64 sessionStart =
        static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * 1'000'000ULL;
    mdf::IHeader* header = writer->Header();
    if (header == nullptr) {
        return fail(errorMessage, QStringLiteral("MDF4 writer did not create a header"));
    }
    header->Author("Miata vehicle logger");
    header->Project("Vehicle-Electronics");
    header->Subject("Decoded vehicle signals");
    header->StartTime(sessionStart);

    QStringList provenanceDescriptions;
    for (const QString& provenancePath : provenanceFiles) {
        const QString hash = sha256(provenancePath);
        if (hash.isEmpty()) {
            return fail(
                errorMessage,
                QStringLiteral("could not read MDF4 provenance file %1").arg(provenancePath));
        }
        provenanceDescriptions.append(
            QStringLiteral("%1 sha256=%2").arg(QFileInfo(provenancePath).fileName(), hash));
        mdf::IAttachment* attachment = header->CreateAttachment();
        if (attachment == nullptr) {
            return fail(errorMessage, QStringLiteral("failed to create MDF4 attachment"));
        }
        attachment->FileName(utf8(QFileInfo(provenancePath).absoluteFilePath()));
        attachment->FileType("application/octet-stream");
        attachment->IsEmbedded(true);
        attachment->IsCompressed(true);
    }
    header->Description(utf8(provenanceDescriptions.join(QLatin1Char('\n'))));

    if (mdf::IFileHistory* history = header->CreateFileHistory(); history != nullptr) {
        history->Time(sessionStart);
        history->ToolName("vehicle_loggerd");
        history->ToolVendor("Miata Vehicle-Electronics");
        history->ToolVersion("0.3.0");
        history->Description("Initial decoded-signal measurement");
    }

    mdf::IDataGroup* dataGroup = writer->CreateDataGroup();
    if (dataGroup == nullptr) {
        return fail(errorMessage, QStringLiteral("failed to create MDF4 data group"));
    }

    std::unordered_map<std::string, Impl::ChannelContext> channels;
    channels.reserve(static_cast<size_t>(definitions.size()));
    for (const SignalDefinition& definition : definitions) {
        const std::string canonicalName = utf8(definition.canonicalName);
        if (canonicalName.empty() || channels.contains(canonicalName)) {
            return fail(
                errorMessage,
                QStringLiteral("duplicate or empty MDF4 signal definition '%1'")
                    .arg(definition.canonicalName));
        }

        mdf::IChannelGroup* group = mdf::MdfWriter::CreateChannelGroup(dataGroup);
        if (group == nullptr) {
            return fail(errorMessage, QStringLiteral("failed to create MDF4 channel group"));
        }
        group->Name(canonicalName);
        group->Description("Samples for " + canonicalName);

        mdf::IChannel* master = mdf::MdfWriter::CreateChannel(group);
        mdf::IChannel* value = mdf::MdfWriter::CreateChannel(group);
        if (master == nullptr || value == nullptr) {
            return fail(errorMessage, QStringLiteral("failed to create MDF4 channels"));
        }
        master->Name("Time");
        master->Description("Seconds since session start");
        master->Type(mdf::ChannelType::Master);
        master->Sync(mdf::ChannelSyncType::Time);
        master->DataType(mdf::ChannelDataType::FloatLe);
        master->DataBytes(sizeof(double));
        master->Unit("s");

        value->Name(canonicalName);
        value->Description("Decoded physical value");
        value->Type(mdf::ChannelType::FixedLength);
        value->Sync(mdf::ChannelSyncType::None);
        value->DataType(mdf::ChannelDataType::FloatLe);
        value->DataBytes(sizeof(double));
        value->Unit(utf8(definition.unit));
        channels.emplace(canonicalName, Impl::ChannelContext{group, value});
    }

    if (!writer->InitMeasurement()) {
        return fail(errorMessage, QStringLiteral("failed to initialize MDF4 measurement"));
    }
    writer->StartMeasurement(sessionStart);

    impl_->writer = std::move(writer);
    impl_->channels = std::move(channels);
    impl_->filePath = path;
    impl_->sessionStartUtcNs = sessionStart;
    impl_->lastSampleUtcNs = sessionStart;
    return true;
}

bool Mdf4LogSink::writeSignalSample(
    const SignalSample& sample,
    QString* errorMessage) {
    if (!impl_->writer) {
        return true;
    }
    const auto channel = impl_->channels.find(utf8(sample.canonicalName));
    if (channel == impl_->channels.end()) {
        return fail(
            errorMessage,
            QStringLiteral("MDF4 has no channel for '%1'").arg(sample.canonicalName));
    }
    if (sample.monotonicTimestampNs < 0
        || static_cast<quint64>(sample.monotonicTimestampNs)
            > std::numeric_limits<quint64>::max() - impl_->sessionStartUtcNs) {
        return fail(errorMessage, QStringLiteral("invalid MDF4 sample timestamp"));
    }

    bool valueOk = false;
    const double value = sample.value.toDouble(&valueOk);
    if (!valueOk) {
        return fail(
            errorMessage,
            QStringLiteral("MDF4 signal '%1' is not numeric").arg(sample.canonicalName));
    }
    const quint64 absoluteTimestamp =
        impl_->sessionStartUtcNs + static_cast<quint64>(sample.monotonicTimestampNs);
    if (absoluteTimestamp < impl_->lastSampleUtcNs) {
        return fail(errorMessage, QStringLiteral("MDF4 samples are not time ordered"));
    }

    channel->second.valueChannel->SetChannelValue(value);
    impl_->writer->SaveSample(*channel->second.group, absoluteTimestamp);
    impl_->lastSampleUtcNs = absoluteTimestamp;
    return true;
}

bool Mdf4LogSink::close(QString* errorMessage) {
    if (!impl_->writer) {
        return true;
    }
    impl_->writer->StopMeasurement(impl_->lastSampleUtcNs);
    const bool finalized = impl_->writer->FinalizeMeasurement();
    impl_->writer.reset();
    impl_->channels.clear();
    if (!finalized) {
        return fail(errorMessage, QStringLiteral("failed to finalize MDF4 measurement"));
    }
    return true;
}

bool Mdf4LogSink::isOpen() const {
    return impl_->writer != nullptr;
}

QString Mdf4LogSink::path() const {
    return impl_->filePath;
}

}  // namespace miata::data
