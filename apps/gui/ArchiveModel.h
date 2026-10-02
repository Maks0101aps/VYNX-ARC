#pragma once
#include "vynx-arc-core/src/ffi.rs.h"
#include <QAbstractTableModel>
#include <QSortFilterProxyModel>
#include <QString>
#include <QVector>

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
    void load(rust::Vec<vynx::EntryInfo> entries);
    void navigate(const QString &folder);
    const ArchiveRow &row(int index) const { return rows_.at(index); }
    QString folder() const { return folder_; }
    quint64 totalSize() const;
    quint64 totalPacked() const;
    int count() const { return int(entries_.size()); }

  private:
    rust::Vec<vynx::EntryInfo> entries_;
    QVector<ArchiveRow> rows_;
    QString folder_;
    quint64 totalSize_ = 0, totalPacked_ = 0;
};
class ArchiveFilter : public QSortFilterProxyModel {
  public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

  protected:
    bool lessThan(const QModelIndex &a, const QModelIndex &b) const override;
};
