#pragma once
#include <QHash>
#include <QList>
#include <QWidget>

class QAction;
class QToolButton;
class QLineEdit;
class QTableView;
class QFrame;
class QMenu;
class BreadcrumbBar;

class ArchivePage : public QWidget {
    Q_OBJECT
  public:
    ArchivePage(QAction *extract, QAction *add, QAction *test, QAction *remove, QAction *rename,
                QAction *hash, const QList<QAction *> &overflow,
                QAction *view = nullptr, QAction *find = nullptr,
                QAction *wizard = nullptr, QAction *info = nullptr,
                QAction *repair = nullptr, QWidget *parent = nullptr);

    // Navigation controls
    QLineEdit *search = nullptr;
    QTableView *table = nullptr;
    BreadcrumbBar *breadcrumb = nullptr;
    QToolButton *back = nullptr;
    QToolButton *up = nullptr;

    // Toolbar buttons
    QToolButton *addButton = nullptr;
    QToolButton *extractButton = nullptr;
    QToolButton *testButton = nullptr;
    QToolButton *viewButton = nullptr;
    QToolButton *deleteButton = nullptr;
    QToolButton *renameButton = nullptr;

    QToolButton *findButton = nullptr;
    QToolButton *wizardButton = nullptr;
    QToolButton *infoButton = nullptr;

    QToolButton *repairButton = nullptr;
    QToolButton *moreButton = nullptr;

    // Action members
    QAction *extractAction = nullptr;
    QAction *addAction = nullptr;
    QAction *testAction = nullptr;
    QAction *removeAction = nullptr;
    QAction *renameAction = nullptr;
    QAction *hashAction = nullptr;
    QAction *viewAction = nullptr;
    QAction *findAction = nullptr;
    QAction *wizardAction = nullptr;
    QAction *infoAction = nullptr;
    QAction *repairAction = nullptr;

    void setWritable(bool writable);
    void setColumnPreference(int column, bool visible);

  protected:
    void resizeEvent(QResizeEvent *event) override;

  private:
    void adapt();
    QList<QToolButton *> mutations_;
    QToolButton *hash_ = nullptr;
    QToolButton *test_ = nullptr;
    bool writable_ = false;
    QHash<int, bool> columnPreferences_;
    QList<QAction *> overflowActions_;
    QMenu *moreMenu_ = nullptr;
    QFrame *separator1_ = nullptr;
    QFrame *separator2_ = nullptr;
};
