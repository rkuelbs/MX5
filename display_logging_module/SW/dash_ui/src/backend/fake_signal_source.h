#pragma once

#include "backend/signal_data_source.h"

#include <QElapsedTimer>
#include <QTimer>

namespace miata::dash {

class FakeSignalSource final : public SignalDataSource {
    Q_OBJECT

public:
    explicit FakeSignalSource(QObject* parent = nullptr);

    void start() override;
    void stop() override;

private slots:
    void generateBatch();

private:
    bool loadDefinitions(QString* errorMessage = nullptr);
    [[nodiscard]] double valueFor(const QString& name, double seconds, int index) const;

    QList<miata::data::SignalDefinition> definitions_;
    QElapsedTimer clock_;
    QTimer timer_;
};

}  // namespace miata::dash
