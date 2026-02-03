#include "DbJobQueue.h"

void DbJobQueue::Push(DbJob j)
{
    {
        std::lock_guard<std::mutex> lock(mtx_);
        q_.push(std::move(j));
    }
    cv_.notify_one();
}

bool DbJobQueue::PopWait(DbJob& out)
{
    std::unique_lock<std::mutex> lock(mtx_);
    cv_.wait(lock, [&]{ return stop_ || !q_.empty(); });
    if (stop_ && q_.empty()) return false;
    out = std::move(q_.front());
    q_.pop();
    return true;
}

void DbJobQueue::Stop()
{
    {
        std::lock_guard<std::mutex> lock(mtx_);
        stop_ = true;
    }
    cv_.notify_all();
}
