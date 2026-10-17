#include "MainWindow.h"
#include "SecretUtf8.h"
#include "dialogs/CreateArchiveDialog.h"
#include "widgets/ToastOverlay.h"
#include <QDesktopServices>
#include <QJsonDocument>
#include <QtWidgets>
static QString jobStateLabel(const QString &state) {
    if (state == "completed")
        return MainWindow::tr("Completed");
    if (state == "failed")
        return MainWindow::tr("Failed");
    if (state == "cancelled")
        return MainWindow::tr("Cancelled");
    if (state == "interrupted")
        return MainWindow::tr("Interrupted - review required");
    if (state == "decision")
        return MainWindow::tr("Review required");
    if (state == "running")
        return MainWindow::tr("Running");
    return MainWindow::tr("Pending");
}
void MainWindow::setupJournal() {
    if (QCoreApplication::arguments().contains("--smoke-test") ||
        QCoreApplication::arguments().contains("--capture"))
        return;
    const auto path = QFileInfo(settings_->fileName()).absolutePath() + "/state/jobs.json";
    if (!journal_.open(path, settings_->value("historyEnabled", true).toBool())) {
        toast_->notify(tr("Queue history is unavailable. Jobs will run in memory only."));
        return;
    }
    for (const auto &value : journal_.records()) {
        const auto record = value.toObject();
        auto *item =
            new QListWidgetItem(tr("%1 - %2").arg(jobStateLabel(record["state"].toString()),
                                                  record["title"].toString()),
                                queueView_);
        item->setData(Qt::UserRole + 1, record["title"].toString());
        item->setData(Qt::UserRole + 2, true);
        item->setData(Qt::UserRole + 3, record["id"].toString());
        item->setData(Qt::UserRole + 4, record["spec"].toObject());
        item->setData(Qt::UserRole + 5, record["state"].toString());
        item->setData(Qt::UserRole + 6, record["error"].toString());
        item->setToolTip(record["error"].toString());
        if (record["state"] == "interrupted")
            queueDock_->show();
    }
    updateQueue();
}
void MainWindow::syncJournal() {
    int completed = 0, failed = 0, pending = 0;
    QMap<QString, QVector<int>> batches;
    for (int i = 0; i < queueView_->count(); ++i) {
        auto *item = queueView_->item(i);
        auto state = item->data(Qt::UserRole + 5).toString();
        if (state.isEmpty())
            continue;
        auto id = item->data(Qt::UserRole + 3).toString();
        if (id.isEmpty()) {
            id = journal_.add(item->data(Qt::UserRole + 1).toString(),
                              item->data(Qt::UserRole + 4).toJsonObject());
            item->setData(Qt::UserRole + 3, id);
        }
        journal_.setState(id, state, item->data(Qt::UserRole + 6).toString());
        completed += state == "completed";
        failed += state == "failed";
        pending += state == "pending" || state == "interrupted" || state == "decision";
        const auto batch = item->data(Qt::UserRole + 4).toJsonObject()["batchId"].toString();
        if (!batch.isEmpty()) {
            if (!batches.contains(batch))
                batches[batch] = QVector<int>(4);
            auto &counts = batches[batch];
            ++counts[0];
            counts[1] += state == "completed";
            counts[2] += state == "failed" || state == "cancelled";
            counts[3] += state == "pending" || state == "running" || state == "interrupted" ||
                         state == "decision";
        }
    }
    if (queueSummary_)
        queueSummary_->setText(tr("Completed: %1 | Errors: %2 | Pending/review: %3")
                                   .arg(completed)
                                   .arg(failed)
                                   .arg(pending));
    QStringList batchSummaries;
    for (auto it = batches.cbegin(); it != batches.cend(); ++it) {
        const auto &c = it.value();
        batchSummaries << tr("Batch %1: %2 total, %3 completed, %4 stopped, %5 remaining")
                              .arg(it.key().left(8))
                              .arg(c[0])
                              .arg(c[1])
                              .arg(c[2])
                              .arg(c[3]);
    }
    queueSummary_->setToolTip(batchSummaries.join('\n'));
    if (!batchSummaries.isEmpty())
        queueSummary_->setText(queueSummary_->text() + "\n" + batchSummaries.last());
    if (!journal_.error().isEmpty() && journalWarning_ != journal_.error()) {
        journalWarning_ = journal_.error();
        toast_->notify(tr("Could not save queue history. See operation details."));
    }
}
void MainWindow::exportJobReport() {
    const auto path = QFileDialog::getSaveFileName(
        this, tr("Export operation report"), "VYNX-operations.json", tr("JSON files (*.json)"));
    if (path.isEmpty())
        return;
    QSaveFile file(path);
    const auto bytes =
        QJsonDocument(QJsonObject{{"schema", 1}, {"jobs", journal_.records()}}).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        QMessageBox::warning(this, tr("Export operation report"), file.errorString());
}
void MainWindow::retrySelectedJob() {
    auto *item = queueView_->currentItem();
    if (!item || item == runningItem_ || !item->data(Qt::UserRole + 2).toBool())
        return;
    const auto record = journal_.record(item->data(Qt::UserRole + 3).toString());
    auto spec = record["spec"].toObject();
    const auto kind = spec["kind"].toString();
    if (spec["incompleteSpec"].toBool()) {
        QMessageBox::information(this, tr("Retry operation"),
                                 tr("This selection is too large to restore completely. Repeat it "
                                    "from the archive tab."));
        return;
    }
    if (!QStringList{"create", "extract", "test", "shell", "open", "modify"}.contains(kind)) {
        QMessageBox::information(this, tr("Retry operation"),
                                 tr("Repeat this action from its archive tab. Edited files are "
                                    "managed in Working copies."));
        return;
    }
    if (kind == "create") {
        QStringList initial;
        for (const auto &path : spec["sources"].toArray())
            initial << path.toString();
        CreateArchiveDialog create(initial, qBound(0, spec["format"].toInt(), 11), this);
        create.setWindowTitle(tr("Review and retry"));
        if (!JobJournal::sourcesUnchanged(record)) {
            auto *notice =
                new QLabel(tr("A source changed or is missing. Review the current files carefully; "
                              "the new attempt will use their current content."));
            notice->setWordWrap(true);
            static_cast<QFormLayout *>(create.layout())->insertRow(0, notice);
        }
        create.output->setText(spec["destination"].toString());
        create.preset->setCurrentIndex(qBound(0, spec["preset"].toInt(2), 3));
        create.resource->setCurrentIndex(qBound(0, spec["resource"].toInt(1), 2));
        if (create.exec() != QDialog::Accepted)
            return;
        QJsonArray inputs;
        for (int i = 0; i < create.inputs->count(); ++i)
            inputs.append(create.inputs->item(i)->text());
        spec["sources"] = inputs;
        spec["destination"] = create.output->text();
        spec["format"] = create.format->currentIndex();
        spec["preset"] = create.preset->currentIndex();
        spec["resource"] = create.resource->currentIndex();
        const quint64 sizes[]{0,          100ULL << 20, 500ULL << 20,
                              1ULL << 30, 4ULL << 30,   quint64(create.customSplit->value()) << 20};
        spec["volumeSize"] =
            double(create.format->currentIndex() == 1 && create.advanced->isChecked()
                       ? sizes[create.split->currentIndex()]
                       : 0);
        const auto pw = secret(create.password->text());
        create.password->clear();
        spec["needsPassword"] = !pw->bytes().empty();
        spec["retryOf"] = record["id"];
        if (record["state"] == "interrupted") {
            item->setData(Qt::UserRole + 5, "failed");
            item->setText(tr("Stopped — %1").arg(item->data(Qt::UserRole + 1).toString()));
        }
        enqueueRequest(
            item->data(Qt::UserRole + 1).toString(), [this, spec, pw] { dispatchSpec(spec, pw); },
            {}, spec);
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Review and retry"));
    auto *form = new QFormLayout(&dialog);
    auto *notice =
        new QLabel(JobJournal::sourcesUnchanged(record)
                       ? tr("Review parameters before starting a new attempt.")
                       : tr("A source changed or is missing. Review the current files carefully; "
                            "the new attempt will use their current content."));
    notice->setWordWrap(true);
    form->addRow(notice);
    auto *sources = new QPlainTextEdit;
    QStringList paths;
    for (auto v : spec["sources"].toArray())
        paths << v.toString();
    sources->setPlainText(paths.join('\n'));
    sources->setMaximumHeight(110);
    form->addRow(tr("Sources (one path per line)"), sources);
    auto *destination = new QLineEdit(spec["destination"].toString(), &dialog);
    if (kind == "create" || kind == "extract")
        form->addRow(tr("Destination"), destination);
    else
        destination->hide();
    auto *conflict = new QComboBox(&dialog);
    conflict->addItems({tr("Stop on conflict"), tr("Skip existing"), tr("Replace existing"),
                        tr("Ask"), tr("Keep both"), tr("Replace if newer")});
    conflict->setCurrentIndex(qBound(0, spec["conflict"].toInt(), 5));
    if (kind == "extract")
        form->addRow(tr("Existing files"), conflict);
    else
        conflict->hide();
    auto *preset = new QComboBox(&dialog);
    preset->addItems({tr("Store / None"), tr("Fast"), tr("Balanced"), tr("Maximum")});
    preset->setCurrentIndex(qBound(0, spec["preset"].toInt(2), 3));
    auto *resource = new QComboBox(&dialog);
    resource->addItems({tr("Eco"), tr("Balanced"), tr("Maximum")});
    resource->setCurrentIndex(qBound(0, spec["resource"].toInt(1), 2));
    if (kind == "create" || kind == "modify") {
        form->addRow(tr("Compression"), preset);
        form->addRow(tr("Resources"), resource);
    } else {
        preset->hide();
        resource->hide();
    }
    auto *password = new QLineEdit(&dialog);
    password->setEchoMode(QLineEdit::Password);
    form->addRow(tr("Password, if needed"), password);
    auto *names = new QPlainTextEdit(&dialog);
    QStringList entryNames;
    for (auto name : spec["names"].toArray())
        entryNames << name.toString();
    names->setPlainText(entryNames.join('\n'));
    names->setMaximumHeight(110);
    auto *newName = new QLineEdit(spec["newName"].toString(), &dialog);
    auto *folder = new QLineEdit(spec["folder"].toString(), &dialog);
    if (kind != "modify") {
        names->hide();
        newName->hide();
        folder->hide();
    }
    if (kind == "modify") {
        const auto action = spec["action"].toInt();
        form->addRow(tr("Requested change"), new QLabel(action == 0   ? tr("Add files and folders")
                                                        : action == 1 ? tr("Delete entries")
                                                                      : tr("Rename entry")));
        names->setReadOnly(action != 0);
        form->addRow(action == 0 ? tr("Input paths") : tr("Archive entries"), names);
        if (action == 2)
            form->addRow(tr("New archive path"), newName);
        if (action != 2)
            newName->hide();
        if (action == 0)
            form->addRow(tr("Destination in archive"), folder);
        else
            folder->hide();
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Start new attempt"));
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.resize(650, 450);
    if (dialog.exec() != QDialog::Accepted)
        return;
    QJsonArray list;
    for (const auto &path : sources->toPlainText().split('\n', Qt::SkipEmptyParts))
        list.append(path.trimmed());
    if (list.isEmpty() || list.size() > 10000)
        return;
    if (kind == "modify") {
        QJsonArray entries;
        for (const auto &name : names->toPlainText().split('\n', Qt::SkipEmptyParts))
            entries.append(name);
        if (entries.isEmpty())
            return;
        spec["names"] = entries;
        spec["newName"] = newName->text();
        spec["folder"] = folder->text();
    }
    spec["retryOf"] = record["id"];
    spec["sources"] = list;
    spec["destination"] = destination->text();
    spec["conflict"] = conflict->currentIndex();
    spec["preset"] = preset->currentIndex();
    spec["resource"] = resource->currentIndex();
    const auto pw = secret(password->text());
    password->clear();
    spec["needsPassword"] = !pw->bytes().empty();
    if (record["state"] == "interrupted") {
        item->setData(Qt::UserRole + 5, "failed");
        item->setText(item->data(Qt::UserRole + 1).toString() + " - " + tr("Stopped"));
        journal_.setState(record["id"].toString(), "failed", record["error"].toString());
    }
    enqueueRequest(
        item->data(Qt::UserRole + 1).toString(), [this, spec, pw] { dispatchSpec(spec, pw); },
        paths.join('\n'), spec);
}
void MainWindow::dispatchSpec(QJsonObject spec, const std::shared_ptr<SecretUtf8> &pw) {
    const auto kind = spec["kind"].toString();
    QStringList paths;
    for (auto value : spec["sources"].toArray())
        paths << value.toString();
    if (paths.isEmpty())
        return;
    if (kind == "open") {
        openWithSecret(paths.first(), pw, true);
        return;
    }
    if (kind == "shell") {
        handleShellWithSecret(quint32(spec["action"].toInt()), paths, pw);
        return;
    }
    for (int i = 0; i < sessions_.size(); ++i)
        if (archiveIdentity(sessions_[i]->path) == archiveIdentity(paths.first())) {
            activateSession(i);
            break;
        }
    beginOperation();
    activeSpec_ = spec;
    auto op = operations_.operation;
    vynx::configure_operation(**op, uint8_t(spec["preset"].toInt(2)),
                              uint8_t(spec["resource"].toInt(1)));
    runJob(
        tr("Retry operation"),
        [spec, kind, paths, pw, op] {
            rust::Vec<rust::String> values;
            for (const auto &path : paths)
                values.push_back(path.toUtf8().toStdString());
            const auto output = spec["destination"].toString().toUtf8().toStdString();
            if (kind == "create") {
                const auto volume = quint64(spec["volumeSize"].toDouble());
                if (volume)
                    vynx::create_split_archive(output, {values.data(), values.size()}, volume,
                                               pw->bytes(), **op);
                else
                    vynx::create_archive_as(output, {values.data(), values.size()},
                                            uint8_t(spec["format"].toInt()), pw->bytes(), **op);
            } else {
                auto archive =
                    vynx::open_archive(paths.first().toUtf8().toStdString(), pw->bytes(), **op);
                if (kind == "test")
                    vynx::test_archive(*archive, pw->bytes(), **op);
                else if (kind == "extract") {
                    rust::Vec<quint64> ids;
                    const auto entries = vynx::list_entries(*archive);
                    for (const auto &value : spec["names"].toArray()) {
                        bool found = false;
                        for (const auto &entry : entries)
                            if (QString::fromUtf8(entry.name.data(),
                                                  qsizetype(entry.name.size())) ==
                                value.toString()) {
                                ids.push_back(entry.id);
                                found = true;
                            }
                        if (!found)
                            return QStringLiteral("CHANGED: Selected entry no longer exists");
                    }
                    vynx::extract_archive(*archive, output, {ids.data(), ids.size()},
                                          uint8_t(spec["conflict"].toInt()), spec["smart"].toBool(),
                                          pw->bytes(), **op);
                } else if (kind == "modify") {
                    rust::Vec<rust::String> names;
                    for (auto name : spec["names"].toArray())
                        names.push_back(name.toString().toUtf8().toStdString());
                    vynx::modify_archive(
                        *archive, uint8_t(spec["action"].toInt()), {names.data(), names.size()},
                        spec["newName"].toString().toUtf8().toStdString(),
                        spec["folder"].toString().toUtf8().toStdString(), pw->bytes(), **op);
                } else
                    return QStringLiteral("OPTIONS: Unsupported restored operation");
            }
            return QString();
        },
        [this, kind, paths, pw] {
            if (kind == "modify")
                QTimer::singleShot(0, this,
                                   [this, path = paths.first(), pw] { reloadArchive(path, pw); });
        },
        [this](const QString &) {
            toast_->notify(tr("Operation stopped. Review details in the queue."));
        });
}
