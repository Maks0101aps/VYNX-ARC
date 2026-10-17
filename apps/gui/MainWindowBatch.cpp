#include "MainWindow.h"
#include "SecretUtf8.h"
#include "dialogs/BatchExtractDialog.h"
#include "widgets/ToastOverlay.h"
#include <QtWidgets>
void MainWindow::chooseBatchExtract() {
    BatchExtractDialog dialog(this);
    dialog.destination->setText(
        settings_->value("batchDestination", dialog.destination->text()).toString());
    if (dialog.exec() != QDialog::Accepted)
        return;
    const auto paths = dialog.archives();
    if (queue_.size() + size_t(paths.size()) > 32) {
        QMessageBox::information(
            this, tr("Batch extraction"),
            tr("The queue supports up to 32 pending jobs. Remove some requests and try again."));
        return;
    }
    const auto pw = secret(dialog.password->text());
    dialog.password->clear();
    const quint8 policies[]{4, 1, 2, 0};
    settings_->setValue("batchDestination", dialog.destination->text());
    enqueueBatch(paths, dialog.destination->text(), policies[dialog.conflicts->currentIndex()], pw);
}
void MainWindow::enqueueBatch(const QStringList &paths, const QString &destination, quint8 conflict,
                              const std::shared_ptr<SecretUtf8> &pw) {
    const auto destinations = batchDestinations(paths, destination);
    const auto batchId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    for (int i = 0; i < paths.size(); ++i) {
        const auto path = paths[i];
        const auto output = destinations[i];
        const auto title = tr("Extract %1").arg(QFileInfo(path).fileName());
        enqueueRequest(
            title,
            [this, path, output, conflict, pw, title] {
                beginOperation();
                auto op = operations_.operation;
                runJob(
                    title,
                    [path, output, conflict, pw, op] {
                        auto archive =
                            vynx::open_archive(path.toUtf8().toStdString(), pw->bytes(), **op);
                        vynx::extract_archive(*archive, output.toUtf8().toStdString(), {}, conflict,
                                              false, pw->bytes(), **op);
                        return QString();
                    },
                    {},
                    [this](const QString &error) {
                        toast_->notify(tr("Batch job stopped. See the queue for details."));
                        Q_UNUSED(error);
                    });
            },
            path + "\n" + output,
            QJsonObject{{"kind", "extract"},
                        {"batchId", batchId},
                        {"sources", QJsonArray{path}},
                        {"destination", output},
                        {"conflict", int(conflict)},
                        {"smart", false},
                        {"needsPassword", !pw->bytes().empty()},
                        {"resource", settings_->value("resourceMode", 1).toInt()}});
    }
}
