#pragma once

#include "logging/log_sink.h"

#include <QFile>
#include <QString>
#include <QTextStream>

namespace miata::data {

class SessionLogSink final : public LogSink {
public:
    SessionLogSink();
    ~SessionLogSink();

    bool openSessionDirectory(
        const QString& directoryPath,
        const QString& dbcPath,
        bool rawCanEnabled,
        QString* errorMessage = nullptr);
    bool openFiles(
        const QString& rawCanPath,
        const QString& decodedCsvPath,
        const QString& dbcPath,
        QString* errorMessage = nullptr);
    void close();

    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] QString rawCanPath() const;
    [[nodiscard]] QString decodedCsvPath() const;

    bool writeRawFrame(const CanFrameRecord& record, QString* errorMessage = nullptr) override;
    bool writeSignalSample(const SignalSample& sample, QString* errorMessage = nullptr) override;
    bool flush(QString* errorMessage = nullptr) override;

private:
    static QByteArray dbcSha256(const QString& dbcPath, QString* errorMessage);
    static QString csvField(const QString& value);
    static QString sourceName(SignalSource source);

    QFile rawCanFile_;
    QFile decodedCsvFile_;
    QTextStream rawCanStream_;
    QTextStream decodedCsvStream_;
};

}  // namespace miata::data
