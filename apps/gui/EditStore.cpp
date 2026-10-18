#include "EditStore.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <limits>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
static bool plain(const QString &path) {
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
EditStore::EditStore(QString root, quint64 quota) : root_(std::move(root)), quota_(quota) {
    if (root_.isEmpty())
        root_ = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
                "/WorkingCopies";
    root_ = QFileInfo(root_).absoluteFilePath();
    if (!QDir().mkpath(root_) || !plain(root_)) {
        error_ = "Unsafe or unavailable working-copy directory";
        return;
    }
    lock_ = std::make_unique<QLockFile>(root_ + "/.owner.lock");
    lock_->setStaleLockTime(0);
    if (!lock_->tryLock())
        error_ = "Another application instance owns working copies";
}
EditStore::~EditStore() = default;
bool EditStore::available() const { return lock_ && lock_->isLocked(); }
QString EditStore::directory(const QString &id) const {
    if (QUuid(id).isNull() || id != QUuid(id).toString(QUuid::WithoutBraces))
        return {};
    const auto path = root_ + "/edit-" + id;
    return plain(path) ? path : QString();
}
bool EditStore::write(const QString &directory, const QJsonObject &data) {
    if (!plain(directory))
        return false;
    QSaveFile output(directory + "/.edit.json");
    const auto bytes = QJsonDocument(data).toJson();
    return bytes.size() <= 65536 && output.open(QIODevice::WriteOnly) &&
           output.write(bytes) == bytes.size() && output.commit();
}
QString EditStore::reserve(const QString &archive, const QString &entry, quint64 expectedBytes) {
    const auto used = usedBytes();
    if (!available() || records().size() >= 32 || expectedBytes > quota_ ||
        used > quota_ - expectedBytes)
        return {};
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto path = root_ + "/edit-" + id;
    if (!QDir().mkdir(path))
        return {};
    if (!write(path, QJsonObject{
                         {"schema", 1},
                         {"id", id},
                         {"archive", archive},
                         {"entry", entry},
                         {"state", "preparing"},
                         {"created", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}}))
        return {};
    return id;
}
QJsonObject EditStore::record(const QString &id) const {
    const auto path = directory(id);
    if (path.isEmpty())
        return {};
    QFile input(path + "/.edit.json");
    if (input.size() > 65536 || input.size() < 1 || QFileInfo(input).isSymLink() ||
        !input.open(QIODevice::ReadOnly))
        return {};
    auto value = QJsonDocument::fromJson(input.readAll()).object();
    if (value["schema"].toInt() != 1 || value["id"] != id)
        return {};
    return value;
}
QJsonArray EditStore::records() const {
    QJsonArray result;
    if (!available())
        return result;
    for (const auto &name : QDir(root_).entryList({"edit-*"}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        const auto value = record(name.mid(5));
        if (!value.isEmpty())
            result.append(value);
        if (result.size() >= 32)
            break;
    }
    return result;
}
bool EditStore::update(const QString &id, const QJsonObject &data) {
    if (!available())
        return false;
    const auto path = directory(id);
    if (path.isEmpty())
        return false;
    auto current = record(id);
    if (current.isEmpty())
        return false;
    for (auto it = data.begin(); it != data.end(); ++it)
        current[it.key()] = it.value();
    current["id"] = id;
    current["schema"] = 1;
    return write(path, current);
}
bool EditStore::validPayload(const QJsonObject &record) const {
    const auto dir = directory(record["id"].toString());
    const QFileInfo file(record["file"].toString());
    return !dir.isEmpty() && file.absolutePath() == dir && file.isFile() && !file.isSymLink() &&
           file.canonicalFilePath() == QDir(dir).canonicalPath() + "/" + file.fileName();
}
QString EditStore::fingerprint(const QString &path) {
    const QFileInfo file(path);
    return QString::number(file.size()) + ":" +
           QString::number(file.lastModified().toMSecsSinceEpoch());
}
bool EditStore::discard(const QString &id) {
    if (!available())
        return false;
    const auto path = directory(id);
    if (path.isEmpty())
        return false;
    const auto entries = QDir(path).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System |
                                                  QDir::NoDotAndDotDot);
    for (const auto &entry : entries)
        if (!entry.isFile() || entry.isSymLink())
            return false;
    for (const auto &entry : entries) {
        if (entry.fileName() == ".edit.json")
            continue;
        QFile file(entry.absoluteFilePath());
        file.setPermissions(file.permissions() | QFileDevice::WriteOwner);
        if (!file.remove())
            return false;
    }
    if (!QFile::remove(path + "/.edit.json"))
        return false;
    return QDir(root_).rmdir("edit-" + id);
}

quint64 EditStore::usedBytes() const {
    if (!available())
        return 0;
    quint64 total = 0;
    for (const auto &name : QDir(root_).entryList({"edit-*"}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        const auto path = directory(name.mid(5));
        if (path.isEmpty())
            continue;
        for (const auto &file : QDir(path).entryInfoList(QDir::Files | QDir::NoSymLinks |
                                                         QDir::Hidden | QDir::System)) {
            const auto bytes = quint64(qMax<qint64>(0, file.size()));
            if (total > (~quint64(0)) - bytes)
                return (~quint64(0));
            total += bytes;
        }
    }
    return total;
}
