#pragma once
#include <QString>
#include <deque>
#include <functional>
#include <optional>
// In-process FIFO. The first implementation serializes every job, including reads.
class OperationQueue {
  public:
    struct Job {
        quint64 id;
        QString title;
        std::function<void()> start;
    };
    quint64 enqueue(QString title, std::function<void()> start) {
        if (pending_.size() >= 32)
            return 0;
        const auto id = ++nextId_;
        pending_.push_back({id, std::move(title), std::move(start)});
        return id;
    }
    bool cancel(quint64 id) {
        for (auto it = pending_.begin(); it != pending_.end(); ++it) {
            if (it->id == id) {
                pending_.erase(it);
                return true;
            }
        }
        return false;
    }
    std::optional<Job> take() {
        if (pending_.empty())
            return std::nullopt;
        auto job = std::move(pending_.front());
        pending_.pop_front();
        return job;
    }
    void clear() { pending_.clear(); }
    size_t size() const { return pending_.size(); }

  private:
    std::deque<Job> pending_;
    quint64 nextId_ = 0;
};
