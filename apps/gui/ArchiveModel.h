#pragma once
#include "vynx-arc-core/src/ffi.rs.h"
#include <QAbstractTableModel>
#include <QCache>
#include <QCollator>
#include <QSortFilterProxyModel>
#include <QString>
#include <QVector>
#include <memory>
#include <optional>

struct ArchiveRow {
    QString name;
    QString fullPath;
    quint64 id = 0;
    quint64 size = 0;
    quint64 packed = 0;
    bool folder = false;
    QString crc;
    QString modified;
    qint64 modifiedTime = 0;
    bool modifiedKnown = false;
    QVector<quint64> memberIds;
    std::optional<QCollatorSortKey> sortKey;
};
struct PreparedArchive {
    QCollator collator;
    QVector<ArchiveRow> flat, root, search;
    quint64 totalSize = 0, totalPacked = 0;
};
QString displaySize(quint64 bytes);
class ArchiveModel : public QAbstractTableModel {
    Q_OBJECT
  public:
    using QAbstractTableModel::QAbstractTableModel;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    static std::shared_ptr<PreparedArchive> prepare(rust::Vec<vynx::EntryInfo> entries);
    void loadPrepared(std::shared_ptr<PreparedArchive> prepared);
    void load(rust::Vec<vynx::EntryInfo> entries);
    void navigate(const QString &folder);
    void setSearchMode(bool enabled);
    bool searchMode() const { return searchMode_; }
    const ArchiveRow &row(int index) const { return rows_.at(index); }
    QString folder() const { return folder_; }
    quint64 totalSize() const;
    quint64 totalPacked() const;
    int count() const { return prepared_ ? int(prepared_->flat.size()) : 0; }

  private:
    std::shared_ptr<PreparedArchive> prepared_;
    QCache<QString, QVector<ArchiveRow>> folderCache_{50000};
    QVector<ArchiveRow> rows_;
    QString folder_;
    quint64 totalSize_ = 0, totalPacked_ = 0;
    bool searchMode_ = false;
};
class ArchiveFilter : public QSortFilterProxyModel {
  public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    void setFilterFixedString(const QString &text) {
        literal_ = text;
        literalMode_ = true;
        QSortFilterProxyModel::setFilterFixedString(text);
    }
    void setFilterRegularExpression(const QRegularExpression &expression) {
        literalMode_ = false;
        QSortFilterProxyModel::setFilterRegularExpression(expression);
    }

  private:
    QString literal_;
    bool literalMode_ = true;

  protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &parent) const override;
    bool lessThan(const QModelIndex &a, const QModelIndex &b) const override;
};
