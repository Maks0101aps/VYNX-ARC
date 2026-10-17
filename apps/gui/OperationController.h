#pragma once
#include "ArchiveSession.h"
#include <QFutureWatcher>
#include <QtConcurrent>
#include <functional>
// Owns the serial mutating lane. Documents survive until their jobs finish.
class OperationController {
    QFutureWatcher<QString> watcher_;

  public:
    bool busy = false;
    std::shared_ptr<ArchiveSession> owner;
    std::shared_ptr<rust::Box<vynx::Operation>> operation;
    QFutureWatcher<QString> &watcher() { return watcher_; }
    void start(std::function<QString()> worker) {
        watcher_.setFuture(QtConcurrent::run([worker = std::move(worker)]() mutable {
            QString result;
            try {
                result = worker();
            } catch (const std::exception &error) {
                result = QString::fromUtf8(error.what());
            } catch (...) {
                result = QStringLiteral("Unexpected worker failure");
            }
            worker = {};
            return result;
        }));
    }
    ~OperationController() {
        if (operation)
            vynx::cancel(**operation);
        watcher_.waitForFinished();
    }
};
