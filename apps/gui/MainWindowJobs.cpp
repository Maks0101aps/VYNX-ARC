#include "MainWindow.h"
#include "dialogs/ConflictDialog.h"
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
        pause->setText(paused ? tr("Resume queue") : tr("Pause queue"));
        if (!paused)
            QTimer::singleShot(0, this, &MainWindow::startNextQueued);
    });
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
            }
        }
        updateQueue();
    });
    auto *clearFinished = controls->addAction(tr("Clear completed"));
    queueLayout->addWidget(controls);
    queueDock_->setWidget(queuePanel);
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
            updateQueue();
        }
    });
    connect(clearFinished, &QAction::triggered, this, [this] {
        QSignalBlocker blocker(queueView_);
        for (int i = queueView_->count() - 1; i >= 0; --i)
            if (queueView_->item(i)->data(Qt::UserRole + 2).toBool())
                delete queueView_->takeItem(i);
        updateQueue();
    });
    connect(cancel_, &QPushButton::clicked, this, [this] {
        if (operation_) {
            vynx::cancel(**operation_);
            cancel_->setEnabled(false);
            current_->setText(tr("Cancelling…"));
        }
    });
    timer_ = new QTimer(this);
    timer_->setInterval(100);
    connect(timer_, &QTimer::timeout, this, [this] {
        if (!operation_)
            return;
        auto p = vynx::progress(**operation_);
        operationView_->updateProgress(p.done, p.total, text(p.current), elapsed_.elapsed(),
                                       text(p.phase), text(p.details), p.cancelled);
        showConflict();
    });
    connect(&watcher_, &QFutureWatcher<QString>::finished, this, [this] {
        timer_->stop();
        busy_ = false;
        operationPanel_->hide();
        pages_->setEnabled(true);
        for (auto *a : idleActions_)
            a->setEnabled(true);
        for (auto *a : archiveActions_)
            a->setEnabled(bool(archive_));
        if (archive_)
            updateStatus();
        QString error = watcher_.result();
        if (runningItem_) {
            if (!error.isEmpty())
                runningItem_->setToolTip(runningItem_->toolTip() + "\n" + error);
            const auto status = error.isEmpty()                  ? tr("Completed — %1")
                                : error.startsWith("CANCELLED:") ? tr("Cancelled — %1")
                                                                 : tr("Stopped — %1");
            runningItem_->setText(status.arg(runningItem_->data(Qt::UserRole + 1).toString()));
            runningItem_->setData(Qt::UserRole + 2, true);
            runningItem_ = nullptr;
            updateQueue();
        }
        auto success = std::move(success_);
        auto failure = std::move(failure_);
        operation_.reset();
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
        QTimer::singleShot(0, this, &MainWindow::startNextQueued);
    });
}
void MainWindow::beginOperation() {
    shownConflict_ = 0;
    operation_ = std::make_shared<rust::Box<vynx::Operation>>(vynx::new_operation());
    vynx::configure_operation(**operation_, 2,
                              uint8_t(qBound(0, settings_->value("resourceMode", 1).toInt(), 2)));
}
void MainWindow::runJob(const QString &title, std::function<QString()> worker,
                        std::function<void()> success, std::function<void(QString)> failure) {
    if (busy_)
        return;
    busy_ = true;
    if (!runningItem_) {
        runningItem_ = new QListWidgetItem(queueView_);
        runningItem_->setData(Qt::UserRole + 1, title);
    }
    runningItem_->setData(Qt::UserRole + 2, false);
    runningItem_->setText(tr("Running — %1").arg(runningItem_->data(Qt::UserRole + 1).toString()));
    updateQueue();
    success_ = std::move(success);
    failure_ = std::move(failure);
    pages_->setEnabled(false);
    for (auto *a : idleActions_)
        a->setEnabled(false);
    for (auto *a : archiveActions_)
        a->setEnabled(false);
    operationView_->begin(title);
    cancel_->setEnabled(true);
    progress_->setRange(0, 0);
    elapsed_.start();
    timer_->start();
    watcher_.setFuture(QtConcurrent::run([worker = std::move(worker)]() mutable {
        QString result;
        try {
            result = worker();
        } catch (const std::exception &e) {
            result = QString::fromUtf8(e.what());
        } catch (...) {
            result = QStringLiteral("Unexpected worker failure");
        }
        worker = {}; // Release job captures even if the QFuture outlives completion.
        return result;
    }));
}
void MainWindow::updateQueue() {
    QSignalBlocker blocker(queueView_);
    int completed = 0;
    for (int i = 0; i < queueView_->count(); ++i)
        completed += queueView_->item(i)->data(Qt::UserRole + 2).toBool();
    for (int i = 0; i < queueView_->count() && completed > 64;) {
        if (queueView_->item(i)->data(Qt::UserRole + 2).toBool()) {
            delete queueView_->takeItem(i);
            --completed;
        } else
            ++i;
    }
    auto *item = queueView_->currentItem();
    cancelPending_->setEnabled(item && item != runningItem_ &&
                               item->data(Qt::UserRole).toULongLong() != 0 &&
                               !item->data(Qt::UserRole + 2).toBool());
}
void MainWindow::startNextQueued() {
    if (busy_ || closePending_ || queuePaused_)
        return;
    auto next = queue_.take();
    if (!next)
        return;
    for (int i = 0; i < queueView_->count(); ++i)
        if (queueView_->item(i)->data(Qt::UserRole).toULongLong() == next->id) {
            runningItem_ = queueView_->item(i);
            break;
        }
    next->start();
    if (!busy_ && runningItem_) {
        runningItem_->setText(tr("Stopped — %1").arg(next->title));
        runningItem_->setData(Qt::UserRole + 2, true);
        runningItem_ = nullptr;
        updateQueue();
        QTimer::singleShot(0, this, &MainWindow::startNextQueued);
    }
}
bool MainWindow::enqueueRequest(const QString &title, std::function<void()> start,
                                const QString &details) {
    const auto id = queue_.enqueue(title, std::move(start));
    if (!id) {
        toast_->notify(tr("Operation queue is full"));
        return false;
    }
    auto *item = new QListWidgetItem(tr("Pending — %1").arg(title), queueView_);
    item->setData(Qt::UserRole, id);
    item->setData(Qt::UserRole + 1, title);
    item->setToolTip(details);
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
    if (!operation_)
        return;
    auto request = vynx::conflict_request(**operation_);
    if (!request.id || request.id == shownConflict_)
        return;
    shownConflict_ = request.id;
    auto op = operation_;
    auto *dialog = new ConflictDialog(text(request.existing_path), request.existing_size,
                                      request.existing_modified, text(request.incoming_name),
                                      request.incoming_size, request.incoming_modified, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::ApplicationModal);
    connect(dialog, &ConflictDialog::decision, dialog,
            [op, id = request.id](quint8 choice, bool all) {
                vynx::reply_conflict(**op, id, choice, all);
            });
    connect(dialog, &QDialog::finished, dialog,
            [op, id = request.id] { vynx::reply_conflict(**op, id, 0, false); });
    connect(&watcher_, &QFutureWatcher<QString>::finished, dialog, &QDialog::reject);
    auto *cancelTimer = new QTimer(dialog);
    connect(cancelTimer, &QTimer::timeout, dialog, [dialog, op] {
        if (vynx::cancelled(**op))
            dialog->reject();
    });
    cancelTimer->start(100);
    dialog->open();
}
