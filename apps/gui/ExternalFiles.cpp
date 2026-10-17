#include "ExternalFiles.h"
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QLockFile>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
static bool plainDirectory(const QString &path) {
    const QFileInfo info(path);
    if (!info.isDir() || info.isSymLink())
        return false;
#ifdef Q_OS_WIN
    const auto attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(path.utf16()));
    if (attributes == INVALID_FILE_ATTRIBUTES || attributes & FILE_ATTRIBUTE_REPARSE_POINT)
        return false;
#endif
    return true;
}
ExternalFiles::ExternalFiles(QString root) : root_(std::move(root)) {
    if (root_.isEmpty())
        root_ =
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/OpenFiles";
    root_ = QDir::cleanPath(QFileInfo(root_).absoluteFilePath());
    if (!QDir().mkpath(root_) || !plainDirectory(root_))
        root_.clear();
    cleanup();
}
ExternalFiles::~ExternalFiles() = default;
bool ExternalFiles::allowed(const QString &name) {
    static const QSet<QString> extensions{
        "txt", "log",  "csv", "json", "md",   "xml", "yaml", "yml",  "ini",  "toml", "png",
        "jpg", "jpeg", "bmp", "gif",  "webp", "pdf", "docx", "xlsx", "pptx", "odt",  "ods",
        "odp", "mp3",  "wav", "flac", "ogg",  "mp4", "mkv",  "webm", "mov"};
    return extensions.contains(QFileInfo(name).suffix().toLower());
}
QString ExternalFiles::create() {
    if (root_.isEmpty() || !plainDirectory(root_))
        return {};
    QTemporaryDir directory(root_ + "/view-XXXXXX");
    if (!directory.isValid())
        return {};
    const auto path = directory.path();
    auto lock = std::make_unique<QLockFile>(path + "/.owner.lock");
    lock->setStaleLockTime(0);
    if (!lock->tryLock())
        return {};
    QFile marker(path + "/.vynx-view");
    if (!marker.open(QIODevice::WriteOnly | QIODevice::NewOnly) ||
        marker.write("VYNX ARC external view v1\n") < 0)
        return {};
    marker.close();
    directory.setAutoRemove(false);
    sessions_.push_back({path, std::move(lock)});
    return path;
}
bool ExternalFiles::open(const QString &path) const {
    if (!allowed(path) || !QFileInfo(path).isFile() || QFileInfo(path).isSymLink())
        return false;
    const auto parent = QFileInfo(path).absolutePath();
    bool owned = false;
    for (const auto &session : sessions_)
        owned |= session.path == parent;
    return owned && plainDirectory(parent) && QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}
int ExternalFiles::cleanup() {
    if (root_.isEmpty() || !plainDirectory(root_))
        return 0;
    int removed = 0;
    const QDir root(root_);
    const auto deadline = QDateTime::currentDateTimeUtc().addDays(-7);
    for (const auto &info : root.entryInfoList({"view-*"}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        const auto path = info.absoluteFilePath();
        if (!plainDirectory(path))
            continue;
        QFile marker(path + "/.vynx-view");
        if (marker.size() > 64 || QFileInfo(marker).isSymLink() ||
            !marker.open(QIODevice::ReadOnly) || marker.read(64) != "VYNX ARC external view v1\n" ||
            QFileInfo(marker).lastModified().toUTC() > deadline)
            continue;
        marker.close();
        QLockFile lock(path + "/.owner.lock");
        lock.setStaleLockTime(0);
        if (!lock.tryLock())
            continue;
        const QDir directory(path);
        const auto entries = directory.entryInfoList(QDir::AllEntries | QDir::Hidden |
                                                     QDir::System | QDir::NoDotAndDotDot);
        bool flat = true;
        for (const auto &entry : entries)
            if (!entry.isFile() || entry.isSymLink())
                flat = false;
        if (!flat)
            continue;
        bool success = true;
        for (const auto &entry : entries) {
            if (entry.fileName() == ".owner.lock" || entry.fileName() == ".vynx-view")
                continue;
            QFile file(entry.absoluteFilePath());
            file.setPermissions(file.permissions() | QFileDevice::WriteOwner);
            success &= file.remove();
        }
        if (success) {
            success = QFile::remove(path + "/.vynx-view");
            lock.unlock();
            if (success && root.rmdir(info.fileName()))
                ++removed;
        }
    }
    return removed;
}

QJsonArray ExternalFiles::items() const {
    QJsonArray result;
    if (root_.isEmpty() || !plainDirectory(root_))
        return result;
    for (const auto &info :
         QDir(root_).entryInfoList({"view-*"}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!plainDirectory(info.absoluteFilePath()))
            continue;
        QFile marker(info.absoluteFilePath() + "/.vynx-view");
        if (marker.size() > 64 || QFileInfo(marker).isSymLink() ||
            !marker.open(QIODevice::ReadOnly) || marker.read(64) != "VYNX ARC external view v1\n")
            continue;
        quint64 bytes = 0;
        for (const auto &file :
             QDir(info.absoluteFilePath()).entryInfoList(QDir::Files | QDir::NoDotAndDotDot))
            if (!file.fileName().startsWith('.') && !file.isSymLink())
                bytes += quint64(file.size());
        result.append(QJsonObject{{"directory", info.absoluteFilePath()},
                                  {"bytes", double(bytes)},
                                  {"created", info.birthTime().toString(Qt::ISODate)}});
        if (result.size() >= 1000)
            break;
    }
    return result;
}
bool ExternalFiles::remove(const QString &directory) {
    if (root_.isEmpty() || QFileInfo(directory).absolutePath() != root_ ||
        !plainDirectory(directory))
        return false;
    QFile marker(directory + "/.vynx-view");
    if (marker.size() > 64 || QFileInfo(marker).isSymLink() || !marker.open(QIODevice::ReadOnly) ||
        marker.read(64) != "VYNX ARC external view v1\n")
        return false;
    marker.close();
    for (auto it = sessions_.begin(); it != sessions_.end(); ++it)
        if (it->path == directory) {
            sessions_.erase(it);
            break;
        }
    QLockFile lock(directory + "/.owner.lock");
    lock.setStaleLockTime(0);
    if (!lock.tryLock())
        return false;
    const auto files = QDir(directory).entryInfoList(QDir::AllEntries | QDir::Hidden |
                                                     QDir::System | QDir::NoDotAndDotDot);
    for (const auto &file : files)
        if (!file.isFile() || file.isSymLink())
            return false;
    for (const auto &file : files) {
        if (file.fileName() == ".owner.lock" || file.fileName() == ".vynx-view")
            continue;
        QFile payload(file.absoluteFilePath());
        payload.setPermissions(payload.permissions() | QFileDevice::WriteOwner);
        if (!payload.remove())
            return false;
    }
    if (!QFile::remove(directory + "/.vynx-view"))
        return false;
    lock.unlock();
    return QDir(root_).rmdir(QFileInfo(directory).fileName());
}
