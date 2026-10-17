#pragma once
#include <QJsonArray>
#include <QString>
#include <memory>
#include <vector>
class QLockFile;
// Per-user, flat, owned view directories. Never remove another running session.
class ExternalFiles {
  public:
    explicit ExternalFiles(QString root = {});
    ~ExternalFiles();
    static bool allowed(const QString &name);
    QString create();
    bool open(const QString &path) const;
    int cleanup();
    QJsonArray items() const;
    bool remove(const QString &directory);
    QString root() const { return root_; }

  private:
    QString root_;
    struct Session {
        QString path;
        std::unique_ptr<QLockFile> lock;
    };
    std::vector<Session> sessions_;
};
