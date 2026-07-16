#pragma once

#include "core/signal_sample.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVariantList>

namespace miata::dash {

class SignalHistoryStore final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList series READ series NOTIFY seriesChanged)
    Q_PROPERTY(int windowSeconds READ windowSeconds WRITE setWindowSeconds NOTIFY windowSecondsChanged)
    Q_PROPERTY(int maxSignals READ maxSignals CONSTANT)

public:
    explicit SignalHistoryStore(QObject* parent = nullptr);

    [[nodiscard]] QVariantList series() const;
    [[nodiscard]] int windowSeconds() const;
    [[nodiscard]] int maxSignals() const;
    void setWindowSeconds(int seconds);

public slots:
    void setSelectedSignals(const QStringList& signalNames);
    void updateSamples(const QList<miata::data::SignalSample>& samples);
    void clear();

signals:
    void seriesChanged();
    void windowSecondsChanged();

private:
    struct Point { qint64 receiptNs = 0; double value = 0.0; };
    struct Trace { QString unit; QList<Point> points; };

    void prune(qint64 nowNs);
    void publishIfDirty();

    QHash<QString, Trace> traces_;
    QStringList selectedSignals_;
    QElapsedTimer clock_;
    QTimer publishTimer_;
    int windowSeconds_ = 10;
    int maxSignals_ = 4;
    int maxPointsPerSignal_ = 5000;
    bool dirty_ = false;
};

}  // namespace miata::dash
