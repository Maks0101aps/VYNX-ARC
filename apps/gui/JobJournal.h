#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <memory>
class QLockFile;
class JobJournal {
  public:
    JobJournal();
    ~JobJournal();
    bool open(const QString &path, bool enabled);
    QString add(const QString &title, const QJsonObject &spec);
    bool updateSpec(const QString &id, const QJsonObject &spec);
    bool setState(const QString &id, const QString &state, const QString &error = {});
    bool remove(const QString &id);
    bool clear();
    bool setEnabled(bool enabled);
    QJsonArray records() const { return records_; }
    QJsonObject record(const QString &id) const;
    static QJsonObject sanitizeSpec(const QJsonObject &spec);
    static QJsonArray sourceSnapshot(const QJsonObject &spec);
    static bool sourcesUnchanged(const QJsonObject &record);
    QString error() const { return error_; }

  private:
    bool save();
    QString path_, error_;
    bool enabled_ = false;
    std::unique_ptr<QLockFile> lock_;
    QJsonArray records_;
};
