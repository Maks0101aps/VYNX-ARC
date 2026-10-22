#pragma once
#include "ArchiveModel.h"
#include "dialogs/PreviewDialog.h"
#include <QHash>
#include <QList>
#include <QSet>
#include <QStringList>
#include <QUuid>
#include <memory>
#include <optional>

class SecretUtf8;

struct NestedArchiveInfo {
    QString parentSessionId;
    QString parentArchivePath;
    QString parentEntryPath;
    QString parentFingerprint;
    QString nestedStoreId;
    QString tempContainerPath;
    int depth = 1;
};

struct CachedPreview {
    PreviewContent content;
    quint64 entryId = 0;
    quint64 bytesSize = 0;
};

class PreviewCache {
  public:
    static constexpr quint64 MaxBytes = 32ULL << 20; // 32 MiB

    std::optional<PreviewContent> get(quint64 entryId) {
        auto it = entries_.find(entryId);
        if (it == entries_.end())
            return std::nullopt;
        lru_.removeAll(entryId);
        lru_.append(entryId);
        return it.value().content;
    }

    void put(quint64 entryId, PreviewContent content) {
        if (entries_.contains(entryId))
            return;
        quint64 size = quint64(content.bytes.size());
        if (content.text.has_value())
            size += quint64(content.text->size() * sizeof(QChar));
        if (!content.image.isNull())
            size += quint64(content.image.sizeInBytes());

        if (size > MaxBytes)
            return;

        while (!lru_.isEmpty() && (totalBytes_ + size > MaxBytes || entries_.size() >= 64)) {
            quint64 evictId = lru_.takeFirst();
            auto it = entries_.find(evictId);
            if (it != entries_.end()) {
                totalBytes_ -= it.value().bytesSize;
                entries_.erase(it);
            }
        }

        if (totalBytes_ + size > MaxBytes)
            return;

        CachedPreview cp;
        cp.content = std::move(content);
        cp.entryId = entryId;
        cp.bytesSize = size;
        entries_.insert(entryId, std::move(cp));
        lru_.append(entryId);
        totalBytes_ += size;
    }

    void clear() {
        entries_.clear();
        lru_.clear();
        totalBytes_ = 0;
    }

    quint64 totalBytes() const { return totalBytes_; }
    int count() const { return entries_.size(); }

  private:
    QHash<quint64, CachedPreview> entries_;
    QList<quint64> lru_;
    quint64 totalBytes_ = 0;
};

// Document state never belongs to the active window or a queue row.
struct ArchiveSession {
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString path, search;
    QByteArray headerState;
    QStringList history;
    QSet<QString> selection;
    int scroll = 0;
    bool busy = false, closeWhenIdle = false;
    std::shared_ptr<rust::Box<vynx::Archive>> archive;
    std::shared_ptr<SecretUtf8> password;
    std::shared_ptr<NestedArchiveInfo> nestedInfo;
    PreviewCache previewCache;
    ArchiveModel *model = new ArchiveModel;
    ArchiveFilter *filter = new ArchiveFilter;

    ArchiveSession() {
        filter->setSourceModel(model);
        filter->setFilterCaseSensitivity(Qt::CaseInsensitive);
        filter->setFilterKeyColumn(0);
    }
    ~ArchiveSession() {
        delete filter;
        delete model;
    }
};
