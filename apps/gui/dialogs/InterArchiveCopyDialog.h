#pragma once
#include <QDialog>
#include <QSet>
#include <QString>
#include <QVector>

struct CopyCandidateItem {
    quint64 id = 0;
    QString name;
    QString fullPath;
    quint64 size = 0;
    bool folder = false;
    bool hasConflict = false;
    QString targetPath;
    QVector<quint64> memberIds;
};

enum class CopyConflictPolicy {
    Replace = 1,
    Rename = 2,
    Skip = 3
};

class QTableWidget;
class QComboBox;
class QLabel;

class InterArchiveCopyDialog : public QDialog {
    Q_OBJECT
  public:
    InterArchiveCopyDialog(const QString &sourceArchive,
                           const QString &targetArchive,
                           const QString &targetFolder,
                           const QVector<CopyCandidateItem> &items,
                           quint64 availableSpace,
                           QWidget *parent = nullptr);

    CopyConflictPolicy policy() const;
    QVector<CopyCandidateItem> resolvedItems() const;

  private:
    void updateTable();

    QString sourceArchive_;
    QString targetArchive_;
    QString targetFolder_;
    QVector<CopyCandidateItem> items_;
    quint64 availableSpace_ = 0;

    QTableWidget *table_ = nullptr;
    QComboBox *policyCombo_ = nullptr;
    QLabel *spaceLabel_ = nullptr;
};

