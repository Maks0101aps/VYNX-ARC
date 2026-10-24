#include "MainWindow.h"
#include "ArchiveModel.h"
#include "NestedStore.h"
#include "SecretUtf8.h"
#include "TempStore.h"
#include "dialogs/InterArchiveCopyDialog.h"
#include "widgets/BreadcrumbBar.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeData>
#include <QMessageBox>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <QTimer>
#include <QtWidgets>

static QString fileFingerprint(const QString &path) {
    QFileInfo fi(path);
    if (!fi.exists())
        return {};
    return QString("%1:%2").arg(fi.size()).arg(fi.lastModified().toMSecsSinceEpoch());
}

static std::string utf8(const QString &s) {
    auto b = s.toUtf8();
    return std::string(b.constData(), size_t(b.size()));
}

bool isArchiveEntry(const QString &name) {
    const QString lower = name.toLower();
    for (const auto &ext : {".zip", ".7z", ".tar", ".gz", ".tgz", ".tar.gz",
                            ".xz", ".tar.xz", ".bz2", ".tar.bz2", ".zst",
                            ".tar.zst", ".rar", ".lzma"}) {
        if (lower.endsWith(ext))
            return true;
    }
    return false;
}

static QString pickUniqueName(const QString &baseName, QSet<QString> &existingNamesLower) {
    QFileInfo fi(baseName);
    QString stem = fi.completeBaseName();
    QString ext = fi.suffix().isEmpty() ? QString() : "." + fi.suffix();
    QString candidate = baseName;
    int counter = 1;
    while (existingNamesLower.contains(candidate.toLower())) {
        candidate = QStringLiteral("%1 (%2)%3").arg(stem).arg(counter).arg(ext);
        counter++;
    }
    existingNamesLower.insert(candidate.toLower());
    return candidate;
}

void MainWindow::openNestedArchive(const ArchiveRow &row) {
    if (operations_.busy || session_->busy || !session_->archive)
        return;

    const int currentDepth = session_->nestedInfo ? session_->nestedInfo->depth : 0;
    if (currentDepth >= NestedStore::MaxDepth) {
        QMessageBox::warning(this, tr("Nested archive"),
                             tr("Cannot open nested archive: maximum depth limit (%1) reached.")
                                 .arg(NestedStore::MaxDepth));
        return;
    }

    if (row.size > NestedStore::MaxContainerBytes) {
        QMessageBox::warning(this, tr("Nested archive"),
                             tr("Cannot open nested archive: file size (%1) exceeds maximum container limit of %2.")
                                 .arg(displaySize(row.size), displaySize(NestedStore::MaxContainerBytes)));
        return;
    }

    if (!nestedStore_)
        nestedStore_ = std::make_unique<NestedStore>();

    const auto nestedId = nestedStore_->reserve(session_->path, row.fullPath, currentDepth + 1, row.size);
    if (nestedId.isEmpty()) {
        QMessageBox::warning(this, tr("Nested archive"),
                             tr("Cannot allocate space for nested archive: %1").arg(nestedStore_->error()));
        return;
    }

    const auto nestedDir = nestedStore_->directory(nestedId);
    auto actualDestFile = std::make_shared<QString>();

    beginOperation();
    activeSpec_ = QJsonObject{
        {"kind", "extract_nested"},
        {"sources", QJsonArray{session_->path}},
        {"nestedId", nestedId},
        {"nestedDir", nestedDir},
        {"entryId", static_cast<double>(row.id)},
        {"entryName", row.name}
    };

    auto archive = session_->archive;
    auto pw = session_->password ? session_->password : secret({});
    auto op = operations_.operation;
    const auto id = row.id;
    const auto rowName = row.name;
    const auto parentPath = session_->path;
    const auto parentSessionId = session_->id;
    const auto parentFp = fileFingerprint(parentPath);

    runJob(
        tr("Extracting nested archive %1...").arg(rowName),
        [archive, id, nestedDir, pw, op, actualDestFile] {
            rust::String destFolderStr(nestedDir.toUtf8().constData(), nestedDir.toUtf8().size());
            try {
                auto mat = vynx::materialize_entry(**archive, id, destFolderStr, pw->bytes(), **op);
                *actualDestFile = QString::fromUtf8(mat.data(), mat.size());
            } catch (const std::exception &e) {
                return QString::fromUtf8(e.what());
            }
            return vynx::cancelled(**op) ? QStringLiteral("CANCELLED: Extraction cancelled") : QString();
        },
        [this, nestedId, actualDestFile, parentPath, parentSessionId, parentFp, currentDepth, row] {
            const QString destFile = *actualDestFile;
            if (destFile.isEmpty() || !QFile::exists(destFile) || QFileInfo(destFile).size() == 0) {
                nestedStore_->discard(nestedId);
                QMessageBox::warning(this, tr("Nested archive"),
                                     tr("Failed to extract nested container '%1'.").arg(row.name));
                return;
            }

            auto rec = nestedStore_->record(nestedId);
            rec["state"] = "ready";
            rec["file"] = destFile;
            rec["containerBytes"] = double(QFileInfo(destFile).size());
            nestedStore_->update(nestedId, rec);

            auto nestedSession = std::make_shared<ArchiveSession>();
            nestedSession->nestedInfo = std::make_shared<NestedArchiveInfo>();
            nestedSession->nestedInfo->parentSessionId = parentSessionId;
            nestedSession->nestedInfo->parentArchivePath = parentPath;
            nestedSession->nestedInfo->parentEntryPath = row.fullPath;
            nestedSession->nestedInfo->parentFingerprint = parentFp;
            nestedSession->nestedInfo->nestedStoreId = nestedId;
            nestedSession->nestedInfo->tempContainerPath = destFile;
            nestedSession->nestedInfo->depth = currentDepth + 1;

            sessions_.append(nestedSession);
            tabs_->addTab(QFileInfo(row.name).fileName());
            tabs_->show();
            activateSession(sessions_.size() - 1);

            openPath(destFile);
        },
        [this, nestedId](QString err) {
            nestedStore_->discard(nestedId);
            if (!err.startsWith("CANCELLED:"))
                QMessageBox::warning(this, tr("Nested archive"),
                                     tr("Error extracting nested archive: %1").arg(err));
        });
}

