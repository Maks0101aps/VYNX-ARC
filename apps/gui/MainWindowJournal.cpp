#include "dialogs/ExtractionRecoveryDialog.h"
#include "EditStore.h"
#include "MainWindow.h"
#include "SecretUtf8.h"
#include "dialogs/BatchExtractDialog.h"
#include "dialogs/CreateArchiveDialog.h"
#include "widgets/ToastOverlay.h"
#include <QDesktopServices>
#include <QJsonDocument>
#include <QtWidgets>
static QString receiptKey(const QString &path) {
    auto key = QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return key;
}
static QString jobStateLabel(const QString &state) {
    if (state == "unknown")
        return MainWindow::tr("Result unknown - verification required");
    if (state == "verifying")
        return MainWindow::tr("Verifying result");
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
        if (record["state"] == "interrupted" || record["state"] == "unknown")
            queueDock_->show();
    }
    updateQueue();
}
void MainWindow::syncJournal() {
    int completed = 0, failed = 0, pending = 0, running = 0, cancelled = 0, review = 0;
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
        pending += state == "pending";
        running += state == "running";
        cancelled += state == "cancelled";
        review += state == "interrupted" || state == "decision" || state == "unknown" ||
                  state == "verifying";
        const auto batch = item->data(Qt::UserRole + 4).toJsonObject()["batchId"].toString();
        if (!batch.isEmpty()) {
            if (!batches.contains(batch))
                batches[batch] = QVector<int>(4);
            auto &counts = batches[batch];
            ++counts[0];
            counts[1] += state == "completed";
            counts[2] += state == "failed" || state == "cancelled";
            counts[3] += state == "pending" || state == "running" || state == "interrupted" ||
                         state == "decision" || state == "unknown" || state == "verifying";
        }
    }
    if (queueSummary_)
        queueSummary_->setText(tr("Completed: %1 | Errors: %2 | Pending: %3 | Running: %4 | "
                                  "Cancelled: %5 | Review: %6")
                                   .arg(completed)
                                   .arg(failed)
                                   .arg(pending)
                                   .arg(running)
                                   .arg(cancelled)
                                   .arg(review));
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

    // Prioritize structured extraction recovery: allows inspecting & resuming even after crash ('unknown' state)
    if (kind == "extract" && spec["recoveryPlan"].isObject()) {
        const auto archivePath = spec["sources"].toArray().first().toString();
        const auto destRoot = spec["recoveryPlan"].toObject()["destination"].toString(spec["destination"].toString());
        const auto savedFp = spec["archiveHash"].toString();
        ExtractionRecoveryDialog recovery(archivePath, destRoot, savedFp, spec["recoveryPlan"].toObject(), this);
        if (recovery.exec() == QDialog::Accepted && recovery.canResume()) {
            const auto toExtract = recovery.filesToExtract();
            QJsonArray remainingNames;
            for (const auto &f : toExtract)
                remainingNames.append(f.name);
            spec["names"] = remainingNames;
            spec["conflict"] = 0; // Refuse newly appearing files, including races after review
            spec["smart"] = false;
            spec["destination"] = spec["recoveryPlan"].toObject()["destination"].toString(destRoot);
            if (remainingNames.isEmpty()) return;
            spec["retryOf"] = record["id"];
            if (record["state"] == "interrupted" || record["state"] == "unknown") {
                item->setData(Qt::UserRole + 5, "failed");
                item->setText(tr("Stopped — %1").arg(item->data(Qt::UserRole + 1).toString()));
            }
            auto pw = secret({});
            if (spec["needsPassword"].toBool()) {
                bool ok = false;
                const auto entered = askPassword(&ok);
                if (!ok)
                    return;
                pw = secret(entered);
            }
            enqueueRequest(
                tr("Resumed: %1").arg(item->data(Qt::UserRole + 1).toString()),
                [this, spec, pw] { dispatchSpec(spec, pw); },
                {}, spec);
            return;
        }
        return;
    }

    if (record["state"] == "unknown" || record["state"] == "verifying") {
        QMessageBox::information(
            this, tr("Verify result before retry"),
            tr("This write may have completed. Verify its destination before repeating it. The "
               "original request is retained and will not run automatically."));
        return;
    }
    if (spec["incompleteSpec"].toBool()) {
        QMessageBox::information(this, tr("Retry operation"),
                                 tr("This selection is too large to restore completely. Repeat it "
                                    "from the archive tab."));
        return;
    }
    if (kind == "convert") {
        QStringList paths;
        for (const auto &path : spec["sources"].toArray())
            paths.append(path.toString());
        chooseConversion(paths, spec["editId"].toString(), record);
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
        if (record["state"] == "interrupted" || record["state"] == "unknown") {
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
    if (record["state"] == "interrupted" || record["state"] == "unknown") {
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
                    if (spec.contains("retryOf") && spec["recoveryPlan"].isObject()) {
                        const auto expected = spec["recoveryPlan"].toObject()["sourceDigest"].toString();
                        if (expected.isEmpty() ||
                            QString::fromStdString(std::string(vynx::hash_archive_file(
                                paths.first().toUtf8().toStdString(), **op))) != expected)
                            return QStringLiteral("CHANGED: Extraction source identity could not be verified");
                    }
                    rust::Vec<quint64> ids;
                    const auto entries = vynx::list_entries(*archive);
                    QHash<QString, quint64> index;
                    index.reserve(qsizetype(entries.size()));
                    for (const auto &entry : entries)
                        index.insert(
                            QString::fromUtf8(entry.name.data(), qsizetype(entry.name.size())),
                            entry.id);
                    for (const auto &value : spec["names"].toArray()) {
                        const auto found = index.constFind(value.toString());
                        if (found == index.cend())
                            return QStringLiteral("CHANGED: Selected entry no longer exists");
                        ids.push_back(found.value());
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
                } else if (kind == "copy") {
                    return QStringLiteral("OPTIONS: Repeat inter-archive copy from its source tab");
                } else
                    return QStringLiteral("OPTIONS: Unsupported restored operation");
            }
            return QString();
        },
        [this, kind, paths, pw, spec] {
            if (kind == "modify" || kind == "copy") {
                const auto target = kind == "copy" ? spec["targetArchive"].toString() : paths.first();
                QTimer::singleShot(0, this,
                                   [this, target, pw] { reloadArchive(target, pw); });
            }
        },
        [this](const QString &) {
            toast_->notify(tr("Operation stopped. Review details in the queue."));
        });
}

