#pragma once
#include <QHash>
#include <QWidget>
class QAction;
class QToolButton;
class QLineEdit;
class QTableView;
class BreadcrumbBar;
class ArchivePage : public QWidget {
    Q_OBJECT
  public:
    ArchivePage(QAction *extract, QAction *add, QAction *test, QAction *remove, QAction *rename,
                QAction *hash, const QList<QAction *> &overflow, QWidget *parent = nullptr);
    QLineEdit *search;
    QTableView *table = nullptr;
    BreadcrumbBar *breadcrumb;
    QToolButton *back, *up;
    void setWritable(bool writable);
    void setColumnPreference(int column, bool visible);

  protected:
    void resizeEvent(QResizeEvent *event) override;

  private:
    void adapt();
    QList<QToolButton *> mutations_;
    QToolButton *hash_, *test_;
    bool writable_ = false;
    QHash<int, bool> columnPreferences_;
};
