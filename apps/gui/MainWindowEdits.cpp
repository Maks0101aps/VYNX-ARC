#include "EditStore.h"
#include "ExternalFiles.h"
#include "TempStore.h"
#include "NestedStore.h"
#include "dialogs/TemporaryStorageDialog.h"
#include "MainWindow.h"
#include "SecretUtf8.h"
#include "pages/ArchivePage.h"
#include "widgets/ToastOverlay.h"
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QtWidgets>
static QString value(const rust::String &text) {
    return QString::fromUtf8(text.data(), qsizetype(text.size()));
}
void MainWindow::editSelected() {
    if (operations_.busy || session_->busy || !writableArchive())
        return;
    const auto rows = table_->selectionModel()->selectedRows();
    if (rows.size() != 1)
        return;
    const auto &row = session_->model->row(session_->filter->mapToSource(rows.first()).row());
    if (row.folder || !ExternalFiles::allowed(row.fullPath)) {
        QMessageBox::information(
            this, tr("Edit working copy"),
            tr("This file type cannot be edited directly. Extract it to a chosen folder instead."));
        return;
    }
    if (!edits_->available()) {
        QMessageBox::warning(this, tr("Working copies"), edits_->error());
        return;
    }
    QMessageBox consent(QMessageBox::Question, tr("Edit working copy"),
                        tr("Create an editable copy of %1? Decrypted content and its source path "
                           "will remain on this computer until you discard the working copy. "
                           "Changes return to ZIP/7Z only when you explicitly save them.")
                            .arg(row.fullPath),
                        QMessageBox::Ok | QMessageBox::Cancel, this);
    consent.setTextFormat(Qt::PlainText);
    consent.setDefaultButton(QMessageBox::Cancel);
    if (consent.exec() != QMessageBox::Ok)
        return;
    prepareEdit(row.id, row.fullPath, true, row.size);
}
void MainWindow::prepareEdit(quint64 id, const QString &name, bool launch, quint64 expectedBytes) {
    if (operations_.busy || session_->busy || !session_->archive)
        return;
    const auto key = edits_->reserve(session_->path, name, expectedBytes);
    if (key.isEmpty()) {
        QMessageBox::warning(
            this, tr("Working copies"),
            tr("Could not allocate a working copy. Close another instance or "
               "discard unused copies (32 copies / 2 GiB budget). Unsaved copies are retained."));
        return;
    }
    const auto directory = edits_->directory(key);
    auto manifest = std::make_shared<QJsonObject>(edits_->record(key));
    auto archive = session_->archive;
    auto pw = session_->password ? session_->password : secret({});
    const auto source = session_->path;
    beginOperation();
    activeSpec_ = QJsonObject{{"kind", "edit"}, {"sources", QJsonArray{source}}, {"editId", key}};
    auto op = operations_.operation;
    runJob(
        tr("Preparing editable copy..."),
        [archive, pw, source, directory, manifest, id, op] {
            const auto before = value(vynx::hash_archive_file(source.toUtf8().toStdString(), **op));
            (*manifest)["archiveHash"] = before;
            if (!EditStore::write(directory, *manifest))
                return QStringLiteral("IO: Cannot record edit recovery metadata");
            const auto file = value(vynx::materialize_entry(
                **archive, id, directory.toUtf8().toStdString(), pw->bytes(), **op));
            (*manifest)["file"] = file;
            const auto after = value(vynx::hash_archive_file(source.toUtf8().toStdString(), **op));
            if (before != after)
                return QStringLiteral("CHANGED: Source changed while preparing the edit. Working "
                                      "file retained for export.");
            const auto baseHash = value(vynx::hash_archive_file(file.toUtf8().toStdString(), **op));
            if (!QFile::setPermissions(file, QFile::permissions(file) | QFileDevice::WriteOwner))
                return QStringLiteral("IO: Could not make the working copy writable");
            (*manifest)["baseHash"] = baseHash;
            (*manifest)["fingerprint"] = EditStore::fingerprint(file);
            (*manifest)["state"] = "ready";
            (*manifest)["lastVerified"] =
                QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
            if (!EditStore::write(directory, *manifest))
                return QStringLiteral("IO: Cannot finalize edit recovery metadata; copy retained");
            return QString();
        },
        [this, key, launch] {
            const auto record = edits_->record(key);
            if (launch && edits_->validPayload(record) &&
                !QDesktopServices::openUrl(QUrl::fromLocalFile(record["file"].toString())))
                QMessageBox::warning(this, tr("Edit working copy"),
                                     tr("Windows could not launch an editor. The copy is available "
                                        "in View > Working copies."));
            toast_->notify(tr("Working copy created. Save changes from View > Working copies."));
        });
}
void MainWindow::saveWorkingCopy(const QString &id, const std::shared_ptr<SecretUtf8> &pw) {
    auto record = edits_->record(id);
    if (!edits_->validPayload(record) || record["archiveHash"].toString().isEmpty())
        return;
    const auto source = record["archive"].toString();
    QJsonObject spec{{"kind", "edit"},
                     {"sources", QJsonArray{source}},
                     {"editId", id},
                     {"needsPassword", !pw->bytes().empty()}};
    enqueueRequest(
        tr("Save %1 in archive").arg(record["entry"].toString()),
        [this, id, pw, source, spec] {
            auto current = edits_->record(id);
            if (!edits_->validPayload(current))
                return;
            for (int i = 0; i < sessions_.size(); ++i)
                if (archiveIdentity(sessions_[i]->path) == archiveIdentity(source)) {
                    bindSession(sessions_[i]);
                    break;
                }
            beginOperation();
            activeSpec_ = spec;
            auto op = operations_.operation;
            auto saved = std::make_shared<QJsonObject>(current);
            runJob(
                tr("Saving working copy..."),
                [current, pw, op, saved] {
                    const auto file = current["file"].toString();
                    const auto source = current["archive"].toString();
                    const auto inputHash =
                        value(vynx::hash_archive_file(file.toUtf8().toStdString(), **op));
                    const auto fingerprint = EditStore::fingerprint(file);
                    auto archive =
                        vynx::open_archive(source.toUtf8().toStdString(), pw->bytes(), **op);
                    const auto receipt = value(vynx::replace_entry(
                        *archive, current["entry"].toString().toUtf8().toStdString(),
                        file.toUtf8().toStdString(),
                        current["archiveHash"].toString().toUtf8().toStdString(),
                        inputHash.toUtf8().toStdString(), pw->bytes(), **op));
                    (*saved)["archiveHash"] = receipt;
                    (*saved)["baseHash"] = inputHash;
                    (*saved)["fingerprint"] = fingerprint;
                    (*saved)["state"] = "saved";
                    (*saved)["lastVerified"] =
                        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
                    (*saved)["error"] = QString();
                    return QString();
                },
                [this, id, saved, pw, source] {
                    if (!edits_->update(id, *saved))
                        QMessageBox::warning(
                            this, tr("Working copies"),
                            tr("The archive was saved, but recovery metadata could not be updated. "
                               "Keep the copy and reload its source before another save."));
                    QTimer::singleShot(0, this, [this, source, pw] { reloadArchive(source, pw); });
                },
                [this, id](QString error) {
                    edits_->update(id, QJsonObject{{"state", "needs-review"}, {"error", error}});
                    const bool unknown = error.startsWith("RESULT_UNKNOWN:");
                    QMessageBox message(
                        QMessageBox::Warning,
                        unknown ? tr("Save result requires verification")
                                : tr("Changes were not saved"),
                        unknown ? tr("The archive may have been saved. Keep the working copy and "
                                     "use Verify result in the queue before retrying.")
                                : tr("The working copy is retained. Reload the source or export "
                                     "the file before retrying."),
                        QMessageBox::Close, this);
                    message.setDetailedText(error);
                    message.exec();
                });
            activateSession(tabs_->currentIndex());
        },
        source + "\n" + record["entry"].toString(), spec);
}
void MainWindow::reloadArchive(const QString &path, const std::shared_ptr<SecretUtf8> &pw) {
    if (operations_.busy) {
        enqueueRequest(
            tr("Reload archive"), [this, path, pw] { reloadArchive(path, pw); }, path,
            QJsonObject{{"kind", "open"}, {"sources", QJsonArray{path}}});
        return;
    }
    std::shared_ptr<ArchiveSession> owner;
    for (const auto &candidate : sessions_)
        if (archiveIdentity(candidate->path) == archiveIdentity(path)) {
            owner = candidate;
            break;
        }
    if (!owner)
        return;
    bindSession(owner);
    const auto folder = owner->model->folder();
    const auto query = search_->text();
    auto archive = std::make_shared<std::shared_ptr<rust::Box<vynx::Archive>>>();
    auto prepared = std::make_shared<std::shared_ptr<PreparedArchive>>();
    beginOperation();
    auto op = operations_.operation;
    runJob(
        tr("Reloading archive..."),
        [archive, prepared, path, pw, op] {
            *archive = std::make_shared<rust::Box<vynx::Archive>>(
                vynx::open_archive(path.toUtf8().toStdString(), pw->bytes(), **op));
            *prepared = ArchiveModel::prepare(vynx::list_entries(***archive));
            return QString();
        },
        [this, owner, archive, prepared, pw, folder, query] {
            owner->previewCache.clear();
            owner->archive = *archive;
            owner->password = pw;
            owner->model->loadPrepared(*prepared);
            owner->model->navigate(folder);
            owner->model->setSearchMode(!query.isEmpty());
            search_->setText(query);
            updateSessionUi();
        });
    activateSession(tabs_->currentIndex());
}
void MainWindow::reloadWorkingSource(const QString &id, const std::shared_ptr<SecretUtf8> &pw) {
    auto record = edits_->record(id);
    if (operations_.busy || !edits_->validPayload(record))
        return;
    beginOperation();
    auto op = operations_.operation;
    auto digest = std::make_shared<QString>();
    runJob(
        tr("Checking edit source..."),
        [record, pw, op, digest] {
            const auto path = record["archive"].toString().toUtf8().toStdString();
            const auto before = value(vynx::hash_archive_file(path, **op));
            auto archive = vynx::open_archive(path, pw->bytes(), **op);
            const auto entries = vynx::list_entries(*archive);
            rust::Vec<quint64> ids;
            for (const auto &entry : entries)
                if (value(entry.name) == record["entry"].toString())
                    ids.push_back(entry.id);
            if (ids.size() != 1)
                return QStringLiteral("CHANGED: Original edited entry no longer exists");
            const auto hashes =
                vynx::hash_entries(*archive, {ids.data(), ids.size()}, pw->bytes(), **op);
            if (hashes.size() != 1 ||
                QString("SHA-256: %1\nCRC32: %2")
                        .arg(value(hashes[0].sha256), value(hashes[0].crc32)) !=
                    record["baseHash"].toString())
                return QStringLiteral("CHANGED: Original entry content changed. Export the working "
                                      "copy or create a new edit; no merge was performed.");
            *digest = value(vynx::hash_archive_file(path, **op));
            if (*digest != before)
                return QStringLiteral("CHANGED: Archive changed while checking");
            return QString();
        },
        [this, id, digest] {
            edits_->update(
                id,
                QJsonObject{{"archiveHash", *digest}, {"state", "ready"}, {"error", QString()}});
            toast_->notify(tr("Source reloaded. Review the working copy before saving."));
        });
}
void MainWindow::showWorkingCopies() {
    if (!edits_->available()) {
        QMessageBox::information(this, tr("Working copies"), edits_->error());
        return;
    }
    if (workingDock_) {
        workingDock_->show();
        workingDock_->raise();
        return;
    }
    workingDock_ = new QDockWidget(tr("Working copies"), this);
    workingDock_->setObjectName("workingCopiesDock");
    workingDock_->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::RightDockWidgetArea);
    auto *panel = new QWidget;
    auto &dialog = *panel;
    auto *frame = new QWidget;
    auto *frameLayout = new QVBoxLayout(frame);
    frameLayout->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setMinimumSize(0, 0);
    panel->setMinimumHeight(180);
    scroll->setWidget(panel);
    frameLayout->addWidget(scroll, 1);
    workingDock_->setWidget(frame);
    addDockWidget(Qt::BottomDockWidgetArea, workingDock_);
    auto *layout = new QVBoxLayout(&dialog);
    auto *notice =
        new QLabel(tr("Working copies contain plaintext and source paths until discarded. Saving "
                      "is explicit; external applications may keep their own copies."));
    notice->setWordWrap(true);
    layout->addWidget(notice);
    auto *usage = new QLabel;
    usage->setWordWrap(true);
    usage->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    usage->setToolTip(edits_->root());
    layout->addWidget(usage);
    auto *table = new QTableWidget(0, 6);
    table->setObjectName("workingCopiesTable");
    table->setAccessibleName(tr("Working copies"));
    table->setHorizontalHeaderLabels(
        {tr("Archive"), tr("Entry"), tr("Size"), tr("State"), tr("Last verified"), tr("Error")});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table, 1);
    auto *detail = new QPlainTextEdit;
    detail->setReadOnly(true);
    detail->setMaximumHeight(90);
    detail->setAccessibleName(tr("Working copy details"));
    layout->addWidget(detail);
    auto selected = [table] {
        return table->currentRow() < 0
                   ? QString()
                   : table->item(table->currentRow(), 0)->data(Qt::UserRole).toString();
    };
    auto showDetails = [this, selected, detail] {
        const auto record = edits_->record(selected());
        detail->setPlainText(record["file"].toString() + "\n" + record["error"].toString() + "\n" +
                             tr("Last verified: %1").arg(record["lastVerified"].toString()));
    };
    connect(table, &QTableWidget::currentCellChanged, &dialog, [showDetails] { showDetails(); });
    auto refresh = [this, table, selected, usage, showDetails] {
        usage->setText(tr("Working storage: %1 / %2. Existing edits are retained when the budget "
                          "is exceeded.\n%3")
                           .arg(QLocale().formattedDataSize(qint64(edits_->usedBytes())),
                                QLocale().formattedDataSize(qint64(edits_->quota())),
                                edits_->root()));
        const auto current = selected();
        QSignalBlocker blocker(table);
        const auto records = edits_->records();
        table->setRowCount(0);
        for (const auto &value : records) {
            const auto row = value.toObject();
            int index = table->rowCount();
            table->insertRow(index);
            auto *archive = new QTableWidgetItem(QFileInfo(row["archive"].toString()).fileName());
            archive->setToolTip(row["archive"].toString());
            archive->setData(Qt::UserRole, row["id"].toString());
            table->setItem(index, 0, archive);
            table->setItem(index, 1, new QTableWidgetItem(row["entry"].toString()));
            const bool changed =
                EditStore::fingerprint(row["file"].toString()) != row["fingerprint"].toString();
            auto *state = new QTableWidgetItem(
                row["state"] == "needs-review" ? tr("Review required")
                : row["state"] == "preparing"
                    ? tr("Preparation interrupted / export available copy")
                : (changed || row["state"] == "changed") ? tr("Changed / verify before saving")
                : row["state"] == "saved"                ? tr("Saved")
                                                         : tr("Working copy"));
            state->setToolTip(row["file"].toString() + "\n" + row["error"].toString());
            table->setItem(index, 2,
                           new QTableWidgetItem(QLocale().formattedDataSize(
                               QFileInfo(row["file"].toString()).size())));
            table->setItem(index, 3, state);
            table->setItem(index, 4, new QTableWidgetItem(row["lastVerified"].toString()));
            auto *error = new QTableWidgetItem(row["error"].toString());
            error->setToolTip(row["error"].toString());
            table->setItem(index, 5, error);
            if (row["id"] == current)
                table->selectRow(index);
        }
        if (table->currentRow() < 0 && table->rowCount())
            table->selectRow(0);
        showDetails();
    };
    auto *controls = new QToolBar;
    controls->setObjectName("workingCopyControls");
    controls->setToolButtonStyle(Qt::ToolButtonTextOnly);
    controls->setStyleSheet("QToolButton#qt_toolbar_ext_button {padding:0px;border:none;}");
    auto *open = controls->addAction(tr("Open copy"));
    auto *save = controls->addAction(tr("Save in archive..."));
    auto *saveAs = controls->addAction(tr("Save as new archive..."));
    auto *verifyCopy = controls->addAction(tr("Verify copy"));
    auto *folder = controls->addAction(tr("Open working folder"));
    auto *reload = controls->addAction(tr("Reload source..."));
    auto *exportFile = controls->addAction(tr("Export file..."));
    auto *discard = controls->addAction(tr("Discard..."));
    frameLayout->addWidget(controls);
    auto password = [this](const QJsonObject &record, bool &ok) {
        for (const auto &session : sessions_)
            if (archiveIdentity(session->path) == archiveIdentity(record["archive"].toString()) &&
                session->password) {
                ok = true;
                return session->password;
            }
        return secret(askPassword(&ok));
    };
    connect(open, &QAction::triggered, &dialog, [this, selected] {
        const auto record = edits_->record(selected());
        if (edits_->validPayload(record) && ExternalFiles::allowed(record["file"].toString()))
            QDesktopServices::openUrl(QUrl::fromLocalFile(record["file"].toString()));
    });
    connect(verifyCopy, &QAction::triggered, &dialog, [this, selected] {
        if (operations_.busy)
            return;
        const auto id = selected();
        const auto record = edits_->record(id);
        if (!edits_->validPayload(record))
            return;
        beginOperation();
        activeSpec_ = QJsonObject{
            {"kind", "edit-check"}, {"editId", id}, {"sources", QJsonArray{record["file"]}}};
        auto op = operations_.operation;
        auto result = std::make_shared<QString>();
        auto checked = std::make_shared<QString>();
        runJob(
            tr("Verifying working copy..."),
            [record, op, result, checked] {
                const auto path = record["file"].toString();
                const auto before = EditStore::fingerprint(path);
                *result = value(vynx::hash_archive_file(path.toUtf8().toStdString(), **op));
                if (EditStore::fingerprint(path) != before)
                    return QStringLiteral(
                        "CHANGED: Editor changed the file during verification; copy retained");
                *checked = before;
                return QString();
            },
            [this, id, record, result, checked] {
                edits_->update(
                    id, QJsonObject{{"state", *result == record["baseHash"].toString() ? "ready"
                                                                                       : "changed"},
                                    {"lastVerified",
                                     QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                                    {"fingerprint", *checked},
                                    {"error", QString()}});
            },
            [this, id](QString error) {
                edits_->update(id, QJsonObject{{"state", "needs-review"}, {"error", error}});
            });
    });
    connect(saveAs, &QAction::triggered, &dialog, [this, selected] {
        const auto id = selected();
        const auto record = edits_->record(id);
        if (edits_->validPayload(record))
            chooseConversion({record["archive"].toString()}, id);
    });
    connect(folder, &QAction::triggered, &dialog, [this, selected] {
        const auto path = edits_->directory(selected());
        if (!path.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    connect(save, &QAction::triggered, &dialog, [this, selected, password] {
        const auto id = selected();
        const auto record = edits_->record(id);
        bool ok = false;
        auto pw = password(record, ok);
        if (ok)
            saveWorkingCopy(id, pw);
    });
    connect(reload, &QAction::triggered, &dialog, [this, selected, password] {
        if (operations_.busy)
            return;
        const auto id = selected();
        bool ok = false;
        auto pw = password(edits_->record(id), ok);
        if (ok)
            reloadWorkingSource(id, pw);
    });
    connect(discard, &QAction::triggered, &dialog, [this, selected, refresh] {
        if (operations_.busy)
            return;
        const auto id = selected();
        if (id.isEmpty())
            return;
        if (QMessageBox::question(
                this, tr("Discard working copy"),
                tr("Delete this working copy and its unsaved changes? The archive is not changed."),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        if (!edits_->discard(id))
            QMessageBox::warning(this, tr("Working copies"),
                                 tr("The copy is locked or contains unexpected files. Close the "
                                    "editor and try again."));
        refresh();
    });
    connect(exportFile, &QAction::triggered, &dialog, [this, selected] {
        if (operations_.busy)
            return;
        const auto record = edits_->record(selected());
        if (!edits_->validPayload(record))
            return;
        const auto path = QFileDialog::getSaveFileName(
            this, tr("Export working copy"), QFileInfo(record["file"].toString()).fileName());
        if (path.isEmpty())
            return;
        beginOperation();
        auto op = operations_.operation;
        runJob(tr("Exporting working copy..."), [record, path, op] {
            const auto source = record["file"].toString();
            const auto expected =
                value(vynx::hash_archive_file(source.toUtf8().toStdString(), **op))
                    .section('\n', 0, 0)
                    .section(' ', 1, 1);
            QFile input(source);
            QSaveFile output(path);
            if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly))
                return QStringLiteral("IO: Cannot open export file");
            QCryptographicHash hash(QCryptographicHash::Sha256);
            while (!input.atEnd()) {
                if (vynx::cancelled(**op))
                    return QStringLiteral("CANCELLED: Export cancelled");
                const auto bytes = input.read(128 * 1024);
                if (bytes.isEmpty() && input.error() != QFileDevice::NoError)
                    return input.errorString();
                hash.addData(bytes);
                if (output.write(bytes) != bytes.size())
                    return output.errorString();
            }
            if (QString::fromLatin1(hash.result().toHex()) != expected)
                return QStringLiteral(
                    "CHANGED: Working copy changed during export; destination preserved");
            if (vynx::cancelled(**op))
                return QStringLiteral("CANCELLED: Export cancelled");
            return output.commit() ? QString() : output.errorString();
        });
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, workingDock_, &QDockWidget::hide);
    frameLayout->addWidget(buttons);
    auto *timer = new QTimer(panel);
    connect(timer, &QTimer::timeout, &dialog, refresh);
    timer->start(1000);
    refresh();
    workingDock_->show();
    workingDock_->raise();
    QTimer::singleShot(0, this, [this] {
        resizeDocks({workingDock_}, {qBound(180, height() * 3 / 5, 440)}, Qt::Vertical);
    });
}

void MainWindow::showTemporaryFiles() {
    QSet<QString> activeNested;
    for (const auto &s : sessions_) {
        if (s->nestedInfo && !s->nestedInfo->nestedStoreId.isEmpty())
            activeNested.insert(s->nestedInfo->nestedStoreId);
    }
    // A child retains every ancestor container needed for navigation, even if
    // its parent's tab was closed.
    if (nestedStore_) {
        const auto records = nestedStore_->records();
        bool expanded = true;
        while (expanded) {
            expanded = false;
            for (const auto &child : records) {
                const auto rec = child.toObject();
                if (!activeNested.contains(rec["id"].toString())) continue;
                for (const auto &parent : records) {
                    const auto ancestor = parent.toObject();
                    const auto id = ancestor["id"].toString();
                    if (!activeNested.contains(id) && ancestor["file"] == rec["parentArchive"]) {
                        activeNested.insert(id); expanded = true;
                    }
                }
            }
        }
    }
    TemporaryStorageDialog dialog(tempStore_.get(), activeNested, this);
    dialog.exec();
}
