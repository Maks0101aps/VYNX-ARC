#pragma once
#include "ArchiveModel.h"
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QMainWindow>
#include <functional>
#include <memory>
class QTableView;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QLabel;
class QStackedWidget;
class QListWidget;
class QSettings;
class QTimer;
class QAction;

class MainWindow : public QMainWindow {
    Q_OBJECT
  public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    void openPath(const QString &path);
    bool smokeTest();
    void handleShellRequest(quint32 action, const QStringList &paths, const QString &password = {});

  protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

  private:
    void chooseOpen();
    void chooseCreate(const QStringList &initial = {}, int initialFormat = 0);
    void chooseModify(int kind, const QStringList &sources = {}, bool folders = false);
    bool writableArchive() const;
    void chooseExtract(bool smart = false, bool here = false, bool named = false);
    void testArchive();
    void hashFile();
    void hashContents(bool verify = false, bool wholeArchive = false);
    void navigate(const QString &folder);
    void back();
    void up();
    void settings();
    void applyTheme(const QString &theme);
    void updateRecent();
    void updateStatus();
    void runJob(const QString &title, std::function<QString()> worker,
                std::function<void()> success = {}, std::function<void(QString)> failure = {});
    void openWithPassword(const QString &path, const QString &password);
    void beginOperation();
    void showConflict();
    QString askPassword(bool *accepted);
    QByteArray password_;
    QString archivePath_;
    QStringList history_;
    std::shared_ptr<rust::Box<vynx::Archive>> archive_;
    std::shared_ptr<rust::Box<vynx::Operation>> operation_;
    std::unique_ptr<QSettings> settings_;
    ArchiveModel *model_;
    ArchiveFilter *filter_;
    QTableView *table_;
    QLineEdit *search_;
    QLabel *breadcrumb_;
    QLabel *current_;
    QLabel *rate_;
    QProgressBar *progress_;
    QPushButton *cancel_;
    QWidget *operationPanel_;
    QStackedWidget *pages_;
    QListWidget *recent_;
    QFutureWatcher<QString> watcher_;
    QTimer *timer_;
    QElapsedTimer elapsed_;
    QList<QAction *> archiveActions_;
    QList<QAction *> idleActions_;
    std::function<void()> success_;
    std::function<void(QString)> failure_;
    bool busy_ = false;
    bool closePending_ = false;
    quint64 shownConflict_ = 0;
};
