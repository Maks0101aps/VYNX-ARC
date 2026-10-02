#include "ArchiveModel.h"
#include "widgets/IconProvider.h"
#include <QApplication>
#include <QDateTime>
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
    if (orientation == Qt::Horizontal && role == Qt::TextAlignmentRole)
        return int((section >= 1 && section <= 3 ? Qt::AlignRight : Qt::AlignLeft) |
                   Qt::AlignVCenter);
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
    default:
        return {};
    }
}
void ArchiveModel::load(rust::Vec<vynx::EntryInfo> entries) {
    entries_ = std::move(entries);
    totalSize_ = totalPacked_ = 0;
    auto add = [](quint64 a, quint64 b) {
        return b > std::numeric_limits<quint64>::max() - a ? std::numeric_limits<quint64>::max()
                                                           : a + b;
    };
    for (const auto &entry : entries_) {
        totalSize_ = add(totalSize_, entry.size);
        totalPacked_ = add(totalPacked_, entry.packed);
    }
    navigate({});
}
quint64 ArchiveModel::totalSize() const { return totalSize_; }
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
            rows_[folders.value(name)].size += e.size;
            rows_[folders.value(name)].packed += e.packed;
        } else {
            ArchiveRow r;
            r.name = relative;
            r.fullPath = full;
            r.id = e.id;
            r.size = e.size;
            r.packed = e.packed;
            r.crc = text(e.crc);
            r.modified = text(e.modified);
            if (e.modified_known && !r.modified.startsWith("1980-01-01")) {
                if (e.modified_unix <= quint64(std::numeric_limits<qint64>::max()) &&
                    QDateTime::fromSecsSinceEpoch(qint64(e.modified_unix)).isValid()) {
                    r.modifiedTime = qint64(e.modified_unix);
                    r.modifiedKnown = true;
                }
            }
            r.memberIds = {e.id};
            rows_.append(r);
        }
    }
    endResetModel();
}
quint64 ArchiveModel::totalPacked() const { return totalPacked_; }
bool ArchiveFilter::lessThan(const QModelIndex &a, const QModelIndex &b) const {
    bool af = sourceModel()->data(a.siblingAtColumn(0), Qt::UserRole).toBool();
    bool bf = sourceModel()->data(b.siblingAtColumn(0), Qt::UserRole).toBool();
    if (af != bf)
        return sortOrder() == Qt::AscendingOrder ? af : !af;
    if (a.column() == 1 || a.column() == 2)
        return sourceModel()->data(a, Qt::UserRole).toULongLong() <
               sourceModel()->data(b, Qt::UserRole).toULongLong();
    if (a.column() == 3)
        return sourceModel()->data(a, Qt::UserRole).toDouble() <
               sourceModel()->data(b, Qt::UserRole).toDouble();
    if (a.column() == 5)
        return sourceModel()->data(a, Qt::UserRole).toLongLong() <
               sourceModel()->data(b, Qt::UserRole).toLongLong();
    return QString::localeAwareCompare(sourceModel()->data(a).toString(),
                                       sourceModel()->data(b).toString()) < 0;
}
