#include "MainWindow.h"
#include "dialogs/AboutDialog.h"
#include "dialogs/BatchExtractDialog.h"
#include "dialogs/ConflictDialog.h"
#include "dialogs/CreateArchiveDialog.h"
#include "dialogs/ExtractDialog.h"
#include "dialogs/SettingsDialog.h"
#include "pages/ArchivePage.h"
#include "widgets/OperationPanel.h"
#include "widgets/ToastOverlay.h"
#include <QtWidgets>
// Explicit screenshot fixtures only: no extraction, mutation or conflict replies.
QWidget *MainWindow::prepareCapture(const QString &mode) {
    QDialog *dialog = nullptr;
    if (mode == "create")
        dialog = new CreateArchiveDialog({archivePath_}, 1, this);
    else if (mode == "create-stream")
        dialog = new CreateArchiveDialog({archivePath_}, 5, this);
    else if (mode == "batch") {
        auto *batch = new BatchExtractDialog(this);
        batch->files->addItems(
            {archivePath_, QFileInfo(archivePath_).absolutePath() + "/Photos.7z"});
        dialog = batch;
    } else if (mode == "extract")
        dialog = new ExtractDialog(
            QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/Project", true,
            false, false, {}, this);
    else if (mode == "conflict")
        dialog = new ConflictDialog("Project/README.md", 12576, 1760000000, "README.md", 16384,
                                    1760001000, this);
    else if (mode == "settings" || mode == "settings-resources") {
        dialog = new SettingsDialog(settings_->value("theme").toString(),
                                    settings_->value("language").toString(), false, this);
        if (mode == "settings-resources")
            dialog->findChild<QTabWidget *>()->setCurrentIndex(2);
    } else if (mode == "about")
        dialog = new AboutDialog(this);
    else if (mode == "operation") {
        toast_->hide();
        operationView_->begin(tr("Extracting Project.7z"));
        operationView_->updateProgress(0, 528ULL << 20, "Project/src/components/README.md", 0,
                                       tr("Extracting and verifying files"),
                                       tr("7Z decoder worker limit 2"));
        operationView_->updateProgress(
            380ULL << 20, 528ULL << 20, "Project/src/components/README.md", 5000,
            tr("Extracting and verifying files"), tr("7Z decoder worker limit 2"));
        return this;
    } else if (mode == "queue") {
        queueDock_->show();
        new QListWidgetItem(tr("Completed — %1").arg("Project.zip"), queueView_);
        const auto id = queue_.enqueue("Project.7z", [] {});
        auto *item = new QListWidgetItem(tr("Pending — %1").arg("Project.7z"), queueView_);
        item->setData(Qt::UserRole, id);
        item->setData(Qt::UserRole + 1, "Project.7z");
        queueView_->setCurrentItem(item);
        updateQueue();
        return this;
    } else if (mode == "search" && archive_) {
        search_->setText("README");
        return this;
    } else if ((mode == "preview" || mode == "preview-image" || mode == "preview-hex" ||
                mode == "external") &&
               archive_) {
        search_->setText(mode == "preview-image" ? "cover.png"
                         : mode == "preview-hex" ? "sample.bin"
                                                 : "README.md");
        if (filter_->rowCount()) {
            table_->selectRow(0);
            if (mode == "external")
                QTimer::singleShot(0, this, &MainWindow::openSelectedExternally);
            else
                QTimer::singleShot(0, this, &MainWindow::previewSelected);
        }
        return this;
    } else if (mode == "archive" && archive_)
        navigate("Project");
    else if (mode == "breadcrumb" && archive_)
        navigate("Project/src/components");
    if (dialog) {
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        return dialog;
    }
    return this;
}
