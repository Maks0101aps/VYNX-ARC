#include "EditStore.h"
#include "MainWindow.h"
#include "SecretUtf8.h"
#include "dialogs/ConversionDialog.h"
#include "dialogs/ExtractionRecoveryDialog.h"
#include "dialogs/InterArchiveCopyDialog.h"
#include "dialogs/PreviewDialog.h"
#include "pages/ArchivePage.h"
#include <QtConcurrent>
#include <QtWidgets>
#include <algorithm>
#include <atomic>
static QString string(const rust::String &s) {
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}
bool MainWindow::performanceAcceptance(const QString &source, const QString &root) {
    if (QFileInfo::exists(root) || !QFileInfo(source).isFile() || !QDir().mkpath(root))
        return false;
    settings_ = std::make_unique<QSettings>(root + "/settings.ini", QSettings::IniFormat);
    if (!journal_.open(root + "/jobs.json", true))
        return false;
    show();
    QString error;
    QTimer closer;
    connect(&closer, &QTimer::timeout, this, [&] {
        for (auto *box : findChildren<QMessageBox *>())
            if (box->isVisible()) {
                error = box->text();
                box->accept();
            }
        for (auto *dialog : findChildren<QDialog *>()) {
            if (qobject_cast<PreviewDialog *>(dialog)) {
                if (dialog->isVisible())
                    dialog->reject();
            } else if (qobject_cast<InterArchiveCopyDialog *>(dialog)) {
                if (dialog->isVisible())
                    dialog->accept();
            }
        }
    });
    closer.start(5);
    auto idle = [&] {
        QElapsedTimer limit;
        limit.start();
        while ((operations_.busy || queue_.size() || !readers_.isEmpty()) &&
               limit.elapsed() < 60000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QThread::msleep(1);
        }
        QCoreApplication::processEvents();
        return error.isEmpty() && !operations_.busy && !queue_.size() && readers_.isEmpty();
    };
    auto settle = [&] {
        int stable = 0;
        QElapsedTimer limit;
        limit.start();
        while (limit.elapsed() < 60000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QThread::msleep(2);
            bool busy = operations_.busy || queue_.size() || !readers_.isEmpty();
            for (const auto &candidate : sessions_)
                busy = busy || candidate->busy;
            if (!busy) {
                if (++stable == 5)
                    return error.isEmpty();
            } else {
                stable = 0;
            }
        }
        return false;
    };
    QElapsedTimer time;
    QJsonArray opens, extracts, searches, cycles;
    QJsonArray nestedOpens, copies, recoveries, firstPreviews, cachedPreviews;
    int nestedEntries = 0, copyItems = 0, recoveryEntries = 0, recoveryExisting = 0;
    double cancellationSeconds = 0;
    int heartbeats = 0;
    QTimer heartbeat;
    connect(&heartbeat, &QTimer::timeout, this, [&] { ++heartbeats; });
    heartbeat.start(20);
    auto extract = [&](const QString &destination, const QString &name) {
        const auto archive = session_->archive;
        uint64_t selected = 0;
        bool found = false;
        for (const auto &entry : vynx::list_entries(**archive))
            if (string(entry.name) == name) {
                selected = entry.id;
                found = true;
                break;
            }
        if (!found)
            return false;
        activeSpec_ = QJsonObject{{"kind", "extract"},
                                  {"sources", QJsonArray{session_->path}},
                                  {"destination", destination},
                                  {"names", QJsonArray{name}}};
        beginOperation();
        const auto op = operations_.operation;
        runJob(
            "Performance extraction",
            [archive, selected, destination, op] {
                vynx::extract_archive(**archive, destination.toUtf8().toStdString(), {&selected, 1},
                                      0, false, "", **op);
                return QString();
            },
            {}, [&](QString e) { error = e; });
        return idle() && QFileInfo::exists(destination + "/" + name);
    };
    auto write = [](const QString &path, const QByteArray &bytes) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    };
    auto create = [&](const QString &path, const QStringList &inputs) {
        QFile::remove(path);
        rust::Vec<rust::String> values;
        for (const auto &input : inputs)
            values.push_back(input.toUtf8().toStdString());
        auto op = vynx::new_operation();
        vynx::create_archive_as(path.toUtf8().toStdString(), {values.data(), values.size()}, 0, "",
                                *op);
    };
    auto open = [&](const QString &path) {
        openWithSecret(path, secret({}));
        return idle() && session_->archive && session_->path == path;
    };
    auto select = [&](const QString &name) {
        table_->clearSelection();
        for (int i = 0; i < session_->filter->rowCount(); ++i) {
            const auto index = session_->filter->index(i, 0);
            if (session_->model->row(session_->filter->mapToSource(index).row()).name == name) {
                table_->selectionModel()->select(index, QItemSelectionModel::Select |
                                                            QItemSelectionModel::Rows);
                return index;
            }
        }
        return QModelIndex();
    };
    auto closePath = [&](const QString &path) {
        for (int i = 0; i < sessions_.size(); ++i)
            if (sessions_[i]->path == path) {
                closeSession(i);
                return idle();
            }
        return true;
    };
    try {
        for (int trial = 0; trial < 3; ++trial) {
            time.start();
            openWithSecret(source, secret({}));
            if (!idle() || !session_->archive || session_->model->rowCount() == 0)
                return false;
            opens.append(time.nsecsElapsed() / 1e9);
            for (int query = 0; query < 20; ++query) {
                time.restart();
                search_->setText(QString("file-%1").arg(query * 431, 6, 10, QChar('0')));
                if (session_->filter->rowCount() != 1)
                    return false;
                QCoreApplication::processEvents();
                searches.append(time.nsecsElapsed() / 1e6);
            }
            search_->clear();
            time.restart();
            if (!extract(root + QString("/many-%1").arg(trial), "project/g000/file-000000.txt"))
                return false;
            extracts.append(time.nsecsElapsed() / 1e9);
            if (trial == 2) {
                activeSpec_ = QJsonObject{{"kind", "test"}, {"sources", QJsonArray{source}}};
                beginOperation();
                auto op = operations_.operation;
                auto archive = session_->archive;
                QString cancelled;
                time.restart();
                runJob(
                    "Performance cancellation",
                    [archive, op] {
                        vynx::test_archive(**archive, "", **op);
                        return QString();
                    },
                    {}, [&](QString value) { cancelled = value; });
                QTimer::singleShot(50, this, [op] { vynx::cancel(**op); });
                if (!idle() || !cancelled.startsWith("CANCELLED:"))
                    return false;
                cancellationSeconds = time.nsecsElapsed() / 1e9;
            }
            closeSession(sessions_.indexOf(session_));
            if (!idle())
                return false;
        }
        QFile note(root + "/needle.txt");
        if (!note.open(QIODevice::WriteOnly | QIODevice::NewOnly) ||
            note.write("cycle payload") != 13)
            return false;
        note.close();
        const auto small = root + "/cycles.zip";
        auto op = vynx::new_operation();
        rust::Vec<rust::String> inputs;
        inputs.push_back((root + "/needle.txt").toUtf8().toStdString());
        vynx::create_archive(small.toUtf8().toStdString(), {inputs.data(), inputs.size()}, "", *op);
        for (int cycle = 0; cycle < 100; ++cycle) {
            time.restart();
            openWithSecret(small, secret({}));
            if (!idle() || !session_->archive)
                return false;
            // The queue, model, preview decoder, selected extraction and tab teardown
            // are exercised together; each operation uses the shipping worker.
            const auto archive = session_->archive;
            auto preview = QtConcurrent::run([archive] {
                auto op = vynx::new_operation();
                return string(vynx::preview_text(**archive, 0, "", *op));
            });
            while (!preview.isFinished()) {
                QCoreApplication::processEvents();
                QThread::msleep(1);
            }
            if (preview.result() != "cycle payload" ||
                !extract(root + QString("/cycle-%1").arg(cycle), "needle.txt"))
                return false;
            closeSession(sessions_.indexOf(session_));
            if (!idle())
                return false;
            cycles.append(time.nsecsElapsed() / 1e9);
            if (cycle == 9 || cycle == 49 || cycle == 99)
                qInfo("GUI_RESOURCE_CHECKPOINT %d", cycle + 1);
        }

        const auto nestedInner = root + "/nested-inner";
        for (int i = 0; i < 500; ++i)
            if (!write(nestedInner + QString("/nested-%1.txt").arg(i, 3, 10, QChar('0')),
                       QByteArray("nested-") + QByteArray::number(i) + QByteArray(1024, 'n')))
                return false;
        const auto innerArchive = root + "/inner.zip";
        const auto outerArchive = root + "/outer.zip";
        create(innerArchive, {nestedInner});
        create(outerArchive, {innerArchive});
        if (!open(outerArchive))
            return false;
        for (int trial = 0; trial < 3; ++trial) {
            const auto innerIndex = select("inner.zip");
            if (!innerIndex.isValid())
                return false;
            const auto &innerRow =
                session_->model->row(session_->filter->mapToSource(innerIndex).row());
            time.restart();
            openNestedArchive(innerRow);
            QElapsedTimer nestedLimit;
            nestedLimit.start();
            bool nestedReady = false;
            while (nestedLimit.elapsed() < 60000) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                if (!operations_.busy && session_->nestedInfo &&
                    session_->nestedInfo->depth == 1 && session_->archive &&
                    session_->model->rowCount() > 0) {
                    nestedReady = true;
                    break;
                }
                QThread::msleep(1);
            }
            if (!nestedReady)
                return false;
            nestedEntries = session_->model->count();
            nestedOpens.append(time.nsecsElapsed() / 1e9);
            if (!closePath(session_->path) || !open(outerArchive))
                return false;
        }
        if (!closePath(outerArchive))
            return false;

        const auto copySourceDir = root + "/copy-source";
        QStringList copyInputs;
        for (int i = 0; i < 300; ++i) {
            const auto path =
                copySourceDir + QString("/item-%1.txt").arg(i, 3, 10, QChar('0'));
            if (!write(path, QByteArray("copy-") + QByteArray::number(i) + QByteArray(4096, 'c')))
                return false;
            copyInputs.append(path);
        }
        const auto copySource = root + "/copy-source.zip";
        const auto copySeed = root + "/copy-target-seed.txt";
        create(copySource, copyInputs);
        if (!write(copySeed, "target seed"))
            return false;
        for (int trial = 0; trial < 3; ++trial) {
            const auto target = root + QString("/copy-target-%1.zip").arg(trial);
            create(target, {copySeed});
            if (!open(copySource))
                return false;
            table_->clearSelection();
            QItemSelection selection;
            int selectedCount = 0;
            for (int i = 0; i < session_->filter->rowCount() && selectedCount < 200; ++i) {
                const auto index = session_->filter->index(i, 0);
                if (session_->model->row(session_->filter->mapToSource(index).row()).folder)
                    continue;
                selection.select(index, index);
                ++selectedCount;
            }
            if (selectedCount != 200)
                return false;
            table_->selectionModel()->select(selection, QItemSelectionModel::Select |
                                                            QItemSelectionModel::Rows);
            std::unique_ptr<QMimeData> mime(
                session_->filter->mimeData(table_->selectionModel()->selectedRows()));
            if (!open(target))
                return false;
            time.restart();
            pasteFromMimeData(mime.get());
            if (!settle())
                return false;
            copies.append(time.nsecsElapsed() / 1e9);
            copyItems = selectedCount;
            auto verifyOp = vynx::new_operation();
            auto targetArchive = vynx::open_archive(target.toUtf8().toStdString(), "", *verifyOp);
            int files = 0;
            for (const auto &entry : vynx::list_entries(*targetArchive))
                if (!entry.directory)
                    ++files;
            if (files < selectedCount + 1)
                return false;
            if (!closePath(target) || !closePath(copySource))
                return false;
        }

        const auto recoverySourceDir = root + "/recovery-source";
        QStringList recoveryInputs;
        for (int i = 0; i < 400; ++i) {
            const auto path =
                recoverySourceDir + QString("/recovery-%1.txt").arg(i, 3, 10, QChar('0'));
            if (!write(path, QByteArray("recovery-") + QByteArray::number(i) +
                                  QByteArray(512, 'r')))
                return false;
            recoveryInputs.append(path);
        }
        const auto recoveryArchive = root + "/recovery.zip";
        create(recoveryArchive, recoveryInputs);
        auto recoveryOp = vynx::new_operation();
        auto recoverySource =
            vynx::open_archive(recoveryArchive.toUtf8().toStdString(), "", *recoveryOp);
        QJsonArray recoveryFiles;
        QVector<QString> recoveryNames;
        for (const auto &entry : vynx::list_entries(*recoverySource)) {
            if (entry.directory)
                continue;
            recoveryFiles.append(QJsonObject{{"name", string(entry.name)},
                                             {"size", double(entry.size)},
                                             {"modified", double(entry.modified_unix)},
                                             {"crc", string(entry.crc)}});
            recoveryNames.append(string(entry.name));
        }
        recoveryEntries = recoveryFiles.size();
        if (recoveryEntries != 400)
            return false;
        for (int trial = 0; trial < 3; ++trial) {
            const auto destination = root + QString("/recovery-output-%1").arg(trial);
            for (int i = 0; i < 200; ++i) {
                const auto name = recoveryNames[i];
                const auto sourceFile = recoverySourceDir + "/" + name;
                const auto targetFile = destination + "/" + name;
                QDir().mkpath(QFileInfo(targetFile).absolutePath());
                if (!QFile::copy(sourceFile, targetFile))
                    return false;
            }
            QJsonObject plan{{"files", recoveryFiles}};
            time.restart();
            ExtractionRecoveryDialog dialog(recoveryArchive, destination, {}, plan, this);
            recoveries.append(time.nsecsElapsed() / 1e9);
            recoveryExisting = 200;
            if (dialog.filesToExtract().size() != 200)
                return false;
        }

        const auto previewFile = root + "/preview.txt";
        const auto previewArchive = root + "/preview.zip";
        if (!write(previewFile, "cached preview payload"))
            return false;
        create(previewArchive, {previewFile});
        for (int trial = 0; trial < 3; ++trial) {
            if (!open(previewArchive))
                return false;
            const auto previewIndex = select("preview.txt");
            if (!previewIndex.isValid())
                return false;
            const auto previewId =
                session_->model->row(session_->filter->mapToSource(previewIndex).row()).id;
            auto previewSettled = [&] {
                QElapsedTimer limit;
                limit.start();
                while (limit.elapsed() < 60000) {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                    bool visible = false;
                    for (auto *dialog : findChildren<PreviewDialog *>())
                        visible = visible || dialog->isVisible();
                    if (!operations_.busy && !visible &&
                        session_->previewCache.get(previewId).has_value()) {
                        QElapsedTimer settle;
                        settle.start();
                        while (settle.elapsed() < 15)
                            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
                        return true;
                    }
                    QThread::msleep(1);
                }
                return false;
            };
            time.restart();
            previewSelected();
            if (!previewSettled())
                return false;
            firstPreviews.append(time.nsecsElapsed() / 1e9);
            time.restart();
            previewSelected();
            if (!previewSettled())
                return false;
            cachedPreviews.append(time.nsecsElapsed() / 1e9);
            if (!closePath(previewArchive))
                return false;
        }

        QSaveFile report(root + "/results.json");
        const auto bytes =
            QJsonDocument(QJsonObject{{"status", "passed"},
                                      {"open_seconds", opens},
                                      {"selected_extract_seconds", extracts},
                                      {"search_milliseconds", searches},
                                      {"resource_cycle_seconds", cycles},
                                      {"nested_open_seconds", nestedOpens},
                                      {"inter_archive_copy_seconds", copies},
                                      {"recovery_plan_seconds", recoveries},
                                      {"preview_first_seconds", firstPreviews},
                                      {"preview_cached_seconds", cachedPreviews},
                                      {"nested_entries", nestedEntries},
                                      {"copy_items", copyItems},
                                      {"recovery_entries", recoveryEntries},
                                      {"recovery_existing_entries", recoveryExisting},
                                      {"cancellation_seconds", cancellationSeconds},
                                      {"heartbeat_ticks", heartbeats},
                                      {"cache_scope", "First and repeated GUI process operations; "
                                                      "OS filesystem cache uncontrolled"},
                                      {"scenario_scope", "Nested ZIP open through GUI; 200-file "
                                                         "inter-archive copy; recovery dialog "
                                                         "classification of 400 entries with 200 "
                                                         "existing outputs; first and cached text "
                                                         "preview through PreviewDialog"}})
                .toJson();
        return report.open(QIODevice::WriteOnly) && report.write(bytes) == bytes.size() &&
               report.commit();
    } catch (const std::exception &e) {
        qWarning("GUI performance acceptance: %s", e.what());
        return false;
    }
}
bool MainWindow::workerFaultAcceptance(const QString &mode, const QString &root) {
    const auto fail = [](int line) {
        qWarning("Worker acceptance failed at %d", line);
        return false;
    };
    const bool recovering = mode.startsWith("recover-");
    if (!QStringList{"crash", "hang", "memory", "before", "after", "recover-before",
                     "recover-after", "recover-after-dirty", "batch"}
             .contains(mode))
        return fail(__LINE__);
    if (!recovering && QFileInfo::exists(root))
        return fail(__LINE__);
    if (recovering && !QFileInfo::exists(root + "/.vynxarc-fault-qa"))
        return fail(__LINE__);
    if (!QDir().mkpath(root))
        return fail(__LINE__);
    settings_ = std::make_unique<QSettings>(root + "/settings.ini", QSettings::IniFormat);
    edits_ = std::make_unique<EditStore>(root + "/WorkingCopies");
    if (!edits_->available() || !journal_.open(root + "/jobs.json", true))
        return fail(__LINE__);
    QTimer closer;
    connect(&closer, &QTimer::timeout, this, [this] {
        for (auto *box : findChildren<QMessageBox *>())
            if (box->isVisible())
                box->accept();
    });
    closer.start(20);
    auto waitIdle = [this] {
        QElapsedTimer time;
        time.start();
        while ((operations_.busy || queue_.size()) && time.elapsed() < 20000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(1);
        }
        return !operations_.busy && !queue_.size();
    };
    auto write = [&](const QString &path, const QByteArray &bytes) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) &&
               file.write(bytes) == bytes.size();
    };
    try {
        if (recovering) {
            QJsonObject original;
            for (const auto &v : journal_.records()) {
                const auto r = v.toObject();
                if (r["spec"].toObject()["kind"] == "edit")
                    original = r;
            }
            if (original.isEmpty() || original["state"] != "unknown" ||
                original["publication"].toArray().size() != 1)
                return fail(__LINE__);
            auto *item = new QListWidgetItem("Interrupted publication", queueView_);
            item->setData(Qt::UserRole + 1, "Interrupted publication");
            item->setData(Qt::UserRole + 2, true);
            item->setData(Qt::UserRole + 3, original["id"].toString());
            item->setData(Qt::UserRole + 4, original["spec"].toObject());
            item->setData(Qt::UserRole + 5, "unknown");
            queueView_->setCurrentItem(item);
            verifySelectedResult();
            if (!waitIdle())
                return fail(__LINE__);
            const auto state = journal_.record(original["id"].toString())["state"].toString();
            if (state != (mode == "recover-before" ? "failed" : "completed"))
                return fail(__LINE__);
            const auto edits = edits_->records();
            if (edits.size() != 1 || !edits_->validPayload(edits.first().toObject()))
                return fail(__LINE__);
            QFile file(edits.first().toObject()["file"].toString());
            const bool dirty = mode == "recover-after-dirty";
            if (!file.open(QIODevice::ReadOnly) ||
                file.readAll() != (dirty ? "newer unsaved working copy" : "unsaved working copy"))
                return fail(__LINE__);
            file.close();
            if (mode == "recover-after" || dirty) {
                auto op = vynx::new_operation();
                const auto edit = edits.first().toObject();
                const auto hash = string(vynx::hash_archive_file(
                    edit["archive"].toString().toUtf8().toStdString(), *op));
                if (edit["archiveHash"] != hash || edit["state"] != (dirty ? "changed" : "saved") ||
                    edit["baseHash"] != original["spec"].toObject()["inputHash"])
                    return fail(__LINE__);
                qunsetenv("VYNX_WORKER_FAULT");
                if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
                    file.write("second saved edit") != 17)
                    return fail(__LINE__);
                file.close();
                saveWorkingCopy(edit["id"].toString(), secret({}));
                if (!waitIdle() || edits_->record(edit["id"].toString())["state"] != "saved")
                    return fail(__LINE__);
                auto archive =
                    vynx::open_archive(edit["archive"].toString().toUtf8().toStdString(), "", *op);
                if (string(vynx::preview_text(*archive, 0, "", *op)) != "second saved edit")
                    return fail(__LINE__);
            }
            QSaveFile report(root + "/recovery-result.json");
            const auto bytes = QJsonDocument(QJsonObject{{"status", "passed"},
                                                         {"state", state},
                                                         {"working_copy_retained", true}})
                                   .toJson();
            return report.open(QIODevice::WriteOnly) && report.write(bytes) == bytes.size() &&
                   report.commit();
        }
        if (!write(root + "/.vynxarc-fault-qa", "owned fault acceptance\n") ||
            !write(root + "/note.txt", "source content"))
            return fail(__LINE__);
        const auto source = root + "/fault.zip", good = root + "/good.zip";
        for (const auto &archive : {source, good}) {
            QProcess cli;
            cli.setCreateProcessArgumentsModifier(
                [](QProcess::CreateProcessArguments *a) { a->flags |= 0x08000000; });
            cli.start(QCoreApplication::applicationDirPath() + "/vynxarc-cli.exe",
                      {"create", archive, root + "/note.txt"});
            if (!cli.waitForFinished(15000) || cli.exitCode() != 0)
                return fail(__LINE__);
        }
        if (mode == "batch") {
            const auto bad = root + "/bad.zip";
            if (!write(bad, "damaged input"))
                return fail(__LINE__);
            const auto batch = QUuid::createUuid().toString(QUuid::WithoutBraces);
            for (const auto &input : {source, good, bad}) {
                const auto output = root + "/batch-output/" + QFileInfo(input).fileName();
                const QJsonObject spec{{"kind", "convert"},     {"sources", QJsonArray{input}},
                                       {"destination", output}, {"format", 0},
                                       {"preset", 2},           {"resource", 1},
                                       {"allowDecrypt", true},  {"needsOutputPassword", false},
                                       {"batchId", batch}};
                if (!enqueueRequest(
                        "Batch acceptance",
                        [this, spec] { dispatchConversionSpec(spec, secret({}), secret({})); }, {},
                        spec))
                    return fail(__LINE__);
            }
            if (!waitIdle())
                return fail(__LINE__);
            QString failedId;
            int completed = 0, failures = 0;
            for (const auto &value : journal_.records()) {
                const auto row = value.toObject();
                if (row["spec"].toObject()["batchId"] != batch)
                    continue;
                completed += row["state"] == "completed";
                failures += row["state"] == "failed";
                if (row["state"] == "failed")
                    failedId = row["id"].toString();
            }
            if (completed != 2 || failures != 1 || failedId.isEmpty() ||
                QFileInfo::exists(root + "/batch-output/bad.zip"))
                return fail(__LINE__);
            QFile original(good), repaired(bad);
            if (!original.open(QIODevice::ReadOnly) ||
                !repaired.open(QIODevice::WriteOnly | QIODevice::Truncate))
                return fail(__LINE__);
            const auto bytes = original.readAll();
            if (repaired.write(bytes) != bytes.size())
                return fail(__LINE__);
            repaired.close();
            original.close();
            if (!QDir().mkpath(root + "/retried"))
                return fail(__LINE__);
            for (int i = 0; i < queueView_->count(); ++i)
                if (queueView_->item(i)->data(Qt::UserRole + 3).toString() == failedId)
                    queueView_->setCurrentItem(queueView_->item(i));
            bool submitted = false, onlyFailed = false, configured = false;
            QElapsedTimer deadline;
            deadline.start();
            QTimer review;
            connect(&review, &QTimer::timeout, this, [&] {
                auto *dialog = qobject_cast<ConversionDialog *>(QApplication::activeModalWidget());
                if (!dialog)
                    return;
                if (deadline.elapsed() > 15000) {
                    dialog->reject();
                    return;
                }
                if (!configured) {
                    configured = true;
                    onlyFailed = dialog->sources().size() == 1 &&
                                 archiveIdentity(dialog->sources().first()) == archiveIdentity(bad);
                    if (!onlyFailed) {
                        dialog->reject();
                        return;
                    }
                    dialog->destination->setText(root + "/retried");
                    dialog->encrypt->setChecked(false);
                    dialog->allowDecrypt->setChecked(true);
                }
                auto *start = dialog->findChild<QPushButton *>("queueConversion");
                if (start && start->isEnabled()) {
                    submitted = true;
                    review.stop();
                    start->click();
                }
            });
            review.start(20);
            retryFailedBatch();
            review.stop();
            if (!submitted || !onlyFailed || !waitIdle())
                return fail(__LINE__);
            int retries = 0;
            for (const auto &value : journal_.records()) {
                const auto row = value.toObject();
                if (row["spec"].toObject()["retryOf"] != failedId)
                    continue;
                ++retries;
                if (row["state"] != "completed" || row["spec"].toObject()["batchId"] == batch)
                    return fail(__LINE__);
            }
            if (retries != 1 || journal_.record(failedId)["state"] != "failed" ||
                journal_.record(failedId)["error"].toString().isEmpty() ||
                !QFileInfo::exists(root + "/retried/bad.zip"))
                return fail(__LINE__);
            QSaveFile report(root + "/worker-result.json");
            const auto result = QJsonDocument(QJsonObject{{"status", "passed"},
                                                          {"mode", mode},
                                                          {"initial_completed", completed},
                                                          {"initial_failed", failures},
                                                          {"retried_only_failed", true},
                                                          {"original_error_retained", true}})
                                    .toJson();
            return report.open(QIODevice::WriteOnly) && report.write(result) == result.size() &&
                   report.commit();
        }
        openWithSecret(good, secret({}));
        if (!waitIdle() || !session_->archive)
            return fail(__LINE__);
        const auto goodSession = session_;
        openWithSecret(source, secret({}));
        if (!waitIdle() || !session_->archive)
            return fail(__LINE__);
        const auto badArchive = session_->archive;
        const auto editId = edits_->reserve(source, "note.txt", 128);
        const auto editFile = edits_->directory(editId) + "/note.txt";
        if (editId.isEmpty() || !write(editFile, "unsaved working copy") ||
            !edits_->update(editId, QJsonObject{{"file", editFile}, {"state", "ready"}}))
            return fail(__LINE__);
        auto hashOp = vynx::new_operation();
        const auto before = string(vynx::hash_archive_file(source.toUtf8().toStdString(), *hashOp));
        if (!edits_->update(editId, QJsonObject{{"archiveHash", before}}))
            return fail(__LINE__);
        const bool publication = mode == "before" || mode == "after";
        activeSpec_ = QJsonObject{{"kind", publication ? "modify" : "test"},
                                  {"sources", QJsonArray{source}},
                                  {"resource", 0},
                                  {"action", 2},
                                  {"names", QJsonArray{"note.txt"}},
                                  {"newName", "renamed.txt"}};
        QString observed;
        if (publication) {
            saveWorkingCopy(editId, secret({}));
            QCoreApplication::processEvents();
        } else {
            beginOperation();
            auto op = operations_.operation;
            vynx::configure_operation(**op, 2, 0);
            runJob(
                "Fault acceptance",
                [badArchive, op, publication] {
                    if (publication) {
                        rust::Vec<rust::String> names;
                        names.push_back("note.txt");
                        vynx::modify_archive(**badArchive, 2, {names.data(), names.size()},
                                             "renamed.txt", "", "", **op);
                    } else
                        vynx::test_archive(**badArchive, "", **op);
                    return QString();
                },
                {}, [&observed](QString error) { observed = error; });
        }
        activateSession(sessions_.indexOf(goodSession));
        if (!archivePage_->isEnabled())
            return fail(__LINE__);
        std::atomic_int ticks = 0;
        QTimer heartbeat;
        connect(&heartbeat, &QTimer::timeout, this, [&ticks] { ++ticks; });
        heartbeat.start(20);
        auto read = QtConcurrent::run([archive = goodSession->archive] {
            try {
                auto op = vynx::new_operation();
                return !vynx::preview_bytes(**archive, 0, "", *op).empty();
            } catch (...) {
                return false;
            }
        });
        if (mode == "hang")
            QTimer::singleShot(250, this, [this] {
                if (operations_.operation)
                    vynx::cancel(**operations_.operation);
            });
        if (!waitIdle())
            return fail(__LINE__);
        read.waitForFinished();
        if (publication)
            observed = edits_->record(editId)["error"].toString();
        if (!read.result() || session_ != goodSession ||
            !edits_->validPayload(edits_->record(editId)))
            return fail(__LINE__);
        if (publication && !observed.startsWith("RESULT_UNKNOWN:"))
            return fail(__LINE__);
        if (mode == "crash" && !observed.startsWith("WORKER_STOPPED:"))
            return fail(__LINE__);
        if (mode == "memory" && !observed.startsWith("WORKER_MEMORY:"))
            return fail(__LINE__);
        if (mode == "hang" && (!observed.startsWith("CANCELLED:") || ticks < 10))
            return fail(__LINE__);
        const auto after = string(vynx::hash_archive_file(source.toUtf8().toStdString(), *hashOp));
        if (mode == "before" && before != after)
            return fail(__LINE__);
        if (mode == "after" && before == after)
            return fail(__LINE__);
        QSaveFile report(root + "/worker-result.json");
        const auto bytes = QJsonDocument(QJsonObject{{"status", "passed"},
                                                     {"mode", mode},
                                                     {"error", observed},
                                                     {"heartbeat_ticks", ticks.load()},
                                                     {"other_tab_readable", true},
                                                     {"working_copy_retained", true},
                                                     {"original_preserved", before == after}})
                               .toJson();
        return report.open(QIODevice::WriteOnly) && report.write(bytes) == bytes.size() &&
               report.commit();
    } catch (const std::exception &e) {
        qWarning("Worker acceptance error: %s", e.what());
        return fail(__LINE__);
    }
}
