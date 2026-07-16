#pragma once

#include "core/signal_definition.h"
#include "core/signal_sample.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QTimer>

namespace miata::dash {

class SignalChannel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString signalName READ signalName CONSTANT)
    Q_PROPERTY(QVariant value READ value NOTIFY valueChanged)
    Q_PROPERTY(bool valid READ valid NOTIFY valueChanged)
    Q_PROPERTY(QString unit READ unit NOTIFY valueChanged)
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY valueChanged)
    Q_PROPERTY(bool stale READ stale NOTIFY staleChanged)

public:
    explicit SignalChannel(QString signalName, QObject* parent = nullptr);

    [[nodiscard]] QString signalName() const;
    [[nodiscard]] QVariant value() const;
    [[nodiscard]] bool valid() const;
    [[nodiscard]] QString unit() const;
    [[nodiscard]] QString sourceName() const;
    [[nodiscard]] bool stale() const;

signals:
    void valueChanged();
    void staleChanged();

private:
    friend class SignalValueProvider;
    QString signalName_;
    QVariant value_;
    QString unit_;
    QString sourceName_;
    qint64 localReceiptNs_ = -1;
    int staleAfterMs_ = -1;
    bool stale_ = true;
};

class SignalValueProvider final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int staleAfterMs READ staleAfterMs WRITE setStaleAfterMs NOTIFY staleAfterMsChanged)

public:
    explicit SignalValueProvider(QObject* parent = nullptr);

    Q_INVOKABLE QObject* channel(const QString& signalName);
    [[nodiscard]] SignalChannel* channelObject(const QString& signalName);
    [[nodiscard]] int staleAfterMs() const;
    void setStaleAfterMs(int milliseconds);
    void setSignalStaleAfterMs(const QString& signalName, int milliseconds);

public slots:
    void setDefinitions(const QList<miata::data::SignalDefinition>& definitions);
    void updateSamples(const QList<miata::data::SignalSample>& samples);

signals:
    void staleAfterMsChanged();

private slots:
    void refreshFreshness();

private:
    QHash<QString, SignalChannel*> channels_;
    QElapsedTimer clock_;
    QTimer freshnessTimer_;
    int staleAfterMs_ = 1000;
};

}  // namespace miata::dash
