#pragma once

#include <QObject>
#include <QProcess>

namespace miata::service {

class ServiceActionRunner final : public QObject {
    Q_OBJECT

public:
    explicit ServiceActionRunner(QObject* parent = nullptr);

    [[nodiscard]] bool available() const;
    [[nodiscard]] bool busy() const;
    [[nodiscard]] QString lastResult() const;
    bool run(const QString& action, QString* errorMessage = nullptr);

signals:
    void stateChanged();

private:
    QProcess process_;
    QString activeAction_;
    QString lastResult_;
};

}  // namespace miata::service
