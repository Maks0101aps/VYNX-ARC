#include "UpdateDialog.h"

#include <QCoreApplication>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>

UpdateDialog::UpdateDialog(const ReleaseInfo &release, QWidget *parent, UpdateService *updateService)
    : QDialog(parent), release_(release), updateService_(updateService)
{
    if (!updateService_) {
        updateService_ = new UpdateService(this);
        ownsUpdateService_ = true;
    }

    setWindowTitle(tr("Update Available"));
    setMinimumSize(520, 420);
    resize(560, 480);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 16, 20, 16);
    mainLayout->setSpacing(12);

    // Title & Version tag
    const QString versionTitle = release_.title.isEmpty()
        ? QStringLiteral("VYNX ARC %1").arg(release_.version)
        : release_.title;
    titleLabel_ = new QLabel(versionTitle, this);
    QFont titleFont = titleLabel_->font();
    titleFont.setPointSize(16);
    titleFont.setBold(true);
    titleLabel_->setFont(titleFont);
    titleLabel_->setObjectName(QStringLiteral("updateTitle"));
    mainLayout->addWidget(titleLabel_);

    subtitleLabel_ = new QLabel(tr("A new version (%1) of VYNX ARC is available.").arg(release_.version), this);
    subtitleLabel_->setObjectName(QStringLiteral("muted"));
    mainLayout->addWidget(subtitleLabel_);

    // Release notes section
    auto *notesHeader = new QLabel(tr("Release notes:"), this);
    QFont headerFont = notesHeader->font();
    headerFont.setBold(true);
    notesHeader->setFont(headerFont);
    mainLayout->addWidget(notesHeader);

    notesBrowser_ = new QTextBrowser(this);
    notesBrowser_->setObjectName(QStringLiteral("releaseNotes"));
    notesBrowser_->setReadOnly(true);
    notesBrowser_->setOpenExternalLinks(true);
    if (!release_.notes.isEmpty()) {
        notesBrowser_->setMarkdown(release_.notes);
    } else {
        notesBrowser_->setPlainText(tr("No release notes provided for this version."));
    }
    mainLayout->addWidget(notesBrowser_, 1);

    // Status label
    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName(QStringLiteral("updateStatusLabel"));
    statusLabel_->setWordWrap(true);
    statusLabel_->hide();
    mainLayout->addWidget(statusLabel_);

    // Progress bar (initially hidden, 0%)
    progressBar_ = new QProgressBar(this);
    progressBar_->setObjectName(QStringLiteral("updateProgressBar"));
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    progressBar_->setTextVisible(true);
    progressBar_->hide();
    mainLayout->addWidget(progressBar_);

    // Action buttons: "Download & Update" (#primary), "Cancel" / "Close"
    auto *buttonLayout = new QHBoxLayout;
    buttonLayout->setSpacing(10);
    buttonLayout->addStretch(1);

    cancelButton_ = new QPushButton(tr("Cancel"), this);
    cancelButton_->setObjectName(QStringLiteral("cancelButton"));
    buttonLayout->addWidget(cancelButton_);

    actionButton_ = new QPushButton(tr("Download & Update"), this);
    actionButton_->setObjectName(QStringLiteral("primary"));
    buttonLayout->addWidget(actionButton_);

    mainLayout->addLayout(buttonLayout);

    connect(actionButton_, &QPushButton::clicked, this, &UpdateDialog::onActionButtonClicked);
    connect(cancelButton_, &QPushButton::clicked, this, &UpdateDialog::reject);

    connect(updateService_, &UpdateService::downloadProgress, this, &UpdateDialog::onDownloadProgress);
    connect(updateService_, &UpdateService::downloadComplete, this, &UpdateDialog::onDownloadComplete);
    connect(updateService_, &UpdateService::downloadError, this, &UpdateDialog::onDownloadError);
}

UpdateDialog::~UpdateDialog() {
    if (updateService_ && !ownsUpdateService_) {
        updateService_->disconnect(this);
    }
}

void UpdateDialog::reject() {
    if (downloading_ && updateService_) {
        updateService_->cancelDownload();
    }
    QDialog::reject();
}

void UpdateDialog::startDownload() {
    if (downloading_ || downloadComplete_) {
        return;
    }

    downloading_ = true;
    actionButton_->setEnabled(false);
    progressBar_->show();
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    statusLabel_->show();
    statusLabel_->setText(tr("Downloading update..."));

    // Check if portable
    isPortable_ = QFile::exists(QCoreApplication::applicationDirPath() + QStringLiteral("/portable.flag"));

    updateService_->downloadAndVerify(release_, isPortable_);
}

void UpdateDialog::onActionButtonClicked() {
    if (!downloadComplete_) {
        startDownload();
    } else {
        emit installRequested(downloadedPath_, isPortable_);
        accept();
    }
}

void UpdateDialog::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal) {
    if (!downloading_) return;
    if (bytesTotal > 0) {
        progressBar_->setRange(0, 100);
        int percent = static_cast<int>((bytesReceived * 100) / bytesTotal);
        progressBar_->setValue(percent);
        statusLabel_->setText(tr("Downloading update (%1%)...").arg(percent));
    } else {
        progressBar_->setRange(0, 0);
        statusLabel_->setText(tr("Downloading update..."));
    }
}

void UpdateDialog::onDownloadComplete(const QString &filePath) {
    downloading_ = false;
    downloadComplete_ = true;
    downloadedPath_ = filePath;

    progressBar_->setRange(0, 100);
    progressBar_->setValue(100);
    statusLabel_->show();
    statusLabel_->setText(tr("Update ready! Click Install & Relaunch to complete the update."));
    actionButton_->setText(tr("Install & Relaunch"));
    actionButton_->setEnabled(true);
    cancelButton_->setText(tr("Close"));

    emit readyToInstall(downloadedPath_, isPortable_);
}

void UpdateDialog::onDownloadError(const QString &error) {
    downloading_ = false;
    progressBar_->hide();
    statusLabel_->show();
    statusLabel_->setText(tr("Download failed: %1").arg(error));
    actionButton_->setText(tr("Retry"));
    actionButton_->setEnabled(true);
}
