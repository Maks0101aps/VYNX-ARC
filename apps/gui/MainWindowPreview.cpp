#include "ExternalFiles.h"
#include "MainWindow.h"
#include "SecretUtf8.h"
#include "dialogs/PreviewDialog.h"
#include "widgets/ToastOverlay.h"
#include <QtWidgets>

void MainWindow::previewSelected() {
    if (session_->busy || !session_->archive)
        return;
    const auto selection = table_->selectionModel()->selectedRows();
    if (selection.size() != 1)
        return;
    const auto &row = session_->model->row(session_->filter->mapToSource(selection.first()).row());
    if (row.folder)
        return;
    const auto id = row.id;
    const auto name = row.fullPath;
    auto archive = session_->archive;
    auto pw = session_->password ? session_->password : secret({});
    auto result = std::make_shared<PreviewContent>();
    if (operations_.busy) {
        auto reader = std::make_shared<OperationController>();
        reader->owner = session_;
        reader->operation = std::make_shared<rust::Box<vynx::Operation>>(vynx::new_operation());
        auto readOp = reader->operation;
        vynx::configure_operation(
            **readOp, 2, uint8_t(qBound(0, settings_->value("resourceMode", 1).toInt(), 2)));
        reader->owner->busy = true;
        readers_.append(reader);
        auto *progress =
            new QProgressDialog(tr("Reading file preview..."), tr("Cancel"), 0, 0, this);
        progress->setWindowModality(Qt::NonModal);
        progress->setMinimumDuration(0);
        connect(progress, &QProgressDialog::canceled, this, [readOp] { vynx::cancel(**readOp); });
        std::weak_ptr<OperationController> weak = reader;
        connect(&reader->watcher(), &QFutureWatcher<QString>::finished, this,
                [this, weak, progress, result, name] {
                    auto completed = weak.lock();
                    if (!completed)
                        return;
                    const auto error = completed->watcher().result();
                    progress->close();
                    progress->deleteLater();
                    completed->owner->busy = false;
                    if (completed->owner == session_)
                        updateSessionUi();
                    readers_.removeAll(completed);
                    if (completed->owner->closeWhenIdle) {
                        closeSession(sessions_.indexOf(completed->owner));
                        return;
                    }
                    if (!error.isEmpty()) {
                        if (!error.startsWith("CANCELLED:"))
                            QMessageBox::warning(this, tr("Operation stopped"), error);
                        return;
                    }
                    PreviewDialog dialog(name, *result, false, this);
                    dialog.exec();
                });
        reader->start([archive, pw, readOp, result, id] {
            auto bytes = vynx::preview_bytes(**archive, id, pw->bytes(), **readOp);
            *result = PreviewDialog::decode(
                QByteArray(reinterpret_cast<const char *>(bytes.data()), qsizetype(bytes.size())));
            return vynx::cancelled(**readOp) ? QStringLiteral("CANCELLED: Preview cancelled")
                                             : QString();
        });
        updateSessionUi();
        return;
    }
    beginOperation();
    auto op = operations_.operation;
    runJob(
        tr("Reading file preview..."),
        [archive, pw, op, result, id] {
            auto bytes = vynx::preview_bytes(**archive, id, pw->bytes(), **op);
            *result = PreviewDialog::decode(
                QByteArray(reinterpret_cast<const char *>(bytes.data()), qsizetype(bytes.size())));
            return vynx::cancelled(**op) ? QStringLiteral("CANCELLED: Preview cancelled")
                                         : QString();
        },
        [this, result, name] {
            PreviewDialog dialog(name, *result, ExternalFiles::allowed(name), this);
            if (dialog.exec() == QDialog::Accepted)
                QTimer::singleShot(0, this, &MainWindow::openSelectedExternally);
        });
}
void MainWindow::openSelectedExternally() {
    if (operations_.busy || !session_->archive)
        return;
    const auto selection = table_->selectionModel()->selectedRows();
    if (selection.size() != 1)
        return;
    const auto &row = session_->model->row(session_->filter->mapToSource(selection.first()).row());
    if (row.folder)
        return;
    if (!ExternalFiles::allowed(row.fullPath)) {
        QMessageBox::information(
            this, tr("Open externally"),
            tr("This file type cannot be opened directly. Extract it to a chosen folder instead."));
        return;
    }
    QMessageBox confirmation(QMessageBox::Question, tr("Open externally"),
                             tr("Open %1 in its associated application? A read-only temporary copy "
                                "will be saved on this computer. "
                                "Edits will not be saved back to the archive. Copies older than "
                                "seven days are cleaned at startup; "
                                "the external application may keep its own copies.")
                                 .arg(row.fullPath),
                             QMessageBox::Open | QMessageBox::Cancel, this);
    confirmation.setTextFormat(Qt::PlainText);
    confirmation.setDefaultButton(QMessageBox::Cancel);
    if (confirmation.exec() != QMessageBox::Open)
        return;
    const auto directory = externalFiles_->create();
    if (directory.isEmpty()) {
        QMessageBox::warning(this, tr("Open externally"),
                             tr("Could not create a private temporary folder."));
        return;
    }
    const auto id = row.id;
    auto archive = session_->archive;
    auto pw = session_->password ? session_->password : secret({});
    auto path = std::make_shared<QString>();
    beginOperation();
    auto op = operations_.operation;
    runJob(
        tr("Preparing external file..."),
        [archive, pw, op, id, directory, path] {
            const auto value = vynx::materialize_entry(
                **archive, id, directory.toUtf8().toStdString(), pw->bytes(), **op);
            *path = QString::fromUtf8(value.data(), qsizetype(value.size()));
            return QString();
        },
        [this, path, op] {
            if (vynx::cancelled(**op))
                return;
            if (!externalFiles_->open(*path))
                QMessageBox::warning(this, tr("Open externally"),
                                     tr("Windows could not open this file. Check its default "
                                        "application or extract it manually."));
        });
}
