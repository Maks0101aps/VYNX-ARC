#include "NestedStore.h"
#include "MainWindow.h"
#include "pages/ArchivePage.h"
#include "widgets/BreadcrumbBar.h"
#include <QtWidgets>
QString MainWindow::archiveIdentity(const QString &path) const {
    QString candidate = QFileInfo(path).absoluteFilePath();
    QRegularExpression split("(\\.7z\\.)[0-9]{3}$", QRegularExpression::CaseInsensitiveOption);
    const auto match = split.match(candidate);
    if (match.hasMatch())
        candidate.replace(match.capturedStart(), match.capturedLength(), match.captured(1) + "001");
    const auto canonical = QFileInfo(candidate).canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? candidate : canonical).toCaseFolded();
}
void MainWindow::bindSession(const std::shared_ptr<ArchiveSession> &next) {
    next->model->setArchiveSource(next->path, next->id);
    if (session_ == next && table_->model() == next->filter)
        return;
    if (session_ && table_->model() == session_->filter) {
        session_->search = search_->text();
        session_->scroll = table_->verticalScrollBar()->value();
        session_->headerState = table_->horizontalHeader()->saveState();
        session_->selection.clear();
        for (const auto &index : table_->selectionModel()->selectedRows())
            session_->selection.insert(
                session_->model->row(session_->filter->mapToSource(index).row()).fullPath);
    }
    session_ = next;
    QSignalBlocker blocker(search_);
    search_->setText(session_->search);
    table_->setModel(session_->filter);
    // Qt also replaces the headers' selection models. Retain every model still
    // used by a child view, and release unshared models left by setModel.
    QSet<QItemSelectionModel *> used{table_->selectionModel()};
    for (auto *view : table_->findChildren<QAbstractItemView *>())
        used.insert(view->selectionModel());
    for (auto *selection : table_->findChildren<QItemSelectionModel *>())
        if (!used.contains(selection))
            delete selection;
    connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this] { updateStatus(); });
    if (!session_->headerState.isEmpty())
        table_->horizontalHeader()->restoreState(session_->headerState);
    table_->setColumnHidden(7, session_->search.isEmpty());
    for (int i = 0; !session_->selection.isEmpty() && i < session_->filter->rowCount(); ++i) {
        auto index = session_->filter->index(i, 0);
        if (session_->selection.contains(
                session_->model->row(session_->filter->mapToSource(index).row()).fullPath))
            table_->selectionModel()->select(index, QItemSelectionModel::Select |
                                                        QItemSelectionModel::Rows);
    }
    table_->verticalScrollBar()->setValue(session_->scroll);
}
void MainWindow::updateSessionUi() {
    archivePage_->setEnabled(!session_->busy);
    pages_->setCurrentIndex(session_->archive ? 1 : 0);
    for (auto *action : archiveActions_)
        action->setEnabled(bool(session_->archive) && !operations_.busy);
    archivePage_->setWritable(writableArchive());
    const auto title =
        session_->path.isEmpty() ? tr("VYNX ARC") : QFileInfo(session_->path).fileName();
    setWindowTitle(title + (session_->path.isEmpty() ? QString() : " — VYNX ARC"));
    QStringList parentChain;
    if (session_->nestedInfo) {
        parentChain.append(QFileInfo(session_->nestedInfo->parentArchivePath).fileName());
    }
    breadcrumb_->setPath(QFileInfo(session_->path).fileName(), session_->model->folder(), parentChain);
    if (session_->nestedInfo) {
        QFileInfo parentFi(session_->nestedInfo->parentArchivePath);
        const QString curFp = QString("%1:%2").arg(parentFi.size()).arg(parentFi.lastModified().toMSecsSinceEpoch());
        if (!session_->nestedInfo->parentFingerprint.isEmpty() && curFp != session_->nestedInfo->parentFingerprint) {
            statusBar()->showMessage(tr("Warning: Parent archive was modified; opened nested copy is stale."), 10000);
        }
    }
    if (session_->archive)
        updateStatus();
    else {
        archiveSummary_->clear();
        statusBar()->clearMessage();
    }
}
void MainWindow::activateSession(int index) {
    if (index < 0 || index >= sessions_.size())
        return;
    if (tabs_->currentIndex() != index) {
        const QSignalBlocker blocker(tabs_);
        tabs_->setCurrentIndex(index);
    }
    bindSession(sessions_[index]);
    updateSessionUi();
}
void MainWindow::closeSession(int index) {
    if (index < 0 || index >= sessions_.size())
        return;
    auto closing = sessions_[index];
    if (closing->busy) {
        if (QMessageBox::question(
                this, tr("Operation in progress"),
                tr("Cancel this archive's operation and close its tab after it stops?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
            closing->closeWhenIdle = true;
            if (operations_.owner == closing && operations_.operation)
                vynx::cancel(**operations_.operation);
            for (const auto &reader : readers_)
                if (reader->owner == closing && reader->operation)
                    vynx::cancel(**reader->operation);
        }
        return;
    }
    const QSignalBlocker blocker(tabs_);
    if (closing->nestedInfo && !closing->nestedInfo->nestedStoreId.isEmpty() && nestedStore_) {
        bool hasChild = false;
        for (const auto &candidate : sessions_)
            if (candidate->nestedInfo && candidate->nestedInfo->parentSessionId == closing->id)
                hasChild = true;
        if (!hasChild) nestedStore_->discard(closing->nestedInfo->nestedStoreId);
    }
    sessions_.removeAt(index);
    tabs_->removeTab(index);
    if (sessions_.isEmpty()) {
        sessions_.append(std::make_shared<ArchiveSession>());
        tabs_->addTab(tr("New archive"));
        tabs_->hide();
    }
    activateSession(qBound(0, tabs_->currentIndex(), sessions_.size() - 1));
    saveWorkspace();
}
void MainWindow::saveWorkspace() {
    if (QCoreApplication::arguments().contains("--smoke-test") ||
        QCoreApplication::arguments().contains("--capture"))
        return;
    if (!settings_->value("historyEnabled", true).toBool()) {
        settings_->remove("workspace");
        return;
    }
    QStringList paths;
    for (const auto &session : sessions_)
        if (!session->path.isEmpty() && paths.size() < 32)
            paths << session->path;
    settings_->setValue("workspace", paths);
}
void MainWindow::restoreWorkspace() {
    if (!settings_->value("historyEnabled", true).toBool() ||
        QCoreApplication::arguments().size() != 1)
        return;
    const auto paths = settings_->value("workspace").toStringList().mid(0, 32);
    if (paths.isEmpty())
        return;
    if (QMessageBox::question(
            this, tr("Restore workspace"),
            tr("Restore %1 previously open archives? Passwords will be requested again.")
                .arg(paths.size()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    for (const auto &path : paths)
        openPath(path);
}
