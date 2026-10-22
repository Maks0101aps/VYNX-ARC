#include "TempStore.h"
#include "EditStore.h"
#include "ExternalFiles.h"
#include "NestedStore.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>

TempStore::TempStore(ExternalFiles *externalFiles, NestedStore *nestedStore, EditStore *editStore)
    : externalFiles_(externalFiles), nestedStore_(nestedStore), editStore_(editStore) {}

TempStore::CategoryUsage TempStore::previewUsage() const {
    CategoryUsage usage;
    if (!externalFiles_)
        return usage;
    const auto items = externalFiles_->items();
    usage.count = items.size();
    for (const auto &val : items)
        usage.bytes += quint64(val.toObject()["bytes"].toDouble());
    return usage;
}

TempStore::CategoryUsage TempStore::nestedUsage() const {
    CategoryUsage usage;
    if (!nestedStore_)
        return usage;
    const auto records = nestedStore_->records();
    usage.count = records.size();
    usage.bytes = nestedStore_->usedBytes();
    return usage;
}

TempStore::CategoryUsage TempStore::editUsage() const {
    CategoryUsage usage;
    if (!editStore_)
        return usage;
    const auto records = editStore_->records();
    usage.count = records.size();
    for (const auto &val : records) {
        const auto rec = val.toObject();
        usage.bytes += quint64(QFileInfo(rec["file"].toString()).size());
    }
    return usage;
}

QVector<TempStore::TempItem> TempStore::allItems(const QSet<QString> &activeNestedIds) const {
    QVector<TempItem> result;

    // 1. Previews (views)
    if (externalFiles_) {
        for (const auto &val : externalFiles_->items()) {
            const auto obj = val.toObject();
            const auto dir = obj["directory"].toString();
            TempItem item;
            item.id = dir;
            item.category = QStringLiteral("Preview");
            item.name = QFileInfo(dir).fileName();
            item.path = dir;
            item.bytes = quint64(obj["bytes"].toDouble());
            item.status = QStringLiteral("Disposable");
            item.reason = QStringLiteral("Temporary preview view");
            item.canClean = true;
            result.append(item);
        }
    }

    // 2. Nested archives
    if (nestedStore_) {
        for (const auto &val : nestedStore_->records()) {
            const auto obj = val.toObject();
            const auto id = obj["id"].toString();
            const auto dir = nestedStore_->directory(id);
            const bool isActive = activeNestedIds.contains(id);

            quint64 bytes = 0;
            if (!dir.isEmpty()) {
                for (const auto &f : QDir(dir).entryInfoList(QDir::Files | QDir::NoDotAndDotDot))
                    bytes += quint64(f.size());
            }

            TempItem item;
            item.id = id;
            item.category = QStringLiteral("Nested");
            item.name = obj["parentEntry"].toString();
            item.path = dir;
            item.bytes = bytes;
            item.status = isActive ? QStringLiteral("In use") : QStringLiteral("Closed");
            item.reason = isActive ? QStringLiteral("Active tab viewing nested archive")
                                   : QStringLiteral("Closed nested container; safe to reclaim");
            item.canClean = !isActive;
            result.append(item);
        }
    }

    // 3. Working copies (edits)
    if (editStore_) {
        for (const auto &val : editStore_->records()) {
            const auto rec = val.toObject();
            const auto id = rec["id"].toString();
            const auto file = rec["file"].toString();
            const QFileInfo fi(file);

            const QString currentHash = EditStore::fingerprint(file);
            const QString savedHash = rec["fingerprint"].toString();
            const bool isDirty = !savedHash.isEmpty() && currentHash != savedHash;

            TempItem item;
            item.id = id;
            item.category = QStringLiteral("WorkingCopy");
            item.name = rec["entry"].toString();
            item.path = file;
            item.bytes = quint64(fi.size());
            item.status = isDirty ? QStringLiteral("Unsaved modifications")
                                  : QStringLiteral("Synchronized with edit state");
            item.reason = isDirty ? QStringLiteral("Preserved to prevent data loss")
                                  : QStringLiteral("Registered working copy; requires explicit removal");
            item.canClean = false; // Never auto-clean working copies!
            result.append(item);
        }
    }

    return result;
}

QVector<TempStore::TempItem> TempStore::previewCleanupList(const QSet<QString> &activeNestedIds) const {
    QVector<TempItem> list;
    for (const auto &item : allItems(activeNestedIds)) {
        if (item.canClean)
            list.append(item);
    }
    return list;
}

int TempStore::cleanItems(const QVector<TempItem> &items) {
    int count = 0;
    for (const auto &item : items) {
        if (!item.canClean)
            continue;
        const QString cat = item.category.toLower();
        if (cat == QLatin1String("preview") && externalFiles_) {
            if (externalFiles_->remove(item.path))
                ++count;
        } else if (cat == QLatin1String("nested") && nestedStore_) {
            if (nestedStore_->discard(item.id))
                ++count;

        }
    }
    return count;
}

