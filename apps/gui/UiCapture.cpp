#include "EditStore.h"
#include "ExternalFiles.h"
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
// Explicit screenshot fixtures in isolated capture storage; no user files are modified.
QWidget *MainWindow::prepareCapture(const QString &mode) {
    QDialog *dialog = nullptr;
    if (mode == "tabs" && session_->archive) {
        const auto first = session_;
        search_->setText("README");
        for (const auto &name : {QString("Photos.7z"), QString("Documents.zip")}) {
            auto next = std::make_shared<ArchiveSession>();
            next->path = QFileInfo(first->path).absolutePath() + "/" + name;
            next->archive = first->archive;
            next->model->load(vynx::list_entries(**first->archive));
            sessions_.append(next);
            tabs_->addTab(name);
        }
        tabs_->show();
        return this;
    }
    if (mode == "working-copies" && session_->archive) {
        const auto key = edits_->reserve(session_->path, "Project/README.md");
        QFile file(edits_->directory(key) + "/README.md");
        if (!file.open(QIODevice::WriteOnly))
            return this;
        file.write("Changed working content\n");
        file.close();
        edits_->update(key, QJsonObject{{"file", file.fileName()},
                                        {"state", "needs-review"},
                                        {"fingerprint", "original"}});
        QTimer::singleShot(0, this, &MainWindow::showWorkingCopies);
        return this;
    }
    if (mode == "temporary-files") {
        const auto dir = externalFiles_->create();
        QFile file(dir + "/README.md");
        if (!file.open(QIODevice::WriteOnly))
            return this;
        file.write("Temporary view");
        file.close();
        QTimer::singleShot(0, this, &MainWindow::showTemporaryFiles);
        return this;
    }
    if (mode == "review" && session_->archive) {
        if (!journal_.open(transientStorage_->path() + "/jobs.json", true))
            return this;
        QJsonObject spec{{"kind", "extract"},
                         {"sources", QJsonArray{session_->path}},
                         {"destination", QDir::tempPath() + "/Extracted"}};
        auto id = journal_.add(tr("Extract"), spec);
        journal_.setState(id, "failed", "IO: Fixture publication conflict");
        auto *item = new QListWidgetItem(tr("Stopped"), queueView_);
        item->setData(Qt::UserRole + 1, tr("Extract"));
        item->setData(Qt::UserRole + 2, true);
        item->setData(Qt::UserRole + 3, id);
        queueView_->setCurrentItem(item);
        QTimer::singleShot(0, this, &MainWindow::retrySelectedJob);
        return this;
    }
    if (mode == "create")
        dialog = new CreateArchiveDialog({session_->path}, 1, this);
    else if (mode == "create-stream")
        dialog = new CreateArchiveDialog({session_->path}, 5, this);
    else if (mode == "batch") {
        auto *batch = new BatchExtractDialog(this);
        batch->files->addItems(
            {session_->path, QFileInfo(session_->path).absolutePath() + "/Photos.7z"});
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
    } else if (mode == "search" && session_->archive) {
        search_->setText("README");
        return this;
    } else if ((mode == "preview" || mode == "preview-image" || mode == "preview-hex" ||
                mode == "external") &&
               session_->archive) {
        search_->setText(mode == "preview-image" ? "cover.png"
                         : mode == "preview-hex" ? "sample.bin"
                                                 : "README.md");
        if (session_->filter->rowCount()) {
            table_->selectRow(0);
            if (mode == "external")
                QTimer::singleShot(0, this, &MainWindow::openSelectedExternally);
            else
                QTimer::singleShot(0, this, &MainWindow::previewSelected);
        }
        return this;
    } else if (mode == "archive" && session_->archive)
        navigate("Project");
    else if (mode == "breadcrumb" && session_->archive)
        navigate("Project/src/components");
    if (dialog) {
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        return dialog;
    }
    return this;
}
