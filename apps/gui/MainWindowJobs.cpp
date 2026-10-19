#include "MainWindow.h"
#include "dialogs/ConflictDialog.h"
#include "pages/ArchivePage.h"
#include "widgets/OperationPanel.h"
#include "widgets/ToastOverlay.h"
#include <QtConcurrent>
#include <QtWidgets>

static QString text(const rust::String &s) {
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}

void MainWindow::setupOperations(QVBoxLayout *layout, QMenu *viewMenu) {
    operationView_ = new OperationPanel;
    operationPanel_ = operationView_;
    current_ = operationView_->current;
    rate_ = operationView_->rate;
    progress_ = operationView_->progress;
    cancel_ = operationView_->cancel;
    layout->addWidget(operationPanel_);
    queueDock_ = new QDockWidget(tr("Operation queue"), this);
    queueDock_->setObjectName("operationQueueDock");
    queueDock_->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::RightDockWidgetArea);
    auto *queuePanel = new QWidget;
    auto *queueLayout = new QVBoxLayout(queuePanel);
    queueView_ = new QListWidget;
    queueView_->setObjectName("operationQueue");
    queueView_->setAccessibleName(tr("Operation queue"));
    queueView_->setMinimumHeight(100);
    queueLayout->addWidget(queueView_);
    queueSummary_ = new QLabel;
    queueSummary_->setWordWrap(true);
    queueLayout->addWidget(queueSummary_);
    queueFilter_ = new QComboBox;
    queueFilter_->setObjectName("queueFilter");
    queueFilter_->setAccessibleName(tr("Filter operations"));
    queueFilter_->addItems({tr("All operations"), tr("Pending"), tr("Running"), tr("Errors"),
                            tr("Needs review"), tr("Finished")});
    connect(queueFilter_, &QComboBox::currentIndexChanged, this, [this] { updateQueue(); });
    auto *controls = new QToolBar;
    controls->setObjectName("queueControls");
    controls->setToolButtonStyle(Qt::ToolButtonTextOnly);
    controls->setMovable(false);
    // The global button padding otherwise clips Qt's narrow overflow indicator.
    controls->setStyleSheet("QToolButton#qt_toolbar_ext_button {padding:0px;border:none;}");
    auto *addArchives = controls->addAction(tr("Add archives…"));
    connect(addArchives, &QAction::triggered, this, &MainWindow::chooseOpen);
    cancelPending_ = controls->addAction(tr("Cancel pending"));
    cancelPending_->setToolTip(tr("Cancel selected pending operation"));
    cancelPending_->setEnabled(false);
    auto *pause = controls->addAction(tr("Pause queue"));
    pause->setObjectName("pauseQueue");
    pause->setCheckable(true);
    connect(pause, &QAction::toggled, this, [this, pause](bool paused) {
        queuePaused_ = paused;
        settings_->setValue("queuePaused", paused);
        pause->setText(paused ? tr("Resume queue") : tr("Pause queue"));
        if (!paused)
            QTimer::singleShot(0, this, &MainWindow::startNextQueued);
    });
    if (!QCoreApplication::arguments().contains("--smoke-test") &&
        !QCoreApplication::arguments().contains("--capture"))
        pause->setChecked(settings_->value("queuePaused", false).toBool());
    auto *up = controls->addAction(tr("Move up"));
    auto *down = controls->addAction(tr("Move down"));
    connect(up, &QAction::triggered, this, [this] { movePending(-1); });
    connect(down, &QAction::triggered, this, [this] { movePending(1); });
    auto *cancelAll = controls->addAction(tr("Cancel all pending"));
    connect(cancelAll, &QAction::triggered, this, [this] {
        queue_.clear();
        for (int i = 0; i < queueView_->count(); ++i) {
            auto *item = queueView_->item(i);
            if (item != runningItem_ && !item->data(Qt::UserRole + 2).toBool()) {
                item->setText(tr("Cancelled — %1").arg(item->data(Qt::UserRole + 1).toString()));
                item->setData(Qt::UserRole + 2, true);
                item->setData(Qt::UserRole + 5, "cancelled");
            }
        }
        updateQueue();
    });
    auto *openOutput = controls->addAction(tr("Open folder"));
    connect(openOutput, &QAction::triggered, this, [this] {
        const auto *item = queueView_->currentItem();
        if (!item)
            return;
        const auto spec = item->data(Qt::UserRole + 4).toJsonObject();
        const QFileInfo output(spec["destination"].toString());
        const auto folder =
            spec["kind"] == "create" ? output.absolutePath() : output.absoluteFilePath();
        if (!spec["destination"].toString().isEmpty() && QDir(folder).exists())
            QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
    });
    auto *clearFinished = controls->addAction(tr("Clear completed"));
    auto *retryBatch = controls->addAction(tr("Retry failed batch jobs..."));
    connect(retryBatch, &QAction::triggered, this, &MainWindow::retryFailedBatch);
    auto *verifyResult = controls->addAction(tr("Verify result"));
    connect(verifyResult, &QAction::triggered, this, &MainWindow::verifySelectedResult);
    auto *retry = controls->addAction(tr("Review / retry..."));
    connect(retry, &QAction::triggered, this, &MainWindow::retrySelectedJob);
    auto *report = controls->addAction(tr("Export report..."));
    connect(report, &QAction::triggered, this, &MainWindow::exportJobReport);
    auto *queueFrame = new QWidget;
    auto *frameLayout = new QVBoxLayout(queueFrame);
    frameLayout->setContentsMargins(0, 0, 0, 0);
    auto *queueScroll = new QScrollArea;
    queueScroll->setWidgetResizable(true);
    queueScroll->setFrameShape(QFrame::NoFrame);
    queueScroll->setMinimumSize(0, 0);
    queuePanel->setMinimumHeight(140);
    queueScroll->setWidget(queuePanel);
    frameLayout->addWidget(queueScroll, 1);
    frameLayout->addWidget(queueFilter_);
    frameLayout->addWidget(controls);
    queueDock_->setWidget(queueFrame);
    addDockWidget(Qt::BottomDockWidgetArea, queueDock_);
    queueDock_->hide();
    viewMenu->addAction(queueDock_->toggleViewAction());
    connect(queueView_, &QListWidget::currentItemChanged, this, [this] { updateQueue(); });
    connect(queueView_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Operation details"));
        auto *layout = new QVBoxLayout(&dialog);
        auto *details = new QPlainTextEdit(item->text() + "\n\n" + item->toolTip());
        details->setReadOnly(true);
        layout->addWidget(details);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(buttons);
        dialog.resize(720, 360);
        dialog.exec();
    });
    connect(cancelPending_, &QAction::triggered, this, [this] {
        auto *item = queueView_->currentItem();
        if (item && queue_.cancel(item->data(Qt::UserRole).toULongLong())) {
            item->setText(tr("Cancelled — %1").arg(item->data(Qt::UserRole + 1).toString()));
            item->setData(Qt::UserRole + 2, true);
            item->setData(Qt::UserRole + 5, "cancelled");
            updateQueue();
        }
    });
    connect(clearFinished, &QAction::triggered, this, [this] {
        QSignalBlocker blocker(queueView_);
        for (int i = queueView_->count() - 1; i >= 0; --i)
            if (queueView_->item(i)->data(Qt::UserRole + 2).toBool() &&
                !QStringList{"interrupted", "unknown", "verifying"}.contains(
                    queueView_->item(i)->data(Qt::UserRole + 5).toString())) {
                journal_.remove(queueView_->item(i)->data(Qt::UserRole + 3).toString());
                delete queueView_->takeItem(i);
            }
        updateQueue();
    });
    connect(cancel_, &QPushButton::clicked, this, [this] {
        if (operations_.operation) {
            vynx::cancel(**operations_.operation);
            cancel_->setEnabled(false);
            current_->setText(tr("Cancelling…"));
        }
    });
    timer_ = new QTimer(this);
    timer_->setInterval(100);
    connect(timer_, &QTimer::timeout, this, [this] {
        if (!operations_.operation)
            return;
        auto p = vynx::progress(**operations_.operation);
        operationView_->updateProgress(p.done, p.total, text(p.current), elapsed_.elapsed(),
                                       text(p.phase), text(p.details), p.cancelled);
        const auto publication = vynx::publication_id(**operations_.operation);
        if (publication && publication != shownPublication_) {
            shownPublication_ = publication;
            QJsonArray targets;
            for (const auto &t : vynx::publication_targets(**operations_.operation))
                targets.append(QJsonObject{{"path", text(t.path)},
                                           {"sha256", text(t.sha256)},
                                           {"before", text(t.before)}});
            const bool recorded =
                runningItem_ &&
                journal_.recordIntent(runningItem_->data(Qt::UserRole + 3).toString(), targets,
                                      text(vynx::operation_input_hash(**operations_.operation)));
            vynx::reply_publication(**operations_.operation, publication, recorded);
        }
        showConflict();
    });
    connect(&operations_.watcher(), &QFutureWatcher<QString>::finished, this, [this] {
        timer_->stop();
        operations_.busy = false;
        const auto owner = operations_.owner;
        if (owner)
            owner->busy = false;
        if (owner)
            bindSession(owner);
        operationPanel_->hide();
        archivePage_->setEnabled(true);
        for (auto *a : idleActions_)
            a->setEnabled(true);
        for (auto *a : archiveActions_)
            a->setEnabled(bool(session_->archive));
        if (session_->archive)
            updateStatus();
        QString error = operations_.watcher().result();
        if (runningItem_ && !error.isEmpty() && !error.startsWith("RESULT_UNKNOWN:") &&
            !journal_.record(runningItem_->data(Qt::UserRole + 3).toString())["publication"]
                 .toArray()
                 .isEmpty())
            error = "RESULT_UNKNOWN: Publication was prepared. Verify the destination before "
                    "retrying.\n" +
                    error;

        if (runningItem_) {
            if (!error.isEmpty())
                runningItem_->setToolTip(runningItem_->toolTip() + "\n" + error);
            const auto status = error.isEmpty()                       ? tr("Completed — %1")
                                : error.startsWith("RESULT_UNKNOWN:") ? tr("Result unknown — %1")
                                : error.startsWith("CANCELLED:")      ? tr("Cancelled — %1")
                                                                      : tr("Stopped — %1");
            runningItem_->setText(status.arg(runningItem_->data(Qt::UserRole + 1).toString()));
            runningItem_->setData(Qt::UserRole + 2, true);
            runningItem_->setData(Qt::UserRole + 5, error.isEmpty() ? "completed"
                                                    : error.startsWith("RESULT_UNKNOWN:")
                                                        ? "unknown"
                                                    : error.startsWith("CANCELLED:") ? "cancelled"
                                                                                     : "failed");
            runningItem_->setData(Qt::UserRole + 6, error);
            runningItem_ = nullptr;
            updateQueue();
        }
        auto success = std::move(success_);
        auto failure = std::move(failure_);
        operations_.operation.reset();
        operations_.owner.reset();
        if (closePending_) {
            QTimer::singleShot(0, this, [this] { close(); });
            return;
        }
        if (error.isEmpty()) {
            if (success)
                success();
            toast_->notify(tr("Operation completed"));
            updateStatus();
        } else if (failure) {
            failure(error);
        } else
            QMessageBox::warning(this, tr("Operation stopped"), error);
        activateSession(tabs_->currentIndex());
        saveWorkspace();
        if (owner && owner->closeWhenIdle)
            closeSession(sessions_.indexOf(owner));
        QTimer::singleShot(0, this, &MainWindow::startNextQueued);
    });
}
void MainWindow::beginOperation() {
    shownConflict_ = 0;
    shownPublication_ = 0;
    if (activeSpec_.isEmpty())
        activeSpec_ = QJsonObject{{"kind", "read"}, {"sources", QJsonArray{session_->path}}};
    operations_.operation = std::make_shared<rust::Box<vynx::Operation>>(vynx::new_operation());
    vynx::configure_operation(
        **operations_.operation, uint8_t(qBound(0, activeSpec_["preset"].toInt(2), 3)),
        uint8_t(qBound(
            0, activeSpec_["resource"].toInt(settings_->value("resourceMode", 1).toInt()), 2)));
}
void MainWindow::runJob(const QString &title, std::function<QString()> worker,
                        std::function<void()> success, std::function<void(QString)> failure) {
    if (operations_.busy)
        return;
    operations_.busy = true;
    operations_.owner.reset();
    const auto sources = activeSpec_["sources"].toArray();
    if (!sources.isEmpty() && activeSpec_["kind"] != "create") {
        const auto path = sources.first().toString();
        for (const auto &candidate : sessions_)
            if (!candidate->path.isEmpty() &&
                archiveIdentity(candidate->path) == archiveIdentity(path)) {
                operations_.owner = candidate;
                break;
            }
    }
    if (operations_.owner)
        operations_.owner->busy = true;
    if (!runningItem_) {
        runningItem_ = new QListWidgetItem(queueView_);
        runningItem_->setData(Qt::UserRole + 1, title);
    }
    if (!activeSpec_.isEmpty())
        runningItem_->setData(Qt::UserRole + 4, activeSpec_);
    if (!runningItem_->data(Qt::UserRole + 3).toString().isEmpty())
        journal_.updateSpec(runningItem_->data(Qt::UserRole + 3).toString(),
                            runningItem_->data(Qt::UserRole + 4).toJsonObject());
    activeSpec_ = {};
    runningItem_->setData(Qt::UserRole + 2, false);
    runningItem_->setData(Qt::UserRole + 5, "running");
    runningItem_->setText(tr("Running — %1").arg(runningItem_->data(Qt::UserRole + 1).toString()));
    updateQueue();
    success_ = std::move(success);
    failure_ = std::move(failure);
    archivePage_->setEnabled(!session_->busy);
    for (auto *a : idleActions_)
        a->setEnabled(false);
    for (auto *a : archiveActions_)
        a->setEnabled(false);
    if (session_->archive)
        updateStatus();
    operationView_->begin(title);
    cancel_->setEnabled(true);
    progress_->setRange(0, 0);
    elapsed_.start();
    timer_->start();
    vynx::control_publication(**operations_.operation, true);
    operations_.start(std::move(worker));
}
void MainWindow::updateQueue() {
    QSignalBlocker blocker(queueView_);
    for (int i = 0; i < queueView_->count(); ++i) {
        auto *row = queueView_->item(i);
        const auto state = row->data(Qt::UserRole + 5).toString();
        const int filter = queueFilter_->currentIndex();
        const bool visible =
            filter == 0 || (filter == 1 && state == "pending") ||
            (filter == 2 && state == "running") || (filter == 3 && state == "failed") ||
            (filter == 4 &&
             QStringList{"unknown", "verifying", "interrupted", "decision"}.contains(state)) ||
            (filter == 5 && QStringList{"completed", "cancelled"}.contains(state));
        row->setHidden(!visible);
    }
    if (queueView_->currentItem() && queueView_->currentItem()->isHidden()) {
        queueView_->clearSelection();
        queueView_->setCurrentItem(nullptr);
    }
    int completed = 0;
    for (int i = 0; i < queueView_->count(); ++i)
        completed += queueView_->item(i)->data(Qt::UserRole + 2).toBool() &&
                     !QStringList{"interrupted", "unknown", "verifying"}.contains(
                         queueView_->item(i)->data(Qt::UserRole + 5).toString());
    for (int i = 0; i < queueView_->count() && completed > 64;) {
        if (queueView_->item(i)->data(Qt::UserRole + 2).toBool() &&
            !QStringList{"interrupted", "unknown", "verifying"}.contains(
                queueView_->item(i)->data(Qt::UserRole + 5).toString())) {
            journal_.remove(queueView_->item(i)->data(Qt::UserRole + 3).toString());
            delete queueView_->takeItem(i);
            --completed;
        } else
            ++i;
    }
    auto *item = queueView_->currentItem();
    cancelPending_->setEnabled(item && item != runningItem_ &&
                               item->data(Qt::UserRole).toULongLong() != 0 &&
                               !item->data(Qt::UserRole + 2).toBool());
    syncJournal();
}

