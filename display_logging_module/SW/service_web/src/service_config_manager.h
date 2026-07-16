#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace miata::service {

struct ConfigStageResult {
    QString token;
    QString sha256;
    QStringList warnings;
};

class ServiceConfigManager final {
public:
    ~ServiceConfigManager();

    void setPaths(const QString& dbcPath, const QString& loggingPath, const QString& dashPath);
    void setUpdatesEnabled(bool enabled);
    [[nodiscard]] QJsonObject statusJson() const;

    bool stage(
        const QString& type, const QByteArray& contents,
        ConfigStageResult* result, QString* errorMessage = nullptr);
    bool activate(
        const QString& type, const QString& token,
        QString* errorMessage = nullptr);
    void discard();

private:
    struct StagedConfig {
        QString type;
        QString token;
        QString path;
        QString sha256;
        QStringList warnings;
    };

    [[nodiscard]] QString targetPath(const QString& type) const;
    bool validateSet(
        const QString& dbcPath, const QString& loggingPath, const QString& dashPath,
        QStringList* warnings, QString* errorMessage) const;
    static bool writeAtomic(
        const QString& path, const QByteArray& contents, QString* errorMessage);

    QString dbcPath_;
    QString loggingPath_;
    QString dashPath_;
    bool updatesEnabled_ = false;
    StagedConfig staged_;
};

}  // namespace miata::service
