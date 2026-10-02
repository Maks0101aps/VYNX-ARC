#pragma once
#include <QWidget>
class QAction;
class QListWidget;
class HomePage : public QWidget {
    Q_OBJECT
  public:
    HomePage(QAction *open, QAction *create, QWidget *parent = nullptr);
    QListWidget *recent;
  signals:
    void clearHistory();
    void openRequested(const QString &path);
    void removeRecent(const QString &path);
};