void MainWindow::startNextQueued() {
    if (operations_.busy || closePending_ || queuePaused_)
        return;
    const auto front = queue_.frontId();
    for (int i = 0; i < queueView_->count(); ++i) {
        auto *item = queueView_->item(i);
        if (item->data(Qt::UserRole).toULongLong() != front)
            continue;
        const auto sources = item->data(Qt::UserRole + 4).toJsonObject()["sources"].toArray();
        if (sources.isEmpty())
            break;
        for (const auto &candidate : sessions_)
            if (candidate->busy && !candidate->path.isEmpty() &&
                archiveIdentity(candidate->path) == archiveIdentity(sources.first().toString())) {
                QTimer::singleShot(100, this, &MainWindow::startNextQueued);
                return;
            }
        break;
    }
    auto next = queue_.take();
    if (!next)
        return;
    for (int i = 0; i < queueView_->count(); ++i)
        if (queueView_->item(i)->data(Qt::UserRole).toULongLong() == next->id) {
            runningItem_ = queueView_->item(i);
            break;
        }
    if (runningItem_)
        activeSpec_ = runningItem_->data(Qt::UserRole + 4).toJsonObject();
    next->start();
    if (!operations_.busy && runningItem_) {
        runningItem_->setText(tr("Stopped — %1").arg(next->title));
        runningItem_->setData(Qt::UserRole + 2, true);
        runningItem_->setData(Qt::UserRole + 5, "failed");
        runningItem_->setData(Qt::UserRole + 6,
                              tr("The request did not start. Review its parameters."));
        activeSpec_ = {};
        runningItem_ = nullptr;
        updateQueue();
        QTimer::singleShot(0, this, &MainWindow::startNextQueued);
    }
}
bool MainWindow::enqueueRequest(const QString &title, std::function<void()> start,
                                const QString &details, const QJsonObject &spec) {
    const auto id = queue_.enqueue(title, std::move(start));
    if (!id) {
        toast_->notify(tr("Operation queue is full"));
        return false;
    }
    auto *item = new QListWidgetItem(tr("Pending — %1").arg(title), queueView_);
    item->setData(Qt::UserRole, id);
    item->setData(Qt::UserRole + 1, title);
    item->setToolTip(details);
    item->setData(Qt::UserRole + 4, spec);
    item->setData(Qt::UserRole + 5, "pending");
    queueDock_->show();
    updateQueue();
    QTimer::singleShot(0, this, &MainWindow::startNextQueued);
    return true;
}
void MainWindow::movePending(int direction) {
    auto *item = queueView_->currentItem();
    if (!item || item == runningItem_ || item->data(Qt::UserRole + 2).toBool())
        return;
    const int row = queueView_->row(item);
    int target = row + direction;
    while (target >= 0 && target < queueView_->count()) {
        auto *candidate = queueView_->item(target);
        if (candidate != runningItem_ && !candidate->data(Qt::UserRole + 2).toBool())
            break;
        target += direction;
    }
    if (target < 0 || target >= queueView_->count() ||
        !queue_.move(item->data(Qt::UserRole).toULongLong(), direction))
        return;
    QSignalBlocker blocker(queueView_);
    queueView_->takeItem(row);
    queueView_->insertItem(target, item);
    queueView_->setCurrentItem(item);
    updateQueue();
}
void MainWindow::showConflict() {
    if (!operations_.operation)
        return;
    auto request = vynx::conflict_request(**operations_.operation);
    if (!request.id || request.id == shownConflict_)
        return;
    shownConflict_ = request.id;
    if (runningItem_) {
        runningItem_->setData(Qt::UserRole + 5, "decision");
        runningItem_->setText(tr("Review required") + " - " +
                              runningItem_->data(Qt::UserRole + 1).toString());
        updateQueue();
    }
    auto op = operations_.operation;
    auto *dialog = new ConflictDialog(text(request.existing_path), request.existing_size,
                                      request.existing_modified, text(request.incoming_name),
                                      request.incoming_size, request.incoming_modified, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::ApplicationModal);
    connect(dialog, &ConflictDialog::decision, dialog,
            [op, id = request.id](quint8 choice, bool all) {
                vynx::reply_conflict(**op, id, choice, all);
            });
    connect(dialog, &QDialog::finished, this, [this, op] {
        if (operations_.operation == op && runningItem_ &&
            runningItem_->data(Qt::UserRole + 5) == "decision") {
            runningItem_->setData(Qt::UserRole + 5, "running");
            runningItem_->setText(
                tr("Running — %1").arg(runningItem_->data(Qt::UserRole + 1).toString()));
            updateQueue();
        }
    });
    connect(dialog, &QDialog::finished, dialog,
            [op, id = request.id] { vynx::reply_conflict(**op, id, 0, false); });
    connect(&operations_.watcher(), &QFutureWatcher<QString>::finished, dialog, &QDialog::reject);
    auto *cancelTimer = new QTimer(dialog);
    connect(cancelTimer, &QTimer::timeout, dialog, [dialog, op] {
        if (vynx::cancelled(**op))
            dialog->reject();
    });
    cancelTimer->start(100);
    dialog->open();
}
