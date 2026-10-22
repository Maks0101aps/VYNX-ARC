#include "ArchiveModel.h"
#include "widgets/IconProvider.h"
#include <QApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QStyle>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>

static QString text(const rust::String &s) {
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}
QString displaySize(quint64 bytes) {
    return QLocale().formattedDataSize(
        qint64(qMin(bytes, quint64(std::numeric_limits<qint64>::max()))));
}
int ArchiveModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : rows_.size();
}
int ArchiveModel::columnCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : 8; }
QVariant ArchiveModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::TextAlignmentRole)
        return int((section >= 1 && section <= 3 ? Qt::AlignRight : Qt::AlignLeft) |
                   Qt::AlignVCenter);
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    const QStringList columns{tr("Name"), tr("Size"),     tr("Packed"), tr("Ratio"),
                              tr("Type"), tr("Modified"), tr("CRC32"),  tr("Path")};
    return columns.value(section);
}
QVariant ArchiveModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= rows_.size())
        return {};
    const auto &r = rows_[index.row()];
    if (role == Qt::UserRole) {
        switch (index.column()) {
        case 0:
            return r.folder;
        case 1:
            return QVariant::fromValue(r.size);
        case 2:
            return QVariant::fromValue(r.packed);
        case 3:
            return r.size ? 1.0 - double(r.packed) / double(r.size) : 0.0;
        case 5:
            return r.modifiedKnown ? r.modifiedTime : std::numeric_limits<qint64>::min();
        default:
            return {};
        }
    }
    if (role == Qt::ToolTipRole)
        return index.column() == 5
                   ? (r.modifiedKnown
                          ? QDateTime::fromSecsSinceEpoch(r.modifiedTime).toString(Qt::ISODate)
                          : tr("Unknown"))
                   : r.fullPath;
    if (role == Qt::DecorationRole && index.column() == 0)
        return Icons::file(r.name, r.folder);
    if (role == Qt::TextAlignmentRole && index.column() >= 1 && index.column() <= 3)
        return int(Qt::AlignRight | Qt::AlignVCenter);
    if (role != Qt::DisplayRole)
        return {};
    switch (index.column()) {
    case 0:
        return r.name;
    case 1:
        return r.folder ? QString() : displaySize(r.size);
    case 2:
        return r.folder || r.packed == 0 ? QString() : displaySize(r.packed);
    case 3:
        return !r.folder && r.size && r.packed
                   ? QString::number(100.0 * (1.0 - double(r.packed) / double(r.size)), 'f', 1) +
                         "%"
                   : QString();
    case 4:
        return r.folder ? tr("Folder") : QFileInfo(r.name).suffix().toUpper();
    case 5:
        return r.modifiedKnown ? QLocale().toString(QDateTime::fromSecsSinceEpoch(r.modifiedTime),
                                                    QLocale::ShortFormat)
                               : QStringLiteral("—");
    case 6:
        return r.crc;
    case 7:
        return r.fullPath;
    default:
        return {};
    }
}
static QVector<ArchiveRow> folderRows(const QVector<ArchiveRow> &flat, const QString &folder,
                                      const QCollator &collator) {
    QVector<ArchiveRow> rows;
    QHash<QString, int> folders;
    const QString prefix = folder.isEmpty() ? QString() : folder + "/";
    for (const auto &entry : flat) {
        if (!entry.fullPath.startsWith(prefix))
            continue;
        const auto relative = entry.fullPath.mid(prefix.size());
        if (relative.isEmpty())
            continue;
        const auto slash = relative.indexOf('/');
        if (slash >= 0 || entry.folder) {
            const auto name = slash >= 0 ? relative.left(slash) : relative;
            if (!folders.contains(name)) {
                ArchiveRow row;
                row.name = name;
                row.fullPath = prefix + name;
                row.folder = true;
                row.sortKey = collator.sortKey(name);
                folders.insert(name, rows.size());
                rows.append(row);
            }
            auto &row = rows[folders.value(name)];
            row.memberIds.append(entry.id);
            row.size += entry.size;
            row.packed += entry.packed;
        } else
            rows.append(entry);
    }
    return rows;
}
std::shared_ptr<PreparedArchive> ArchiveModel::prepare(rust::Vec<vynx::EntryInfo> entries) {
    auto prepared = std::make_shared<PreparedArchive>();
    prepared->flat.reserve(qsizetype(entries.size()));
    bool directories = false, nested = false;
    auto add = [](quint64 a, quint64 b) {
        return b > std::numeric_limits<quint64>::max() - a ? std::numeric_limits<quint64>::max()
                                                           : a + b;
    };
    for (const auto &entry : entries) {
        ArchiveRow row;
        row.fullPath = text(entry.name);
        row.fullPath.replace('\\', '/');
        if (row.fullPath.endsWith('/'))
            row.fullPath.chop(1);
        row.name = row.fullPath.section('/', -1);
        row.sortKey = prepared->collator.sortKey(row.name);
        row.id = entry.id;
        row.folder = entry.directory;
        row.size = entry.size;
        row.packed = entry.packed;
        row.crc = text(entry.crc);
        row.modified = text(entry.modified);
        row.memberIds = {entry.id};
        if (entry.modified_known && !row.modified.startsWith("1980-01-01") &&
            entry.modified_unix <= quint64(std::numeric_limits<qint64>::max()) &&
            QDateTime::fromSecsSinceEpoch(qint64(entry.modified_unix)).isValid()) {
            row.modifiedKnown = true;
            row.modifiedTime = qint64(entry.modified_unix);
        }
        prepared->totalSize = add(prepared->totalSize, entry.size);
        prepared->totalPacked = add(prepared->totalPacked, entry.packed);
        directories |= row.folder;
        nested |= row.fullPath.contains('/');
        prepared->flat.append(std::move(row));
    }
    if (!directories)
        prepared->search = prepared->flat;
    else {
        prepared->search.reserve(prepared->flat.size());
        for (const auto &row : prepared->flat)
            if (!row.folder)
                prepared->search.append(row);
    }
    prepared->root = (!directories && !nested) ? prepared->flat
                                               : folderRows(prepared->flat, {}, prepared->collator);
    return prepared;
}
void ArchiveModel::load(rust::Vec<vynx::EntryInfo> entries) {
    loadPrepared(prepare(std::move(entries)));
}
void ArchiveModel::loadPrepared(std::shared_ptr<PreparedArchive> prepared) {
    beginResetModel();
    prepared_ = std::move(prepared);
    folderCache_.clear();
    folder_.clear();
    searchMode_ = false;
    totalSize_ = prepared_->totalSize;
    totalPacked_ = prepared_->totalPacked;
    rows_ = prepared_->root;
    endResetModel();
}
quint64 ArchiveModel::totalSize() const { return totalSize_; }
void ArchiveModel::setSearchMode(bool enabled) {
    if (searchMode_ == enabled)
        return;
    searchMode_ = enabled;
    navigate(folder_);
}
void ArchiveModel::navigate(const QString &folder) {
    beginResetModel();
    folder_ = folder;
    if (!prepared_)
        rows_.clear();
    else if (searchMode_)
        rows_ = prepared_->search;
    else if (folder.isEmpty())
        rows_ = prepared_->root;
    else if (const auto *cached = folderCache_.object(folder))
        rows_ = *cached;
    else {
        rows_ = folderRows(prepared_->flat, folder, prepared_->collator);
        qsizetype cost = rows_.size();
        for (const auto &row : rows_)
            cost += row.memberIds.size();
        if (cost <= folderCache_.maxCost())
            folderCache_.insert(folder, new QVector<ArchiveRow>(rows_), int(cost));
    }
    endResetModel();
}
bool ArchiveFilter::filterAcceptsRow(int row, const QModelIndex &parent) const {
    if (literalMode_ && literal_.isEmpty())
        return true;
    if (const auto *model = qobject_cast<const ArchiveModel *>(sourceModel())) {
        const auto &entry = model->row(row);
        if (literalMode_)
            return entry.name.contains(literal_, filterCaseSensitivity()) ||
                   entry.fullPath.contains(literal_, filterCaseSensitivity());
        const auto pattern = filterRegularExpression();
        return entry.name.contains(pattern) || entry.fullPath.contains(pattern);
    }
    return QSortFilterProxyModel::filterAcceptsRow(row, parent);
}
quint64 ArchiveModel::totalPacked() const { return totalPacked_; }
bool ArchiveFilter::lessThan(const QModelIndex &a, const QModelIndex &b) const {
    const auto *model = qobject_cast<const ArchiveModel *>(sourceModel());
    if (!model)
        return QSortFilterProxyModel::lessThan(a, b);
    const auto &left = model->row(a.row());
    const auto &right = model->row(b.row());
    if (left.folder != right.folder)
        return sortOrder() == Qt::AscendingOrder ? left.folder : !left.folder;
    if (a.column() == 1)
        return left.size < right.size;
    if (a.column() == 2)
        return left.packed < right.packed;
    if (a.column() == 3)
        return (left.size ? 1.0 - double(left.packed) / double(left.size) : 0.0) <
               (right.size ? 1.0 - double(right.packed) / double(right.size) : 0.0);
    if (a.column() == 5)
        return (left.modifiedKnown ? left.modifiedTime : std::numeric_limits<qint64>::min()) <
               (right.modifiedKnown ? right.modifiedTime : std::numeric_limits<qint64>::min());
    if (a.column() == 0)
        return left.sortKey && right.sortKey
                   ? left.sortKey->compare(*right.sortKey) < 0
                   : QString::localeAwareCompare(left.name, right.name) < 0;
    return QSortFilterProxyModel::lessThan(a, b);
}

