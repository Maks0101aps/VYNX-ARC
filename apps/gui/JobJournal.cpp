#include "JobJournal.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
JobJournal::JobJournal() = default;
JobJournal::~JobJournal() = default;
static bool validState(const QString &state) {
    return QSet<QString>{"pending", "running",   "decision",   "completed",
                         "failed",  "cancelled", "interrupted"}
        .contains(state);
}
QJsonObject JobJournal::sanitizeSpec(const QJsonObject &spec) {
    QJsonObject result;
    // Never serialize arbitrary worker captures or credentials.
    const QStringList strings{"kind",    "destination", "folder",      "newName",  "editId",
                              "batchId", "retryOf",     "archiveHash", "inputHash"};
    const QStringList numbers{"action", "format", "preset", "resource", "conflict", "volumeSize"};
    for (const auto &key : strings)
        if (spec[key].isString())
            result[key] = spec[key].toString().left(32768);
    for (const auto &key : numbers)
        if (spec[key].isDouble())
            result[key] = spec[key];
    for (const auto &key : {QString("sources"), QString("names")}) {
        QJsonArray values;
        for (const auto &value : spec[key].toArray()) {
            if (values.size() >= 10000)
                break;
            if (value.isString())
                values.append(value.toString().left(32768));
        }
        result[key] = values;
    }
    for (const auto &key : {QString("smart"), QString("needsPassword")})
        if (spec[key].isBool())
            result[key] = spec[key];
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
        QFile::remove(path_);
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
            document.object()["schema"].toInt() != 1 || !document.object()["jobs"].isArray() ||
            document.object()["jobs"].toArray().size() > 96) {
            error_ = "Unsupported or damaged journal; original file preserved";
            return false;
        }
        for (const auto &value : document.object()["jobs"].toArray()) {
            auto record = value.toObject();
            if (!validState(record["state"].toString()) || record["id"].toString().isEmpty() ||
                !record["spec"].isObject()) {
                records_ = {};
                error_ = "Damaged journal record; original file preserved";
                return false;
            }
            record["spec"] = sanitizeSpec(record["spec"].toObject());
            if (record["state"] == "running" || record["state"] == "pending" ||
                record["state"] == "decision") {
                record["state"] = "interrupted";
                record["error"] = "Application stopped. Review parameters and explicitly retry; no "
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
    const auto bytes = QJsonDocument(QJsonObject{{"schema", 1}, {"jobs", records_}}).toJson();
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
    save();
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
    if (!enabled)
        return !QFile::exists(path_) || QFile::remove(path_);
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
