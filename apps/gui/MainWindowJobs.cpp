#include "MainWindow.h"
#include "dialogs/ConflictDialog.h"
#include "widgets/OperationPanel.h"
#include "widgets/ToastOverlay.h"
#include <QtConcurrent>
#include <QtWidgets>

static QString text(const rust::String &s) {
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}

void MainWindow::setupOperations(QVBoxLayout *layout) {
    operationView_ = new OperationPanel;
    operationPanel_ = operationView_;
    current_ = operationView_->current;
    rate_ = operationView_->rate;
    progress_ = operationView_->progress;
    cancel_ = operationView_->cancel;
    layout->addWidget(operationPanel_);
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
        operationView_->updateProgress(p.done, p.total, text(p.current), elapsed_.elapsed());
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
    });
}
void MainWindow::beginOperation() {
    shownConflict_ = 0;
    operation_ = std::make_shared<rust::Box<vynx::Operation>>(vynx::new_operation());
}
void MainWindow::runJob(const QString &title, std::function<QString()> worker,
                        std::function<void()> success, std::function<void(QString)> failure) {
    if (busy_)
        return;
    busy_ = true;
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