Qt::ItemFlags ArchiveModel::flags(const QModelIndex &index) const {
    auto defaultFlags = QAbstractTableModel::flags(index);
    if (index.isValid())
        return defaultFlags | Qt::ItemIsDragEnabled;
    return defaultFlags;
}

Qt::DropActions ArchiveModel::supportedDragActions() const {
    return Qt::CopyAction;
}

QStringList ArchiveModel::mimeTypes() const {
    return {QStringLiteral("application/x-vynx-archive-copy")};
}

QMimeData *ArchiveModel::mimeData(const QModelIndexList &indexes) const {
    auto *mime = new QMimeData;
    QSet<int> rowSet;
    for (const auto &idx : indexes) {
        if (idx.isValid())
            rowSet.insert(idx.row());
    }
    QJsonArray itemsArr;
    for (int r : rowSet) {
        if (r >= 0 && r < rows_.size()) {
            const auto &row = rows_[r];
            QJsonObject obj;
            obj["id"] = double(row.id);
            obj["name"] = row.name;
            obj["fullPath"] = row.fullPath;
            obj["size"] = double(row.size);
            obj["folder"] = row.folder;
            QJsonArray mArr;
            for (auto mid : row.memberIds)
                mArr.append(double(mid));
            obj["memberIds"] = mArr;
            itemsArr.append(obj);
        }
    }
    QJsonObject root;
    root["items"] = itemsArr;
    root["sourceArchive"] = archivePath_;
    root["sourceSessionId"] = sourceSessionId_;
    const QFileInfo source(archivePath_);
    root["sourceFingerprint"] = QString("%1:%2").arg(source.size()).arg(source.lastModified().toMSecsSinceEpoch());
    mime->setData("application/x-vynx-archive-copy", QJsonDocument(root).toJson(QJsonDocument::Compact));
    return mime;
}
