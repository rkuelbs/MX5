#pragma once

#include "core/signal_definition.h"
#include "core/signal_sample.h"

#include <QHash>
#include <QLocalServer>
#include <QList>
#include <QObject>
#include <QTimer>

class QLocalSocket;

namespace miata::ipc {

class SignalIpcServer final : public QObject {
    Q_OBJECT

public:
    explicit SignalIpcServer(QObject* parent = nullptr);

    void setDefinitions(const QList<miata::data::SignalDefinition>& definitions);
    bool start(const QString& serverName, QString* errorMessage = nullptr);
    void stop();
    [[nodiscard]] bool isListening() const;
    [[nodiscard]] QString serverName() const;
    [[nodiscard]] int clientCount() const;

public slots:
    void publishSamples(const QList<miata::data::SignalSample>& samples);

signals:
    void clientCountChanged(int count);
    void serverError(const QString& message);

private slots:
    void acceptConnections();
    void flushPendingSamples();

private:
    void send(QLocalSocket* socket, const QByteArray& frame);
    void removeClient(QLocalSocket* socket);

    QLocalServer server_;
    QList<QLocalSocket*> clients_;
    QList<miata::data::SignalDefinition> definitions_;
    QHash<QString, miata::data::SignalSample> latestState_;
    QHash<QString, miata::data::SignalSample> pendingLatest_;
    QList<miata::data::SignalSample> pendingEvents_;
    QTimer flushTimer_;
};

}  // namespace miata::ipc
