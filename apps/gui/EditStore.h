#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <memory>
class QLockFile;
// Explicit editing workspaces are never age-cleaned like disposable views.
class EditStore {
  public:
    explicit EditStore(QString root = {});
    ~EditStore();
    bool available() const;
    QString reserve(const QString &archive, const QString &entry);
    QJsonArray records() const;
    QJsonObject record(const QString &id) const;
    bool update(const QString &id, const QJsonObject &data);
    bool discard(const QString &id);
    bool validPayload(const QJsonObject &record) const;
    QString directory(const QString &id) const;
    QString error() const { return error_; }
    static bool write(const QString &directory, const QJsonObject &data);
    static QString fingerprint(const QString &path);

  private:
    QString root_, error_;
    std::unique_ptr<QLockFile> lock_;
};
