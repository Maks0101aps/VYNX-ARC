#pragma once

#include "../UpdateService.h"
#include <QDialog>
#include <QString>

class QLabel;
class QProgressBar;
class QPushButton;
class QTextBrowser;

class UpdateDialog : public QDialog {
    Q_OBJECT

public:
    explicit UpdateDialog(const ReleaseInfo &release, QWidget *parent = nullptr, UpdateService *updateService = nullptr);
    ~UpdateDialog() override;

    const ReleaseInfo &release() const { return release_; }
    QString downloadedPath() const { return downloadedPath_; }
    bool isPortable() const { return isPortable_; }

    QLabel *titleLabel() const { return titleLabel_; }
    QLabel *subtitleLabel() const { return subtitleLabel_; }
    QTextBrowser *notesBrowser() const { return notesBrowser_; }
    QProgressBar *progressBar() const { return progressBar_; }
    QLabel *statusLabel() const { return statusLabel_; }
    QPushButton *actionButton() const { return actionButton_; }
    QPushButton *cancelButton() const { return cancelButton_; }

public slots:
    void startDownload();
    void reject() override;

signals:
    void readyToInstall(const QString &downloadedPath, bool isPortable);
    void installRequested(const QString &downloadedPath, bool isPortable);

public slots:
    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void onDownloadComplete(const QString &filePath);
    void onDownloadError(const QString &error);

private slots:
    void onActionButtonClicked();

private:
    ReleaseInfo release_;
    UpdateService *updateService_ = nullptr;
    bool ownsUpdateService_ = false;

    QLabel *titleLabel_ = nullptr;
    QLabel *subtitleLabel_ = nullptr;
    QTextBrowser *notesBrowser_ = nullptr;
    QProgressBar *progressBar_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QPushButton *actionButton_ = nullptr;
    QPushButton *cancelButton_ = nullptr;

    bool downloading_ = false;
    bool downloadComplete_ = false;
    bool isPortable_ = false;
    QString downloadedPath_;
};
