#pragma once

#include "core/signal_definition.h"
#include "core/signal_sample.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <memory>

namespace miata::data {

class Mdf4LogSink final {
public:
    Mdf4LogSink();
    ~Mdf4LogSink();

    Mdf4LogSink(const Mdf4LogSink&) = delete;
    Mdf4LogSink& operator=(const Mdf4LogSink&) = delete;

    bool open(
        const QString& path,
        const QList<SignalDefinition>& definitions,
        const QStringList& provenanceFiles,
        QString* errorMessage = nullptr);
    bool writeSignalSample(const SignalSample& sample, QString* errorMessage = nullptr);
    bool close(QString* errorMessage = nullptr);

    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] QString path() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace miata::data