void MainWindow::navigateToParentArchive(int parentLevel) {
    Q_UNUSED(parentLevel);
    if (!session_->nestedInfo)
        return;
    const auto parentSessionId = session_->nestedInfo->parentSessionId;
    const auto parentPath = session_->nestedInfo->parentArchivePath;

    for (int i = 0; i < sessions_.size(); ++i) {
        if (sessions_[i]->id == parentSessionId || sessions_[i]->path == parentPath) {
            activateSession(i);
            return;
        }
    }
    if (QFile::exists(parentPath))
        openPath(parentPath);
}

void MainWindow::copySelectedToClipboard() {
    if (!session_->archive)
        return;
    const auto rows = table_->selectionModel()->selectedRows();
    if (rows.isEmpty())
        return;

    clipboard_.clear();
    clipboard_.sourceArchive = session_->path;
    clipboard_.sourceSessionId = session_->id;
    clipboard_.sourceFingerprint = fileFingerprint(session_->path);
    clipboard_.sourcePassword = session_->password;

    for (const auto &idx : rows) {
        const auto &r = session_->model->row(session_->filter->mapToSource(idx).row());
        ArchiveClipboardItem it;
        it.id = r.id;
        it.name = r.name;
        it.fullPath = r.fullPath;
        it.size = r.size;
        it.folder = r.folder;
        it.memberIds = r.memberIds;
        clipboard_.items.append(it);
    }

    statusBar()->showMessage(tr("Copied %1 items to archive clipboard (use Ctrl+V in target archive to paste)")
                                 .arg(clipboard_.items.size()), 4000);
}

void MainWindow::pasteFromMimeData(const QMimeData *mime) {
    if (!mime || !mime->hasFormat("application/x-vynx-archive-copy"))
        return;
    const auto jsonBytes = mime->data("application/x-vynx-archive-copy");
    if (jsonBytes.size() > 16 * 1024 * 1024) return;
    const auto doc = QJsonDocument::fromJson(jsonBytes);
    if (!doc.isObject())
        return;
    const auto obj = doc.object();
    const auto sourceSession = obj["sourceSessionId"].toString();
    std::shared_ptr<ArchiveSession> source;
    for (const auto &candidate : sessions_)
        if (candidate->id == sourceSession && candidate->path == obj["sourceArchive"].toString())
            source = candidate;
    if (!source || source->busy) return;
    clipboard_.clear();
    clipboard_.sourceArchive = obj["sourceArchive"].toString();
    clipboard_.sourceSessionId = obj["sourceSessionId"].toString();
    clipboard_.sourceFingerprint = obj["sourceFingerprint"].toString();
    const auto itemsArr = obj["items"].toArray();
    for (const auto &val : itemsArr) {
        const auto itemObj = val.toObject();
        ArchiveClipboardItem it;
        it.id = quint64(itemObj["id"].toDouble());
        it.fullPath = itemObj["fullPath"].toString();
        bool found = false;
        for (int row = 0; row < source->model->rowCount(); ++row) {
            const auto &actual = source->model->row(row);
            if (actual.id == it.id && actual.fullPath == it.fullPath) {
                it.name = actual.name; it.size = actual.size;
                it.folder = actual.folder; it.memberIds = actual.memberIds;
                found = true; break;
            }
        }
        if (!found) { clipboard_.clear(); return; }
        clipboard_.items.append(it);
    }
    pasteFromClipboard();
}

