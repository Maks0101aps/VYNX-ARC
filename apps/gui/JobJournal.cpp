#include "JobJournal.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QRegularExpression>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
static bool safeFile(const QString &path) {
    QFileInfo info(path);
    if (!info.isFile() || info.isSymLink())
        return false;
#ifdef Q_OS_WIN
    const auto flags = GetFileAttributesW(reinterpret_cast<LPCWSTR>(path.utf16()));
    if (flags == INVALID_FILE_ATTRIBUTES || flags & FILE_ATTRIBUTE_REPARSE_POINT)
        return false;
#endif
    return true;
}
static bool safeStore(const QString &path) {
    QFileInfo info(path);
    if (!info.isDir() || info.isSymLink())
        return false;
#ifdef Q_OS_WIN
    const auto flags = GetFileAttributesW(reinterpret_cast<LPCWSTR>(path.utf16()));
    if (flags == INVALID_FILE_ATTRIBUTES || flags & FILE_ATTRIBUTE_REPARSE_POINT)
        return false;
#endif
    return true;
}
static void prunePayloads(const QString &dataRoot, const QSet<QString> &referenced = {}) {
    // Only delete obsolete content-addressed descriptions after the replacement
    // journal is durable. Preserve damaged/unrecognized files for diagnosis.
    if (safeStore(dataRoot))
        for (const auto &name :
             QDir(dataRoot).entryList({"*.json"}, QDir::Files | QDir::NoSymLinks)) {
            if (referenced.contains(name) ||
                !QRegularExpression("^[0-9a-f]{64}\\.json$").match(name).hasMatch())
                continue;
            QFile old(dataRoot + "/" + name);
            if (safeFile(old.fileName()) && old.size() <= 16 * 1024 * 1024 &&
                old.open(QIODevice::ReadOnly) &&
                QCryptographicHash::hash(old.readAll(), QCryptographicHash::Sha256).toHex() ==
                    name.left(64).toLatin1()) {
                old.close();
                QFile::remove(old.fileName());
            }
        }
}
JobJournal::JobJournal() = default;
JobJournal::~JobJournal() = default;
static bool validState(const QString &state) {
    return QSet<QString>{"pending",   "running",     "decision", "completed", "failed",
                         "cancelled", "interrupted", "unknown",  "verifying"}
        .contains(state);
}
QJsonObject JobJournal::sanitizeSpec(const QJsonObject &spec) {
    QJsonObject result;
    // Never serialize arbitrary worker captures or credentials.
    const QStringList strings{"kind",    "destination", "folder",      "newName",  "editId",
                              "batchId", "retryOf",     "archiveHash", "inputHash"};
    const QStringList numbers{"action", "format", "preset", "resource", "conflict", "volumeSize"};
    for (const auto &key : strings)
        if (spec[key].isString()) {
            if (spec[key].toString().size() > 32768)
                result["incompleteSpec"] = true;
            result[key] = spec[key].toString().left(32768);
        }
    for (const auto &key : numbers)
        if (spec[key].isDouble())
            result[key] = spec[key];
    for (const auto &key : {QString("sources"), QString("names")}) {
        QJsonArray values;
        if (spec[key].toArray().size() > 1000000)
            result["incompleteSpec"] = true;
        for (const auto &value : spec[key].toArray()) {
            if (values.size() >= 1000000)
                break;
            if (value.isString()) {
                if (value.toString().size() > 32768)
                    result["incompleteSpec"] = true;
                values.append(value.toString().left(32768));
            } else
                result["incompleteSpec"] = true;
        }
        result[key] = values;
    }
    for (const auto &key : {QString("smart"), QString("needsPassword"), QString("incompleteSpec")})
        if (spec[key].isBool())
            result[key] = result[key].toBool() || spec[key].toBool();
    return result;
}
QJsonArray JobJournal::sourceSnapshot(const QJsonObject &spec) {
    QJsonArray sources;
    for (const auto &value : spec["sources"].toArray()) {
        QFileInfo file(value.toString());
        sources.append(
            QJsonObject{{"path", file.absoluteFilePath()},
                        {"exists", file.exists()},
                        {"directory", file.isDir()},
                        {"size", QString::number(file.size())},
                        {"modified", QString::number(file.lastModified().toMSecsSinceEpoch())}});
    }
    return sources;
}
bool JobJournal::sourcesUnchanged(const QJsonObject &record) {
    return record["sources"].toArray() == sourceSnapshot(record["spec"].toObject());
}
bool JobJournal::open(const QString &path, bool enabled) {
    path_ = path;
    records_ = {};
    error_.clear();
    enabled_ = false;
    if (!QDir().mkpath(QFileInfo(path_).absolutePath())) {
        error_ = "Cannot create journal directory";
        return false;
    }
    lock_ = std::make_unique<QLockFile>(path_ + ".lock");
    lock_->setStaleLockTime(0);
    if (!lock_->tryLock()) {
        error_ = "Another application instance owns this journal";
        return false;
    }
    if (!enabled) {
        if (QFile::exists(path_) && !QFile::remove(path_)) {
            error_ = "Cannot remove disabled queue history";
            return false;
        }
        prunePayloads(path_ + ".data");
        return true;
    }
    QFile input(path_);
    if (input.exists()) {
        if (input.size() > 1024 * 1024 || !input.open(QIODevice::ReadOnly)) {
            error_ = "Journal is too large or unreadable";
            return false;
        }
        QJsonParseError parse;
        const auto document = QJsonDocument::fromJson(input.readAll(), &parse);
        if (parse.error != QJsonParseError::NoError || !document.isObject() ||
            (document.object()["schema"].toInt() != 1 &&
             document.object()["schema"].toInt() != 2) ||
            !document.object()["jobs"].isArray() ||
            document.object()["jobs"].toArray().size() > 96) {
            error_ = "Unsupported or damaged journal; original file preserved";
            return false;
        }
        qint64 totalPayload = 0;
        for (const auto &value : document.object()["jobs"].toArray()) {
            auto record = value.toObject();
            if (record.contains("payload")) {
                const auto ref = record["payload"].toObject();
                const auto digest = ref["sha256"].toString();
                const auto dataPath = path_ + ".data/" + digest + ".json";
                QFile data(dataPath);
                if (!QRegularExpression("^[0-9a-f]{64}$").match(digest).hasMatch() ||
                    !safeStore(path_ + ".data") || !safeFile(dataPath) || data.size() < 1 ||
                    data.size() > 16 * 1024 * 1024 || data.size() != ref["bytes"].toInteger() ||
                    (totalPayload += data.size()) > 64 * 1024 * 1024 ||
                    !data.open(QIODevice::ReadOnly)) {
                    records_ = {};
                    error_ =
                        "Missing, unsafe or oversized queue payload; original journal preserved";
                    return false;
                }
                const auto bytes = data.readAll();
                QJsonParseError failure;
                const auto payload = QJsonDocument::fromJson(bytes, &failure);
                if (QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() !=
                        digest.toLatin1() ||
                    failure.error != QJsonParseError::NoError || !payload.isObject() ||
                    payload.object()["schema"].toInt() != 1 ||
                    !payload.object()["spec"].isObject() ||
                    !payload.object()["sources"].isArray()) {
                    records_ = {};
                    error_ = "Damaged queue payload; original journal preserved";
                    return false;
                }
                record.remove("payload");
                record["spec"] = payload.object()["spec"];
                record["sources"] = payload.object()["sources"];
            }
            if (!validState(record["state"].toString()) || record["id"].toString().isEmpty() ||
                !record["spec"].isObject()) {
                records_ = {};
                error_ = "Damaged journal record; original file preserved";
                return false;
            }
            record["spec"] = sanitizeSpec(record["spec"].toObject());
            if (record["state"] == "running" || record["state"] == "pending" ||
                record["state"] == "decision") {
                const auto kind = record["spec"].toObject()["kind"].toString();
                const bool writes =
                    record["state"] != "pending" &&
                    QStringList{"create", "modify", "edit", "convert", "shell", "extract"}.contains(
                        kind);
                record["state"] = writes ? "unknown" : "interrupted";
                record["error"] =
                    writes ? "Application stopped during a possible write. Verify the destination "
                             "before any retry. No work resumed automatically."
                           : "Application stopped. Review parameters and explicitly retry; no "
                             "work resumed automatically.";
            }
            records_.append(record);
        }
    }
    enabled_ = true;
    return true;
}
bool JobJournal::save() {
    if (!enabled_)
        return true;
    if (!lock_ || !lock_->isLocked())
        return false;
    QJsonArray diskRecords;
    QSet<QString> referenced;
    const auto dataRoot = path_ + ".data";
    if (QFileInfo::exists(dataRoot)) {
        if (!safeStore(dataRoot)) {
            error_ = "Unsafe queue payload directory";
            return false;
        }
        qint64 stored = 0;
        const auto files = QDir(dataRoot).entryInfoList(QDir::Files | QDir::NoSymLinks);
        if (files.size() > 512) {
            error_ = "Queue payload directory needs review";
            return false;
        }
        for (const auto &file : files)
            stored += file.size();
        if (stored > 128 * 1024 * 1024) {
            error_ = "Queue storage budget exceeded; review retained descriptions";
            return false;
        }
    }
    qint64 totalPayload = 0;
    for (const auto &value : records_) {
        auto record = value.toObject();
        const auto payload = QJsonDocument(QJsonObject{{"schema", 1},
                                                       {"spec", record["spec"]},
                                                       {"sources", record["sources"]}})
                                 .toJson(QJsonDocument::Compact);
        if (payload.size() > 16 * 1024 * 1024 ||
            (totalPayload += payload.size()) > 64 * 1024 * 1024) {
            error_ = "Complete queue descriptions exceed storage budget; request was not persisted";
            return false;
        }
        if (payload.size() > 8192) {
            const auto root = path_ + ".data";
            if (!QDir().mkpath(root) || !safeStore(root)) {
                error_ = "Unsafe queue payload directory";
                return false;
            }
            const auto digest = QString::fromLatin1(
                QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
            const auto name = digest + ".json";
            QFile existing(root + "/" + name);
            if (existing.exists()) {
                if (!safeFile(existing.fileName()) || existing.size() != payload.size() ||
                    !existing.open(QIODevice::ReadOnly) || existing.readAll() != payload) {
                    error_ = "Existing queue payload is damaged; preserved for review";
                    return false;
                }
            } else {
                QSaveFile data(existing.fileName());
                data.setDirectWriteFallback(false);
                if (!data.open(QIODevice::WriteOnly) || data.write(payload) != payload.size() ||
                    !data.commit()) {
                    error_ = "Cannot save complete queue description";
                    return false;
                }
            }
            referenced.insert(name);
            record.remove("spec");
            record.remove("sources");
            record["payload"] = QJsonObject{{"sha256", digest}, {"bytes", payload.size()}};
        }
        diskRecords.append(record);
    }
    const auto bytes = QJsonDocument(QJsonObject{{"schema", 2}, {"jobs", diskRecords}}).toJson();
    if (bytes.size() > 1024 * 1024) {
        error_ = "Journal size limit exceeded";
        return false;
    }
    QSaveFile output(path_);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() ||
        !output.commit()) {
        error_ = output.errorString();
        return false;
    }
    prunePayloads(dataRoot, referenced);
    error_.clear();
    return true;
}
QString JobJournal::add(const QString &title, const QJsonObject &spec) {
    while (records_.size() >= 96) {
        int removable = -1;
        for (int i = 0; i < records_.size(); ++i) {
            const auto state = records_[i].toObject()["state"].toString();
            if (state == "completed" || state == "failed" || state == "cancelled") {
                removable = i;
                break;
            }
        }
        if (removable < 0) {
            error_ = "Journal is full of unfinished jobs";
            return {};
        }
        records_.removeAt(removable);
    }
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto safe = sanitizeSpec(spec);
    records_.append(
        QJsonObject{{"id", id},
                    {"title", title.left(32768)},
                    {"state", "pending"},
                    {"spec", safe},
                    {"sources", sourceSnapshot(safe)},
                    {"error", QString()},
                    {"created", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}});
    if (!save()) {
        records_.removeLast();
        return {};
    }
    return id;
}
bool JobJournal::setState(const QString &id, const QString &state, const QString &error) {
    if (!validState(state))
        return false;
    for (int i = 0; i < records_.size(); ++i) {
        auto row = records_[i].toObject();
        if (row["id"] != id)
            continue;
        if (row["state"] == state && row["error"] == error)
            return true;
        row["state"] = state;
        row["error"] = error.left(32768);
        records_[i] = row;
        return save();
    }
    return false;
}
QJsonObject JobJournal::record(const QString &id) const {
    for (const auto &value : records_)
        if (value.toObject()["id"] == id)
            return value.toObject();
    return {};
}
bool JobJournal::remove(const QString &id) {
    for (int i = records_.size() - 1; i >= 0; --i)
        if (records_[i].toObject()["id"] == id)
            records_.removeAt(i);
    return save();
}
bool JobJournal::clear() {
    records_ = {};
    return save();
}
bool JobJournal::setEnabled(bool enabled) {
    if (enabled == enabled_)
        return true;
    if (!lock_ || !lock_->isLocked())
        return false;
    enabled_ = enabled;
    if (!enabled) {
        if (QFile::exists(path_) && !QFile::remove(path_)) {
            error_ = "Cannot remove queue history";
            return false;
        }
        prunePayloads(path_ + ".data");
        return true;
    }
    return save();
}

bool JobJournal::updateSpec(const QString &id, const QJsonObject &spec) {
    for (int i = 0; i < records_.size(); ++i) {
        auto row = records_[i].toObject();
        if (row["id"] != id)
            continue;
        row["spec"] = sanitizeSpec(spec);
        row["sources"] = sourceSnapshot(row["spec"].toObject());
        records_[i] = row;
        return save();
    }
    return false;
}
