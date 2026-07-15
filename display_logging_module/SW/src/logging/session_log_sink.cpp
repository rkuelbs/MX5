#include "logging/session_log_sink.h"

#include "can/candump_codec.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>

namespace miata::data {

SessionLogSink::SessionLogSink()
    : rawCanStream_(&rawCanFile_), decodedCsvStream_(&decodedCsvFile_) {}

SessionLogSink::~SessionLogSink() {
    close();
}

bool SessionLogSink::openSessionDirectory(
    const QString& directoryPath,
    const QString& dbcPath,
    bool rawCanEnabled,
    QString* errorMessage) {
    QDir directory(directoryPath);
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("could not create log directory %1").arg(directoryPath);
        }
        return false;
    }

    const QString stem = QStringLiteral("vehicle_%1")
                             .arg(QDateTime::currentDateTimeUtc().toString(
                                 QStringLiteral("yyyyMMdd_HHmmss_zzz")));
    return openFiles(
        rawCanEnabled ? directory.filePath(stem + QStringLiteral(".can.log")) : QString{},
        directory.filePath(stem + QStringLiteral(".signals.csv")),
        dbcPath,
        errorMessage);
}

bool SessionLogSink::openFiles(
    const QString& rawCanPath,
    const QString& decodedCsvPath,
    const QString& dbcPath,
    QString* errorMessage) {
    close();
    QString hashError;
    const QByteArray dbcHash = dbcSha256(dbcPath, &hashError);
    if (dbcHash.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = hashError;
        }
        return false;
    }

    if (!rawCanPath.isEmpty()) {
        rawCanFile_.setFileName(rawCanPath);
        if (!rawCanFile_.open(QIODevice::WriteOnly | QIODevice::Text)) {
            if (errorMessage != nullptr) {
                *errorMessage = rawCanFile_.errorString();
            }
            close();
            return false;
        }
        rawCanStream_ << "; format=candump-classic-can-v1\n"
                      << "; dbc_sha256=" << dbcHash << '\n';
    }

    if (!decodedCsvPath.isEmpty()) {
        decodedCsvFile_.setFileName(decodedCsvPath);
        if (!decodedCsvFile_.open(QIODevice::WriteOnly | QIODevice::Text)) {
            if (errorMessage != nullptr) {
                *errorMessage = decodedCsvFile_.errorString();
            }
            close();
            return false;
        }
        decodedCsvStream_ << "# dbc_sha256=" << dbcHash << '\n'
                          << "monotonic_ns,canonical_name,value,unit,source\n";
    }
    return isOpen();
}

void SessionLogSink::close() {
    if (rawCanFile_.isOpen()) {
        rawCanStream_.flush();
        rawCanFile_.close();
    }
    if (decodedCsvFile_.isOpen()) {
        decodedCsvStream_.flush();
        decodedCsvFile_.close();
    }
}

bool SessionLogSink::isOpen() const {
    return rawCanFile_.isOpen() || decodedCsvFile_.isOpen();
}

QString SessionLogSink::rawCanPath() const {
    return rawCanFile_.fileName();
}

QString SessionLogSink::decodedCsvPath() const {
    return decodedCsvFile_.fileName();
}

bool SessionLogSink::writeRawFrame(const CanFrameRecord& record, QString* errorMessage) {
    if (!rawCanFile_.isOpen()) {
        return true;
    }
    rawCanStream_ << CandumpCodec::formatLine(record) << '\n';
    if (rawCanStream_.status() != QTextStream::Ok) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to write raw CAN log");
        }
        return false;
    }
    return true;
}

bool SessionLogSink::writeSignalSample(const SignalSample& sample, QString* errorMessage) {
    if (!decodedCsvFile_.isOpen()) {
        return true;
    }
    decodedCsvStream_ << sample.monotonicTimestampNs << ','
                      << csvField(sample.canonicalName) << ','
                      << csvField(sample.value.toString()) << ','
                      << csvField(sample.unit) << ','
                      << sourceName(sample.source) << '\n';
    if (decodedCsvStream_.status() != QTextStream::Ok) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to write decoded signal log");
        }
        return false;
    }
    return true;
}

bool SessionLogSink::flush(QString* errorMessage) {
    rawCanStream_.flush();
    decodedCsvStream_.flush();
    if ((rawCanFile_.isOpen() && !rawCanFile_.flush())
        || (decodedCsvFile_.isOpen() && !decodedCsvFile_.flush())) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to flush session log");
        }
        return false;
    }
    return true;
}

QByteArray SessionLogSink::dbcSha256(const QString& dbcPath, QString* errorMessage) {
    QFile file(dbcPath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = file.errorString();
        }
        return {};
    }
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex();
}

QString SessionLogSink::csvField(const QString& value) {
    QString escaped = value;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    if (escaped.contains(QLatin1Char(',')) || escaped.contains(QLatin1Char('"'))
        || escaped.contains(QLatin1Char('\n'))) {
        return QLatin1Char('"') + escaped + QLatin1Char('"');
    }
    return escaped;
}

QString SessionLogSink::sourceName(SignalSource source) {
    switch (source) {
    case SignalSource::Can:
        return QStringLiteral("CAN");
    case SignalSource::Vn300:
        return QStringLiteral("VN300");
    case SignalSource::Replay:
        return QStringLiteral("REPLAY");
    case SignalSource::Derived:
        return QStringLiteral("DERIVED");
    }
    return QStringLiteral("UNKNOWN");
}

}  // namespace miata::data
