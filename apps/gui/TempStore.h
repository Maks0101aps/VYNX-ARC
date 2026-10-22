#pragma once
#include <QString>
#include <QStringList>
#include <QVector>
#include <QSet>

class ExternalFiles;
class NestedStore;
class EditStore;

class TempStore {
  public:
    struct CategoryUsage {
        quint64 bytes = 0;
        int count = 0;
    };

    struct TempItem {
        QString id;
        QString category; // "Preview", "Nested", "WorkingCopy"
        QString name;
        QString path;
        quint64 bytes = 0;
        QString status;   // "Clean", "Unsaved modifications", "In use", "Disposable"
        QString reason;   // "Unsaved editor changes", "Active session open", "Cached view"
        bool canClean = false;
    };

    TempStore(ExternalFiles *externalFiles, NestedStore *nestedStore, EditStore *editStore);

    CategoryUsage previewUsage() const;
    CategoryUsage nestedUsage() const;
    CategoryUsage editUsage() const;

    QVector<TempItem> allItems(const QSet<QString> &activeNestedIds = {}) const;
    QVector<TempItem> previewCleanupList(const QSet<QString> &activeNestedIds = {}) const;
    int cleanItems(const QVector<TempItem> &items);

  private:
    ExternalFiles *externalFiles_ = nullptr;
    NestedStore *nestedStore_ = nullptr;
    EditStore *editStore_ = nullptr;
};

