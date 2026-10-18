#include "EditStore.h"
#include "MainWindow.h"
#include "SecretUtf8.h"
#include "dialogs/ConversionDialog.h"
#include "widgets/ToastOverlay.h"
#include <QtWidgets>
void MainWindow::chooseConversion(const QStringList &initial, const QString &editId,
                                  const QJsonObject &retry) {
    auto paths = initial;
    if (paths.isEmpty())
        paths = QFileDialog::getOpenFileNames(
            this, tr("Archives to convert"), {},
            tr("Archives (*.zip *.7z *.rar *.tar *.gz *.xz *.bz2 *.zst *.lzma);;All files (*)"));
    if (paths.isEmpty())
        return;
    if (queue_.size() + size_t(paths.size()) > 32) {
        QMessageBox::information(this, tr("Convert archives"),
                                 tr("The queue supports up to 32 pending jobs."));
        return;
    }
    ConversionDialog dialog(paths, this);
    if (!editId.isEmpty()) {
        dialog.setWindowTitle(tr("Save working copy as a new archive"));
        dialog.outputName->setText(QFileInfo(paths.first()).completeBaseName() + " - edited");
    }
    if (!retry.isEmpty()) {
        const auto previous = retry["spec"].toObject();
        dialog.destination->setText(QFileInfo(previous["destination"].toString()).absolutePath());
        if (paths.size() == 1)
            dialog.outputName->setText(
                QFileInfo(previous["destination"].toString()).completeBaseName());
        dialog.format->setCurrentIndex(qBound(0, previous["format"].toInt(), 1));
        dialog.preset->setCurrentIndex(qBound(0, previous["preset"].toInt(2), 3));
        dialog.resource->setCurrentIndex(qBound(0, previous["resource"].toInt(1), 2));
        dialog.encrypt->setChecked(previous["needsOutputPassword"].toBool());
        if (!dialog.encrypt->isChecked())
            dialog.allowDecrypt->setChecked(previous["allowDecrypt"].toBool());
        dialog.conflict->setCurrentIndex(1);
    }
    if (session_->password && paths.size() == 1 &&
        archiveIdentity(session_->path) == archiveIdentity(paths.first()))
        dialog.sourcePassword->setText(session_->password->text());
    if (retry.isEmpty())
        dialog.resource->setCurrentIndex(qBound(0, settings_->value("resourceMode", 1).toInt(), 2));
    if (dialog.exec() != QDialog::Accepted)
        return;
    const auto sourcePassword = secret(dialog.sourcePassword->text()),
               outputPassword =
                   secret(dialog.encrypt->isChecked() ? dialog.outputPassword->text() : QString());
    dialog.sourcePassword->clear();
    dialog.outputPassword->clear();
    const auto batch = QUuid::createUuid().toString(QUuid::WithoutBraces);
    for (const auto &plan : dialog.plan()) {
        const QFileInfo input(plan.source);
        QJsonObject spec{
            {"kind", "convert"},
            {"sources", QJsonArray{input.absoluteFilePath()}},
            {"destination", plan.output},
            {"format", dialog.format->currentIndex()},
            {"preset", dialog.preset->currentIndex()},
            {"resource", dialog.resource->currentIndex()},
            {"allowDecrypt", dialog.allowDecrypt->isChecked()},
            {"needsPassword", !sourcePassword->bytes().empty()},
            {"needsOutputPassword", !outputPassword->bytes().empty()},
            {"batchId", batch},
            {"sourceSize", QString::number(input.size())},
            {"sourceModified", QString::number(input.lastModified().toMSecsSinceEpoch())}};
        if (!editId.isEmpty())
            spec["editId"] = editId;
        if (!retry.isEmpty())
            spec["retryOf"] = retry["origins"]
                                  .toObject()
                                  .value(input.absoluteFilePath())
                                  .toString(retry["id"].toString());
        const auto title = editId.isEmpty()
                               ? tr("Convert %1").arg(input.fileName())
                               : tr("Save edited %1 as new archive").arg(input.fileName());
        enqueueRequest(
            title,
            [this, spec, sourcePassword, outputPassword] {
                dispatchConversionSpec(spec, sourcePassword, outputPassword);
            },
            input.absoluteFilePath() + "\n" + plan.output, spec);
    }
}
void MainWindow::dispatchConversionSpec(const QJsonObject &spec,
                                        const std::shared_ptr<SecretUtf8> &sourcePassword,
                                        const std::shared_ptr<SecretUtf8> &outputPassword) {
    const auto source = spec["sources"].toArray().first().toString();
    const auto editId = spec["editId"].toString();
    const auto edit = editId.isEmpty() ? QJsonObject() : edits_->record(editId);
    if (!editId.isEmpty() && !edits_->validPayload(edit))
        return;
    beginOperation();
    activeSpec_ = spec;
    auto op = operations_.operation;
    vynx::configure_operation(**op, uint8_t(spec["preset"].toInt(2)),
                              uint8_t(spec["resource"].toInt(1)));
    auto receipt = std::make_shared<QString>();
    runJob(
        tr("Converting archive..."),
        [source, spec, edit, sourcePassword, outputPassword, op, receipt] {
            const QFileInfo info(source);
            if (!spec["sourceSize"].toString().isEmpty() &&
                (QString::number(info.size()) != spec["sourceSize"].toString() ||
                 QString::number(info.lastModified().toMSecsSinceEpoch()) !=
                     spec["sourceModified"].toString()))
                return QStringLiteral(
                    "CHANGED: Source changed after planning; review a new conversion plan");
            auto archive =
                vynx::open_archive(source.toUtf8().toStdString(), sourcePassword->bytes(), **op);
            rust::String result;
            if (edit.isEmpty())
                result = vynx::convert_archive(
                    *archive, spec["destination"].toString().toUtf8().toStdString(),
                    uint8_t(spec["format"].toInt()), sourcePassword->bytes(),
                    outputPassword->bytes(), spec["allowDecrypt"].toBool(), **op);
            else {
                const auto input = edit["file"].toString();
                vynx::CopySaveInfo request;
                request.name = edit["entry"].toString().toUtf8().toStdString();
                request.input = input.toUtf8().toStdString();
                request.archive_hash = edit["archiveHash"].toString().toStdString();
                request.input_hash = vynx::hash_archive_file(input.toUtf8().toStdString(), **op);
                request.output = spec["destination"].toString().toUtf8().toStdString();
                request.format = uint8_t(spec["format"].toInt());
                request.allow_decrypt = spec["allowDecrypt"].toBool();
                result = vynx::save_copy_as(*archive, request, sourcePassword->bytes(),
                                            outputPassword->bytes(), **op);
            }
            *receipt = QString::fromUtf8(result.data(), qsizetype(result.size()));
            return QString();
        },
        [this, spec, editId, receipt] {
            if (!editId.isEmpty())
                edits_->update(editId, QJsonObject{{"exportArchive", spec["destination"]},
                                                   {"exportArchiveHash", *receipt},
                                                   {"error", QString()}});
            toast_->notify(tr("Converted archive verified. Original and working copy are kept."));
        },
        [this, editId](QString error) {
            if (!editId.isEmpty())
                edits_->update(editId, QJsonObject{{"state", "needs-review"}, {"error", error}});
            QMessageBox::warning(this, tr("Conversion stopped"), error);
        });
}
