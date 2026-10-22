#pragma once
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <memory>

class QLockFile;

class NestedStore {
  public:
    explicit NestedStore(QString root = {}, quint64 quota = 1ULL << 30);
    ~NestedStore();

    bool available() const;
    QString reserve(const QString &parentArchive, const QString &parentEntry,
                    int depth = 1, quint64 expectedBytes = 256ULL << 20);
    QString directory(const QString &id) const;
    QJsonObject record(const QString &id) const;
    QJsonArray records() const;
    bool update(const QString &id, const QJsonObject &data);
    bool discard(const QString &id);
    int cleanup(const QSet<QString> &activeIds = {});

    quint64 usedBytes() const;
    quint64 quota() const { return quota_; }
    QString root() const { return root_; }
    QString error() const { return error_; }

    static constexpr int MaxDepth = 4;
    static constexpr quint64 MaxContainerBytes = 256ULL << 20; // 256 MiB

  private:
    QString root_, error_;
    quint64 quota_;
    std::unique_ptr<QLockFile> lock_;
};

