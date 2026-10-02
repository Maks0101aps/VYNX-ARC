#include "ArchiveModel.h"
#include <QApplication>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QStyle>

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
int ArchiveModel::columnCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : 7; }
QVariant ArchiveModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    const QStringList columns{tr("Name"), tr("Size"),     tr("Packed"), tr("Ratio"),
                              tr("Type"), tr("Modified"), tr("CRC32")};
    return columns.value(section);
}
QVariant ArchiveModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= rows_.size())
        return {};
    const auto &r = rows_[index.row()];
    if (role == Qt::UserRole)
        return index.column() == 0 ? QVariant(r.folder) : QVariant::fromValue(r.size);
    if (role == Qt::ToolTipRole)
        return r.fullPath;
    if (role == Qt::DecorationRole && index.column() == 0)
        return QApplication::style()->standardIcon(r.folder ? QStyle::SP_DirIcon
                                                            : QStyle::SP_FileIcon);
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
        return r.size && r.packed
                   ? QString::number(100.0 * (1.0 - double(r.packed) / double(r.size)), 'f', 1) +
                         "%"
                   : QString();
    case 4:
        return r.folder ? tr("Folder") : QFileInfo(r.name).suffix().toUpper();
    case 5:
        return r.modified;
    case 6:
        return r.crc;
    default:
        return {};
    }
}
void ArchiveModel::load(rust::Vec<vynx::EntryInfo> entries) {
    entries_ = std::move(entries);
    navigate({});
}
quint64 ArchiveModel::totalSize() const {
    quint64 total = 0;
    for (const auto &e : entries_) {
        if (e.size > std::numeric_limits<quint64>::max() - total)
            return std::numeric_limits<quint64>::max();
        total += e.size;
    }
    return total;
}
void ArchiveModel::navigate(const QString &folder) {
    beginResetModel();
    folder_ = folder;
    rows_.clear();
    QHash<QString, int> folders;
    const QString prefix = folder.isEmpty() ? QString() : folder + "/";
    for (const auto &e : entries_) {
        QString full = text(e.name);
        full.replace('\\', '/');
        if (!full.startsWith(prefix))
            continue;
        QString relative = full.mid(prefix.size());
        if (relative.endsWith('/'))
            relative.chop(1);
        if (relative.isEmpty())
            continue;
        int slash = relative.indexOf('/');
        if (slash >= 0 || e.directory) {
            QString name = slash >= 0 ? relative.left(slash) : relative;
            if (!folders.contains(name)) {
                folders.insert(name, rows_.size());
                ArchiveRow r;
                r.name = name;
                r.fullPath = prefix + name;
                r.folder = true;
                rows_.append(r);
            }
            rows_[folders.value(name)].memberIds.append(e.id);
        } else {
            ArchiveRow r;
            r.name = relative;
            r.fullPath = full;
            r.id = e.id;
            r.size = e.size;
            r.packed = e.packed;
            r.crc = text(e.crc);
            r.modified = text(e.modified);
            r.memberIds = {e.id};
            rows_.append(r);
        }
    }
    endResetModel();
}
bool ArchiveFilter::lessThan(const QModelIndex &a, const QModelIndex &b) const {
    bool af = sourceModel()->data(a.siblingAtColumn(0), Qt::UserRole).toBool();
    bool bf = sourceModel()->data(b.siblingAtColumn(0), Qt::UserRole).toBool();
    if (af != bf)
        return sortOrder() == Qt::AscendingOrder ? af : !af;
    if (a.column() == 1)
        return sourceModel()->data(a, Qt::UserRole).toULongLong() <
               sourceModel()->data(b, Qt::UserRole).toULongLong();
    return QString::localeAwareCompare(sourceModel()->data(a).toString(),
                                       sourceModel()->data(b).toString()) < 0;
}