void MainWindow::verifySelectedResult() {
    if (operations_.busy)
        return;
    auto *item = queueView_->currentItem();
    if (!item)
        return;
    const auto id = item->data(Qt::UserRole + 3).toString();
    const auto record = journal_.record(id);
    if (record["state"] != "unknown" && record["state"] != "verifying")
        return;
    const auto targets = record["publication"].toArray();
    if (targets.isEmpty()) {
        QMessageBox::information(
            this, tr("Verify result"),
            tr("No publication identity was recorded. Inspect the destination and source manually. "
               "This request will not be retried automatically."));
        return;
    }
    journal_.setState(id, "verifying");
    item->setData(Qt::UserRole + 5, "verifying");
    beginOperation();
    activeSpec_ = QJsonObject{{"kind", "reconcile"}};
    auto op = operations_.operation;
    auto result = std::make_shared<QString>("unknown");
    const auto spec = record["spec"].toObject();
    const auto editId = spec["editId"].toString();
    const auto edit = edits_->record(editId);
    const auto workingFile = edits_->validPayload(edit) ? edit["file"].toString() : QString();
    auto hashes = std::make_shared<QHash<QString, QString>>();
    auto workingHash = std::make_shared<QString>();
    auto workingFingerprint = std::make_shared<QString>();
    auto apply = [this, id, result, spec, editId, workingFile, hashes, workingHash,
                  workingFingerprint](const QString &error) {
        const auto state = error.isEmpty() ? *result : QString("unknown");
        if (state == "completed" && spec["kind"] == "edit" && !workingFile.isEmpty()) {
            const auto current = edits_->record(editId);
            const auto source = spec["sources"].toArray().first().toString();
            const auto savedInput = spec["inputHash"].toString();
            if (edits_->validPayload(current) && current["file"] == workingFile &&
                archiveIdentity(current["archive"].toString()) == archiveIdentity(source) &&
                !savedInput.isEmpty() && hashes->contains(receiptKey(source))) {
                const bool unchanged = *workingHash == savedInput &&
                                       !workingFingerprint->isEmpty() &&
                                       *workingFingerprint == EditStore::fingerprint(workingFile);
                QJsonObject update{
                    {"archiveHash", hashes->value(receiptKey(source))},
                    {"baseHash", savedInput},
                    {"state", unchanged ? "saved" : "changed"},
                    {"error", QString()},
                    {"lastVerified", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
                if (unchanged)
                    update["fingerprint"] = *workingFingerprint;
                if (!edits_->update(editId, update))
                    QMessageBox::warning(this, tr("Working copies"),
                                         tr("The archive was verified, but recovery metadata could "
                                            "not be updated. Keep the working copy."));
            }
        }
        const auto detail =
            state == "completed" ? tr("Verified destination matches the recorded result.")
            : state == "failed"  ? tr("Verified destination matches the pre-publication state. "
                                       "Review parameters before explicitly retrying.")
                                : tr("Destination differs or cannot be checked. Preserve files and "
                                     "inspect it before any retry.");
        journal_.setState(id, state, error.isEmpty() ? detail : error);
        for (int i = 0; i < queueView_->count(); ++i) {
            auto *row = queueView_->item(i);
            if (row->data(Qt::UserRole + 3).toString() != id)
                continue;
            row->setData(Qt::UserRole + 5, state);
            row->setData(Qt::UserRole + 6, error.isEmpty() ? detail : error);
            row->setText(jobStateLabel(state) + " - " + row->data(Qt::UserRole + 1).toString());
            row->setToolTip(detail + "\n" + error);
        }
        updateQueue();
        QMessageBox::information(this, tr("Verify result"),
                                 detail + (error.isEmpty() ? QString() : "\n" + error));
    };
    runJob(
        tr("Verifying interrupted publication..."),
        [targets, op, result, hashes, workingFile, workingHash, workingFingerprint] {
            bool after = true, before = true;
            for (const auto &v : targets) {
                if (vynx::cancelled(**op))
                    return QStringLiteral("CANCELLED: Verification cancelled");
                const auto t = v.toObject();
                const auto path = t["path"].toString();
                QString current;
                if (QFileInfo::exists(path))
                    current = QString::fromStdString(
                        std::string(vynx::hash_archive_file(path.toUtf8().toStdString(), **op)));
                if (!current.isEmpty())
                    hashes->insert(receiptKey(path), current);
                if (!current.isEmpty())
                    current = current.section('\n', 0, 0).mid(9);
                after &= current == t["sha256"].toString();
                before &= current == t["before"].toString();
            }
            *result = after ? "completed" : before ? "failed" : "unknown";
            if (after && !workingFile.isEmpty()) {
                const auto fingerprint = EditStore::fingerprint(workingFile);
                try {
                    *workingHash = QString::fromStdString(std::string(
                        vynx::hash_archive_file(workingFile.toUtf8().toStdString(), **op)));
                    if (fingerprint == EditStore::fingerprint(workingFile))
                        *workingFingerprint = fingerprint;
                } catch (...) {
                    // Archive verification remains valid; retain an unverified dirty copy.
                }
            }
            return QString();
        },
        [apply] { apply({}); }, [apply](QString error) { apply(error); });
}

void MainWindow::retryFailedBatch() {
    const auto *selected = queueView_->currentItem();
    if (!selected)
        return;
    const auto chosen = journal_.record(selected->data(Qt::UserRole + 3).toString());
    const auto batch = chosen["spec"].toObject()["batchId"].toString();
    if (batch.isEmpty())
        return;
    QJsonArray failed;
    QStringList paths;
    QJsonObject origins;
    for (const auto &value : journal_.records()) {
        const auto record = value.toObject();
        const auto spec = record["spec"].toObject();
        if (record["state"] != "failed" || spec["batchId"] != batch ||
            spec["incompleteSpec"].toBool())
            continue;
        if (spec["kind"] != "extract" && spec["kind"] != "convert")
            continue;
        const auto sources = spec["sources"].toArray();
        if (sources.size() != 1)
            continue;
        failed.append(record);
        const auto path = sources.first().toString();
        paths.append(path);
        origins.insert(QFileInfo(path).absoluteFilePath(), record["id"]);
    }
    if (failed.isEmpty()) {
        QMessageBox::information(this, tr("Retry batch"),
                                 tr("This batch has no failed jobs available for retry."));
        return;
    }
    if (queue_.size() + size_t(failed.size()) > 32) {
        QMessageBox::information(this, tr("Retry batch"),
                                 tr("The queue supports up to 32 pending jobs."));
        return;
    }
    const auto first = failed.first().toObject();
    const auto firstSpec = first["spec"].toObject();
    if (firstSpec["kind"] == "convert") {
        auto retry = first;
        retry["origins"] = origins;
        chooseConversion(paths, {}, retry);
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Review failed batch jobs"));
    auto *form = new QFormLayout(&dialog);
    auto *notice = new QLabel(tr("%1 failed jobs will receive new attempts. Review their "
                                 "destination and overwrite policy.")
                                  .arg(failed.size()));
    notice->setWordWrap(true);
    form->addRow(notice);
    auto *destination =
        new QLineEdit(QFileInfo(firstSpec["destination"].toString()).absolutePath());
    form->addRow(tr("Output folder"), destination);
    auto *conflict = new QComboBox;
    conflict->addItems({tr("Refuse"), tr("Skip existing"), tr("Replace existing"), tr("Ask"),
                        tr("Keep both"), tr("Newer only")});
    conflict->setCurrentIndex(qBound(0, firstSpec["conflict"].toInt(4), 5));
    form->addRow(tr("Conflicts"), conflict);
    auto *password = new QLineEdit;
    password->setEchoMode(QLineEdit::Password);
    form->addRow(tr("Source password"), password);
    auto *list = new QPlainTextEdit(paths.join('\n'));
    list->setReadOnly(true);
    form->addRow(tr("Sources"), list);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Queue new attempts"));
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.resize(640, 430);
    if (dialog.exec() != QDialog::Accepted || destination->text().isEmpty())
        return;
    const auto pw = secret(password->text());
    password->clear();
    const auto newBatch = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto outputs = batchDestinations(paths, destination->text());
    for (int i = 0; i < failed.size(); ++i) {
        const auto record = failed[i].toObject();
        auto spec = record["spec"].toObject();
        spec["retryOf"] = record["id"];
        spec["batchId"] = newBatch;
        spec["destination"] = outputs[i];
        spec["conflict"] = conflict->currentIndex();
        spec["needsPassword"] = !pw->bytes().empty();
        enqueueRequest(
            tr("Retry %1").arg(record["title"].toString()),
            [this, spec, pw] { dispatchSpec(spec, pw); }, paths[i] + "\n" + outputs[i], spec);
    }
}
