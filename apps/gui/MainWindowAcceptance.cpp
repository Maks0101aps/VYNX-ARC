#include "EditStore.h"
#include "MainWindow.h"
#include "SecretUtf8.h"
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
    });
    closer.start(20);
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
    QElapsedTimer time;
    QJsonArray opens, extracts, searches, cycles;
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
        QSaveFile report(root + "/results.json");
        const auto bytes =
            QJsonDocument(QJsonObject{{"status", "passed"},
                                      {"open_seconds", opens},
                                      {"selected_extract_seconds", extracts},
                                      {"search_milliseconds", searches},
                                      {"resource_cycle_seconds", cycles},
                                      {"cancellation_seconds", cancellationSeconds},
                                      {"heartbeat_ticks", heartbeats},
                                      {"cache_scope", "First and repeated GUI process operations; "
                                                      "OS filesystem cache uncontrolled"}})
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
                     "recover-after", "recover-after-dirty"}
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
