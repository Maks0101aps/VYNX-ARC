#include "NestedStore.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

static bool plain(const QString &path) {
    const QFileInfo info(path);
    if (!info.isDir() || info.isSymLink())
        return false;
#ifdef Q_OS_WIN
    const auto attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(path.utf16()));
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        return false;
#endif
    return true;
}

NestedStore::NestedStore(QString root, quint64 quota) : root_(std::move(root)), quota_(quota) {
    if (root_.isEmpty())
        root_ = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
                "/NestedArchives";
    root_ = QFileInfo(root_).absoluteFilePath();
    if (!QDir().mkpath(root_) || !plain(root_)) {
        error_ = QStringLiteral("Unsafe or unavailable nested archive directory");
        return;
    }
    lock_ = std::make_unique<QLockFile>(root_ + "/.owner.lock");
    lock_->setStaleLockTime(0);
    if (!lock_->tryLock())
        error_ = QStringLiteral("Another application instance owns nested archives");
}

NestedStore::~NestedStore() = default;

bool NestedStore::available() const {
    return lock_ && lock_->isLocked();
}

QString NestedStore::directory(const QString &id) const {
    if (QUuid(id).isNull() || id != QUuid(id).toString(QUuid::WithoutBraces))
        return {};
    const auto path = root_ + "/nested-" + id;
    return plain(path) ? path : QString();
}

static bool writeManifest(const QString &directory, const QJsonObject &data) {
    if (!plain(directory))
        return false;
    QSaveFile output(directory + "/.nested.json");
    const auto bytes = QJsonDocument(data).toJson();
    return bytes.size() <= 65536 && output.open(QIODevice::WriteOnly) &&
           output.write(bytes) == bytes.size() && output.commit();
}

QString NestedStore::reserve(const QString &parentArchive, const QString &parentEntry,
                            int depth, quint64 expectedBytes) {
    if (!available() || depth > MaxDepth || expectedBytes > MaxContainerBytes)
        return {};
    const auto used = usedBytes();
    if (records().size() >= 32 || expectedBytes > quota_ || used > quota_ - expectedBytes)
        return {};
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto path = root_ + "/nested-" + id;
    if (!QDir().mkdir(path))
        return {};
    QJsonObject meta{
        {QStringLiteral("schema"), 1},
        {QStringLiteral("id"), id},
        {QStringLiteral("parentArchive"), parentArchive},
        {QStringLiteral("parentEntry"), parentEntry},
        {QStringLiteral("depth"), depth},
        {QStringLiteral("state"), QStringLiteral("extracting")},
        {QStringLiteral("created"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}
    };
    if (!writeManifest(path, meta))
        return {};
    return id;
}

QJsonObject NestedStore::record(const QString &id) const {
    const auto path = directory(id);
    if (path.isEmpty())
        return {};
    QFile input(path + "/.nested.json");
    if (input.size() > 65536 || input.size() < 1 || QFileInfo(input).isSymLink() ||
        !input.open(QIODevice::ReadOnly))
        return {};
    auto value = QJsonDocument::fromJson(input.readAll()).object();
    if (value["schema"].toInt() != 1 || value["id"].toString() != id)
        return {};
    return value;
}

QJsonArray NestedStore::records() const {
    QJsonArray result;
    if (!available())
        return result;
    for (const auto &name : QDir(root_).entryList({"nested-*"}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        const auto value = record(name.mid(7));
        if (!value.isEmpty())
            result.append(value);
        if (result.size() >= 32)
            break;
    }
    return result;
}

bool NestedStore::update(const QString &id, const QJsonObject &data) {
    const auto path = directory(id);
    if (path.isEmpty() || data["id"].toString() != id)
        return false;
    return writeManifest(path, data);
}

bool NestedStore::discard(const QString &id) {
    const auto path = directory(id);
    if (path.isEmpty())
        return false;
    const QDir dir(path);
    for (const auto &entry : dir.entryInfoList(QDir::AllEntries | QDir::Hidden |
                                               QDir::System | QDir::NoDotAndDotDot)) {
        if (entry.isDir() && !entry.isSymLink())
            QDir(entry.absoluteFilePath()).removeRecursively();
        else {
            QFile owned(entry.absoluteFilePath());
            owned.setPermissions(owned.permissions() | QFileDevice::WriteOwner);
            owned.remove();
        }
    }
    return QDir(root_).rmdir(QFileInfo(path).fileName());
}

int NestedStore::cleanup(const QSet<QString> &activeIds) {
    if (!available())
        return 0;
    int removed = 0;
    for (const auto &name : QDir(root_).entryList({"nested-*"}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        const auto id = name.mid(7);
        if (activeIds.contains(id))
            continue;
        if (discard(id))
            ++removed;
    }
    return removed;
}

quint64 NestedStore::usedBytes() const {
    quint64 total = 0;
    if (!available())
        return total;
    for (const auto &name : QDir(root_).entryList({"nested-*"}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        const auto path = root_ + "/" + name;
        if (!plain(path))
            continue;
        for (const auto &info : QDir(path).entryInfoList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot)) {
            if (!info.isSymLink())
                total += quint64(info.size());
        }
    }
    return total;
}

