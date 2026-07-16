#pragma once

#include <QJsonObject>
#include <QString>

namespace miata::service {

class ServiceLogCatalog final {
public:
    void setDirectory(const QString& path);
    [[nodiscard]] QJsonObject catalogJson() const;
    [[nodiscard]] QString resolveDownload(
        const QString& fileName, QString* errorMessage = nullptr) const;
    bool deleteCompletedSession(const QString& sessionId, QString* errorMessage = nullptr) const;

private:
    QString directoryPath_;
};

}  // namespace miata::service