void MainWindow::pasteFromClipboard() {
    if (clipboard_.isEmpty())
        return;
    if (operations_.busy || session_->busy || !session_->archive)
        return;

    if (!writableArchive()) {
        QMessageBox::warning(this, tr("Paste between archives"),
                             tr("The active destination archive cannot be modified directly. "
                                "Only ZIP, 7Z and unlocked RAR5 archives support adding copied files."));
        return;
    }

    const auto curFp = fileFingerprint(clipboard_.sourceArchive);
    if (!clipboard_.sourceFingerprint.isEmpty() && curFp != clipboard_.sourceFingerprint) {
        QMessageBox::warning(this, tr("Paste between archives"),
                             tr("The source archive '%1' was modified after files were copied. "
                                "Operation cancelled to avoid inconsistent data.")
                                 .arg(QFileInfo(clipboard_.sourceArchive).fileName()));
        return;
    }

    const QString targetFolder = session_->model->folder();
    QSet<QString> existingNamesLower;
    for (int i = 0; i < session_->model->rowCount(); ++i) {
        const auto &r = session_->model->row(i);
        existingNamesLower.insert(r.name.toLower());
    }

    QVector<CopyCandidateItem> candidates;
    for (const auto &it : clipboard_.items) {
        CopyCandidateItem c;
        c.id = it.id;
        c.name = it.name;
        c.fullPath = it.fullPath;
        c.size = it.size;
        c.folder = it.folder;
        c.memberIds = it.memberIds;
        c.hasConflict = existingNamesLower.contains(it.name.toLower());
        c.targetPath = targetFolder.isEmpty() ? it.name : (targetFolder + "/" + it.name);
        candidates.append(c);
    }

    QStorageInfo storage(session_->path);
    quint64 availableSpace = storage.bytesAvailable();

    InterArchiveCopyDialog dialog(clipboard_.sourceArchive, session_->path, targetFolder, candidates, availableSpace, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const auto policy = dialog.policy();
    const auto rawResolved = dialog.resolvedItems();
    if (rawResolved.isEmpty())
        return;

    // Apply unique rename policy collision avoidance
    QVector<CopyCandidateItem> resolved;
    for (auto item : rawResolved) {
        if (policy == CopyConflictPolicy::Rename && existingNamesLower.contains(item.name.toLower())) {
            item.name = pickUniqueName(item.name, existingNamesLower);
        } else {
            existingNamesLower.insert(item.name.toLower());
        }
        resolved.append(item);
    }

    auto staging = std::make_shared<QTemporaryDir>();
    if (!staging->isValid()) {
        QMessageBox::warning(this, tr("Copy failed"), tr("Cannot create temporary staging directory."));
        return;
    }

    const auto stagingPath = staging->path();
    auto values = std::make_shared<rust::Vec<rust::String>>();

    // Determine source password
    auto srcPw = clipboard_.sourcePassword;
    if (!srcPw || srcPw->bytes().empty()) {
        for (const auto &s : sessions_) {
            if (s->path == clipboard_.sourceArchive && s->password) {
                srcPw = s->password;
                break;
            }
        }
    }
    if (!srcPw)
        srcPw = secret({});

    const auto srcPath = clipboard_.sourceArchive;
    const auto targetPath = session_->path;
    const auto targetPw = session_->password ? session_->password : secret({});
    const auto destinationFolder = utf8(targetFolder);
    auto targetArchive = session_->archive;

    beginOperation();
    activeSpec_ = QJsonObject{
        {"kind", "copy"},
        {"sources", QJsonArray{srcPath}},
        {"targetArchive", targetPath},
        {"targetFolder", targetFolder},
        {"sourceArchive", srcPath},
        {"conflictPolicy", static_cast<int>(policy)},
        {"itemCount", resolved.size()}
    };

    auto op = operations_.operation;

    runJob(
        tr("Copying %1 items into %2...").arg(resolved.size()).arg(QFileInfo(targetPath).fileName()),
        [srcPath, srcPw, resolved, stagingPath, values, targetArchive, destinationFolder, targetPw, op] {
            const auto sourceDigest = vynx::hash_archive_file(srcPath.toUtf8().toStdString(), **op);
            // Open source archive inside background worker safely
            rust::Box<vynx::Archive> srcArchive(vynx::open_archive(
                rust::Str(srcPath.toUtf8().constData(), srcPath.toUtf8().size()),
                srcPw ? srcPw->bytes() : rust::Str(""),
                **op));

            for (const auto &item : resolved) {
                if (vynx::cancelled(**op))
                    return QStringLiteral("CANCELLED: Copy cancelled");

                if (!item.folder) {
                    // Regular file: materialize into dedicated subfolder
                    QString itemDir = stagingPath + "/file_" + QString::number(item.id);
                    QDir().mkpath(itemDir);
                    rust::String dirStr(itemDir.toUtf8().constData(), itemDir.toUtf8().size());
                    rust::String extractedStr = vynx::materialize_entry(
                        *srcArchive, item.id, dirStr, srcPw ? srcPw->bytes() : rust::Str(""), **op);
                    QString extractedPath = QString::fromUtf8(extractedStr.data(), extractedStr.size());
                    QFile ownedCopy(extractedPath);
                    if (!ownedCopy.setPermissions(ownedCopy.permissions() | QFileDevice::WriteOwner))
                        return QStringLiteral("IO: Cannot prepare disposable staging copy");

                    QString finalFilePath = itemDir + "/" + item.name;
                    if (item.name != QFileInfo(extractedPath).fileName() && QFile::exists(extractedPath)) {
                        QFile::rename(extractedPath, finalFilePath);
                    } else if (QFile::exists(extractedPath)) {
                        finalFilePath = extractedPath;
                    }

                    if (QFile::exists(finalFilePath)) {
                        values->push_back(rust::String(finalFilePath.toUtf8().constData(), finalFilePath.toUtf8().size()));
                    }
                } else {
                    // Folder: extract members via extract_archive
                    QString folderStaging = stagingPath + "/folder_" + QString::number(item.id);
                    QDir().mkpath(folderStaging);

                    rust::Vec<quint64> memberIds;
                    for (auto mid : item.memberIds)
                        memberIds.push_back(mid);

                    rust::String folderDestStr(folderStaging.toUtf8().constData(), folderStaging.toUtf8().size());
                    vynx::extract_archive(*srcArchive, folderDestStr,
                                          rust::Slice<const quint64>(memberIds.data(), memberIds.size()),
                                          2 /* Replace */, false /* smart */,
                                          srcPw ? srcPw->bytes() : rust::Str(""), **op);

                    QString extractedFolder = folderStaging + "/" + item.fullPath;
                    QString finalFolderPath = folderStaging + "/" + item.name;
                    if (item.name != QFileInfo(item.fullPath).fileName() && QFile::exists(extractedFolder)) {
                        QDir().rename(extractedFolder, finalFolderPath);
                    } else if (QFile::exists(extractedFolder)) {
                        finalFolderPath = extractedFolder;
                    } else if (QFile::exists(folderStaging + "/" + item.name)) {
                        finalFolderPath = folderStaging + "/" + item.name;
                    }

                    if (QFile::exists(finalFolderPath)) {
                        values->push_back(rust::String(finalFolderPath.toUtf8().constData(), finalFolderPath.toUtf8().size()));
                    }
                }
            }

            if (values->empty())
                return QStringLiteral("No files were extracted from source archive.");

            if (std::string(vynx::hash_archive_file(srcPath.toUtf8().toStdString(), **op)) != std::string(sourceDigest))
                return QStringLiteral("CHANGED: Source changed while copying; target was not written");
            vynx::modify_archive(**targetArchive, 0,
                                 rust::Slice<const rust::String>(values->data(), values->size()),
                                 rust::Str(""), destinationFolder, targetPw->bytes(), **op);
            return QString();
        },
        [this, targetPath, targetPw, staging] {
            QTimer::singleShot(0, this, [this, targetPath, targetPw] {
                reloadArchive(targetPath, targetPw);
                statusBar()->showMessage(tr("Successfully copied files into archive."), 5000);
            });
        },
        [this, targetPath, targetPw](QString err) {
            if (err.contains("password", Qt::CaseInsensitive)) {
                bool accepted = false;
                auto next = secret(askPassword(&accepted));
                if (accepted) {
                    clipboard_.sourcePassword = next;
                    QTimer::singleShot(0, this, &MainWindow::pasteFromClipboard);
                    return;
                }
            }
            if (!err.startsWith("CANCELLED:"))
                QMessageBox::warning(this, tr("Copy failed"), tr("Failed to copy files between archives: %1").arg(err));
        });
}
