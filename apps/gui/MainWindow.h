#pragma once
#include "ArchiveModel.h"
#include "ArchiveSession.h"
#include "JobJournal.h"
#include "OperationController.h"
#include "OperationQueue.h"
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QJsonObject>
#include <QMainWindow>
#include <functional>
#include <memory>
class QTableView;
class QLineEdit;
class QComboBox;
class QProgressBar;
class QPushButton;
class QLabel;
class QStackedWidget;
class QListWidget;
class QListWidgetItem;
class QDockWidget;
class QMenu;
class QTabBar;
class QTemporaryDir;
class QSettings;
class QTimer;
class QAction;
class QVBoxLayout;
class SecretUtf8;
class ExternalFiles;
class EditStore;
class BreadcrumbBar;
class ArchivePage;
class OperationPanel;
class ToastOverlay;

class MainWindow : public QMainWindow {
    Q_OBJECT
  public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    void openPath(const QString &path);
    bool smokeTest();
    bool workerFaultAcceptance(const QString &mode, const QString &root);
    void handleShellRequest(quint32 action, const QStringList &paths);
    QWidget *prepareCapture(const QString &mode);

  protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

  private:
    std::shared_ptr<ArchiveSession> session_;
    QVector<std::shared_ptr<ArchiveSession>> sessions_;
    OperationController operations_;
    QVector<std::shared_ptr<OperationController>> readers_;
    QTabBar *tabs_ = nullptr;
    void bindSession(const std::shared_ptr<ArchiveSession> &session);
    void activateSession(int index);
    void closeSession(int index);
    void updateSessionUi();
    void saveWorkspace();
    void restoreWorkspace();
    QString archiveIdentity(const QString &path) const;
    void chooseOpen();
    void chooseBatchExtract();
    void chooseConversion(const QStringList &initial = {}, const QString &editId = {},
                          const QJsonObject &retry = {});
    void dispatchConversionSpec(const QJsonObject &spec,
                                const std::shared_ptr<SecretUtf8> &sourcePassword,
                                const std::shared_ptr<SecretUtf8> &outputPassword);
    void retryFailedBatch();
    bool enqueueRequest(const QString &title, std::function<void()> start,
                        const QString &details = {}, const QJsonObject &spec = {});
    void enqueueBatch(const QStringList &paths, const QString &destination, quint8 conflict,
                      const std::shared_ptr<SecretUtf8> &password);
    void movePending(int direction);
    bool queuePaused_ = false;
    JobJournal journal_;
    QJsonObject activeSpec_;
    QString journalWarning_;
    void setupJournal();
    void syncJournal();
    void retrySelectedJob();
    void verifySelectedResult();
    quint64 shownPublication_ = 0;
    void dispatchSpec(QJsonObject spec, const std::shared_ptr<SecretUtf8> &password);
    void exportJobReport();
    QLabel *queueSummary_ = nullptr;
    QComboBox *queueFilter_ = nullptr;
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
    void showProperties();
    void previewSelected();
    void openSelectedExternally();
    std::unique_ptr<QTemporaryDir> transientStorage_;
    std::unique_ptr<ExternalFiles> externalFiles_;
    std::unique_ptr<EditStore> edits_;
    void editSelected();
    void prepareEdit(quint64 id, const QString &name, bool launch,
                     quint64 expectedBytes = 256ULL << 20);
    void showWorkingCopies();
    QDockWidget *workingDock_ = nullptr;
    void saveWorkingCopy(const QString &id, const std::shared_ptr<SecretUtf8> &password);
    void reloadWorkingSource(const QString &id, const std::shared_ptr<SecretUtf8> &password);
    void reloadArchive(const QString &path, const std::shared_ptr<SecretUtf8> &password);
    void showTemporaryFiles();
    QAction *editAction_;
    void settings();
    void applyTheme(const QString &theme);
    void updateRecent();
    void updateStatus();
    void runJob(const QString &title, std::function<QString()> worker,
                std::function<void()> success = {}, std::function<void(QString)> failure = {});
    void openWithPassword(const QString &path, const QString &password);
    void openWithSecret(const QString &path, const std::shared_ptr<SecretUtf8> &password,
                        bool reload = false);
    void handleShellWithSecret(quint32 action, const QStringList &paths,
                               const std::shared_ptr<SecretUtf8> &password);
    void beginOperation();
    void setupOperations(QVBoxLayout *layout, QMenu *viewMenu);
    void startNextQueued();
    void updateQueue();
    OperationQueue queue_;
    QListWidget *queueView_;
    QDockWidget *queueDock_;
    QAction *cancelPending_;
    QListWidgetItem *runningItem_ = nullptr;
    void showConflict();
    QString askPassword(bool *accepted);
    std::unique_ptr<QSettings> settings_;
    QTableView *table_;
    QLineEdit *search_;
    BreadcrumbBar *breadcrumb_;
    ArchivePage *archivePage_;
    OperationPanel *operationView_;
    ToastOverlay *toast_;
    QLabel *archiveSummary_;
    QList<QAction *> mutationActions_;
    QAction *renameAction_, *deleteAction_;
    QAction *hashSelectedAction_, *verifySelectedAction_, *propertiesAction_, *previewAction_,
        *externalAction_;
    QLabel *current_;
    QLabel *rate_;
    QProgressBar *progress_;
    QPushButton *cancel_;
    QWidget *operationPanel_;
    QStackedWidget *pages_;
    QListWidget *recent_;
    QTimer *timer_;
    QElapsedTimer elapsed_;
    QList<QAction *> archiveActions_;
    QList<QAction *> idleActions_;
    std::function<void()> success_;
    std::function<void(QString)> failure_;
    bool closePending_ = false;
    quint64 shownConflict_ = 0;
};
