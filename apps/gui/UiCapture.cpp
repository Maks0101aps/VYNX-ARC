#include "MainWindow.h"
#include "dialogs/AboutDialog.h"
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
    else if (mode == "extract")
        dialog = new ExtractDialog(
            QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/Project", true,
            false, false, {}, this);
    else if (mode == "conflict")
        dialog = new ConflictDialog("Project/README.md", 12576, 1760000000, "README.md", 16384,
                                    1760001000, this);
    else if (mode == "settings")
        dialog = new SettingsDialog(settings_->value("theme").toString(),
                                    settings_->value("language").toString(), false, this);
    else if (mode == "about")
        dialog = new AboutDialog(this);
    else if (mode == "operation") {
        toast_->hide();
        operationView_->begin(tr("Extracting Project.7z"));
        operationView_->updateProgress(0, 528ULL << 20, "Project/src/components/README.md", 0);
        operationView_->updateProgress(380ULL << 20, 528ULL << 20,
                                       "Project/src/components/README.md", 5000);
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
