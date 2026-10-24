#include "MainWindow.h"
#include "NestedStore.h"
#include "TempStore.h"
#include "SecretUtf8.h"
#include "dialogs/InterArchiveCopyDialog.h"
#include "dialogs/ExtractionRecoveryDialog.h"
#include "dialogs/ExtractDialog.h"
#include "pages/ArchivePage.h"
#include <QtWidgets>
#include <QJsonDocument>
#include <QSaveFile>

// Exercises actual window handlers against disposable files, rather than fixtures
// that only resemble their states. Available only via an explicit smoke argument.
bool MainWindow::productAcceptance(const QString &root) {
    if (QFileInfo::exists(root) || !QDir().mkpath(root)) return false;
    settings_ = std::make_unique<QSettings>(root + "/settings.ini", QSettings::IniFormat);
    if (!journal_.open(root + "/jobs.json", true)) return false;
    nestedStore_ = std::make_unique<NestedStore>(root + "/nested");
    tempStore_ = std::make_unique<TempStore>(externalFiles_.get(), nestedStore_.get(), edits_.get());
    QString error;
    QStringList notices;
    QString extractDestination;
    QString appearDuringRecovery;
    QTimer dialogs;
    connect(&dialogs, &QTimer::timeout, this, [&] {
        for (auto *box : findChildren<QMessageBox *>()) {
            if (box->isVisible()) {
                notices.append(box->text()); box->accept();
            }
        }
        for (auto *dialog : findChildren<InterArchiveCopyDialog *>())
            if (dialog->isVisible()) dialog->accept();
        for (auto *dialog : findChildren<ExtractDialog *>())
            if (dialog->isVisible() && !extractDestination.isEmpty()) {
                dialog->destination->setText(extractDestination);
                dialog->smartBox->setChecked(true);
                dialog->accept();
            }
        for (auto *dialog : findChildren<ExtractionRecoveryDialog *>())
            if (dialog->isVisible() && dialog->canResume()) {
                if (!appearDuringRecovery.isEmpty()) {
                    QFile late(appearDuringRecovery);
                    if (late.open(QIODevice::WriteOnly)) late.write("racing user data");
                    appearDuringRecovery.clear();
                }
                dialog->accept();
            }
        for (auto *dialog : findChildren<QInputDialog *>())
            if (dialog->isVisible()) { dialog->setTextValue("test-secret"); dialog->accept(); }
    });
    dialogs.start(10);
    auto idle = [&] {
        QElapsedTimer timeout; timeout.start();
        int stable = 0;
        while (timeout.elapsed() < 60000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QThread::msleep(2);
            if (!operations_.busy && !queue_.size() && readers_.isEmpty()) {
                if (++stable == 5) return true;
            } else stable = 0;
        }
        return false;
    };
    auto write = [](const QString &path, const QByteArray &bytes) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    };
    auto create = [&](const QString &path, const QStringList &inputs, const char *password = "") {
        rust::Vec<rust::String> values;
        for (const auto &input : inputs) values.push_back(input.toUtf8().toStdString());
        auto op = vynx::new_operation();
        vynx::create_archive_as(path.toUtf8().toStdString(), {values.data(), values.size()},
                                path.endsWith(".rar") ? 12 : path.endsWith(".7z") ? 1 : 0, password, *op);
    };
    auto open = [&](const QString &path, const QString &password = {}) {
        openWithSecret(path, secret(password));
        return idle() && session_->archive && session_->path == path;
    };
    auto select = [&](const QString &name) {
        for (int i = 0; i < session_->filter->rowCount(); ++i) {
            const auto index = session_->filter->index(i, 0);
            if (session_->model->row(session_->filter->mapToSource(index).row()).name == name) {
                table_->selectionModel()->select(index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
                return index;
            }
        }
        return QModelIndex();
    };
    auto fail = [&](int line) {
        qWarning("Product acceptance failed at line %d", line);
        for (const auto &notice : notices) qWarning().noquote() << notice;
        for (const auto &value : journal_.records()) {
            const auto record = value.toObject();
            if (!record["error"].toString().isEmpty()) qWarning().noquote() << record["spec"] << record["error"];
        }
        return false;
    };
    QJsonArray results;
    try {
        const QString note = root + "/input/note.txt";
        const QString folder = root + "/input/folder";
        if (!write(note, "source bytes") || !write(folder + "/child.txt", "folder bytes")) return fail(__LINE__);
        const QString source = root + "/source.zip";
        create(source, {note, folder}, "test-secret");
        const QString original = root + "/original/note.txt";
        const QString reserved = root + "/original/note (1).txt";
        if (!write(original, "original") || !write(reserved, "reserved")) return fail(__LINE__);
        for (const auto &suffix : {QString(".zip"), QString(".7z"), QString(".rar")}) {
            const QString target = root + "/target" + suffix;
            create(target, {original, reserved});
            if (!open(source, "test-secret")) return fail(__LINE__);
            auto sourceSession = session_;
            if (!open(target)) return fail(__LINE__);
            auto targetSession = session_;
            activateSession(sessions_.indexOf(sourceSession));
            table_->clearSelection();
            if (!select("note.txt").isValid() || !select("folder").isValid()) return fail(__LINE__);
            // Use the actual model MIME producer and window drop receiver.
            std::unique_ptr<QMimeData> mime(session_->filter->mimeData(table_->selectionModel()->selectedRows()));
            activateSession(sessions_.indexOf(targetSession));
            pasteFromMimeData(mime.get());
            if (!idle() || !notices.isEmpty()) return fail(__LINE__);
            auto op = vynx::new_operation();
            auto archive = vynx::open_archive(target.toUtf8().toStdString(), "", *op);
            QHash<QString, QByteArray> expected{{"note.txt", "original"}, {"note (1).txt", "reserved"},
                                                {"note (2).txt", "source bytes"}, {"folder/child.txt", "folder bytes"}};
            for (const auto &entry : vynx::list_entries(*archive)) {
                const auto name = QString::fromUtf8(entry.name.data(), qsizetype(entry.name.size()));
                if (!expected.contains(name)) continue;
                const auto bytes = vynx::preview_bytes(*archive, entry.id, "", *op);
                if (QByteArray(reinterpret_cast<const char *>(bytes.data()), qsizetype(bytes.size())) != expected.take(name))
                    return fail(__LINE__);
            }
            if (!expected.isEmpty()) return fail(__LINE__);
            results.append("encrypted source -> " + suffix + ": MIME, folder, keep-both verified");
        }
        // Generate and open five actual nested containers; the fifth descent is refused.
        QString inner = note;
        for (int level = 4; level >= 0; --level) {
            const QString path = root + QString("/level%1.zip").arg(level);
            create(path, {inner}); inner = path;
        }
        if (!open(inner)) return fail(__LINE__);
        for (int depth = 1; depth <= 4; ++depth) {
            table_->clearSelection();
            const auto index = select(QString("level%1.zip").arg(depth));
            if (!index.isValid()) return fail(__LINE__);
            QMetaObject::invokeMethod(table_, "doubleClicked", Qt::DirectConnection, Q_ARG(QModelIndex, index));
            if (!idle() || !session_->nestedInfo || session_->nestedInfo->depth != depth || !session_->archive)
                return fail(__LINE__);
        }
        ArchiveRow blocked; blocked.name = "blocked.zip"; blocked.size = 1;
        const auto count = sessions_.size();
        openNestedArchive(blocked);
        if (sessions_.size() != count || notices.isEmpty() || !notices.last().contains("depth")) return fail(__LINE__);
        notices.clear();
        const auto path = session_->path;
        closeSession(tabs_->currentIndex());
        if (QFile::exists(path)) return fail(__LINE__);
        results.append("four nested opens, fifth descent refused, readonly container cleanup verified");
        // Directly exercise the recovery dialog's real classification and non-mutating planner.
        const QString destination = root + "/recovery";
        if (!write(destination + "/note.txt", "user edit") || !write(destination + "/note.txt.personal.tmp", "backup"))
            return fail(__LINE__);
        QJsonObject plan{{"files", QJsonArray{
            QJsonObject{{"name", "note.txt"}, {"size", 9}, {"modified", 0}, {"crc", "00000000"}},
            QJsonObject{{"name", "missing.txt"}, {"size", 4}}}}};
        ExtractionRecoveryDialog recovery(source, destination, {}, plan, this);
        const auto remaining = recovery.filesToExtract();
        if (remaining.size() != 1 || remaining.first().name != "missing.txt" ||
            !QFile::exists(destination + "/note.txt") || !QFile::exists(destination + "/note.txt.personal.tmp"))
            return fail(__LINE__);
        results.append("recovery planning preserves modified output and unrelated temp files");
        // Run actual extraction, persist its source digest/root, then recover an
        // unknown job through the queue. Existing edits must survive this path.
        if (!open(source, "test-secret")) return fail(__LINE__);
        extractDestination = root + "/smart-output";
        chooseExtract(true, false, false);
        if (!idle() || !notices.isEmpty()) return fail(__LINE__);
        const QString outputRoot = extractDestination + "/source";
        if (!QFile::exists(outputRoot + "/note.txt") || !QFile::remove(outputRoot + "/note.txt") ||
            !write(outputRoot + "/folder/child.txt", "new user edit") ||
            !write(outputRoot + "/note.txt.personal.tmp", "unrelated backup")) return fail(__LINE__);
        QJsonObject extraction;
        for (const auto &record : journal_.records())
            if (record.toObject()["spec"].toObject()["kind"] == "extract") extraction = record.toObject();
        const auto planIdentity = extraction["spec"].toObject()["recoveryPlan"].toObject();
        if (planIdentity["sourceDigest"].toString().isEmpty() ||
            QDir::cleanPath(QDir::fromNativeSeparators(planIdentity["destination"].toString())) !=
                QDir::cleanPath(QDir::fromNativeSeparators(outputRoot)))
            return fail(__LINE__);
        const auto extractionId = extraction["id"].toString();
        if (!journal_.setState(extractionId, "unknown")) return fail(__LINE__);
        for (int row = 0; row < queueView_->count(); ++row) {
            auto *item = queueView_->item(row);
            if (item->data(Qt::UserRole + 3).toString() == extractionId) {
                item->setData(Qt::UserRole + 2, true);
                item->setData(Qt::UserRole + 5, "unknown");
                queueView_->setCurrentItem(item);
                break;
            }
        }
        retrySelectedJob();
        if (!idle() || !notices.isEmpty() || !QFile::exists(outputRoot + "/note.txt")) return fail(__LINE__);
        QFile changed(outputRoot + "/folder/child.txt"), backup(outputRoot + "/note.txt.personal.tmp");
        if (!changed.open(QIODevice::ReadOnly) || changed.readAll() != "new user edit" ||
            !backup.open(QIODevice::ReadOnly) || backup.readAll() != "unrelated backup") return fail(__LINE__);
        results.append("unknown Smart Extract queue recovery verifies source digest and preserves existing edits/backups");
        if (!QFile::remove(outputRoot + "/note.txt")) return fail(__LINE__);
        appearDuringRecovery = outputRoot + "/note.txt";
        // The selection still points to the original request; retry is explicit.
        retrySelectedJob();
        if (!idle()) return fail(__LINE__);
        QFile raced(outputRoot + "/note.txt");
        if (!raced.open(QIODevice::ReadOnly) || raced.readAll() != "racing user data") return fail(__LINE__);
        bool conflictRecorded = false;
        for (const auto &record : journal_.records())
            if (record.toObject()["error"].toString().contains("CONFLICT")) conflictRecorded = true;
        if (!conflictRecorded) return fail(__LINE__);
        results.append("output appearing after recovery review is refused atomically and retained");
        ArchiveJobSpec rarJob;
        rarJob.kind = JobKind::Create;
        rarJob.sources = {note, folder};
        rarJob.destination = root + "/native.rar";
        rarJob.format = ArchiveFormat::Rar;
        rarJob.preset = CompressionPreset::Fast;
        rarJob.rarSolid = true;
        rarJob.rarHeaders = true;
        rarJob.rarRecovery = 3;
        rarJob.needsPassword = true;
        dispatchSpec(rarJob, secret("test-secret"));
        if (!idle() || !QFile::exists(rarJob.destination) || !open(rarJob.destination, "test-secret") || !writableArchive()) return fail(__LINE__);
        auto rarOp = vynx::new_operation();
        vynx::test_archive(**session_->archive, "test-secret", *rarOp);
        bool recordedRar = false;
        for (const auto &value : journal_.records()) {
            const auto record = value.toObject();
            const auto spec = record["spec"].toObject();
            if (spec["destination"] == rarJob.destination && record["state"] == "completed" &&
                spec["rarSolid"].toBool() && spec["rarHeaders"].toBool() && spec["rarRecovery"].toInt() == 3)
                recordedRar = true;
        }
        if (!recordedRar) return fail(__LINE__);
        results.append("native RAR5 solid/AES headers/recovery creation through typed GUI dispatch, worker, publication acknowledgement and journal");
        nestedStore_->cleanup({});
        QSaveFile report(root + "/product.json");
        const auto bytes = QJsonDocument(QJsonObject{{"status", "passed"}, {"cases", results}}).toJson();
        return report.open(QIODevice::WriteOnly) && report.write(bytes) == bytes.size() && report.commit();
    } catch (const std::exception &exception) {
        qWarning().noquote() << exception.what();
        return fail(__LINE__);
    }
}
