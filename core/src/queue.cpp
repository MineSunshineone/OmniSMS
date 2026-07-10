#include "omnisms/queue.h"

#include <algorithm>
#include <utility>

namespace omnisms {

bool ForwardRetryQueue::enqueue(int target, const SmsEvent& event, int64_t now_us,
                                uint32_t retry_seed, uint32_t message_id, bool notify)
{
    if (jobs_.size() >= capacity_) return false;
    jobs_.push_back({target, event, 0, now_us, retry_seed, message_id, notify});
    return true;
}

bool ForwardRetryQueue::take_ready(int64_t now_us, ForwardJob& out)
{
    if (jobs_.empty()) return false;
    auto next = std::min_element(jobs_.begin(), jobs_.end(),
        [](const ForwardJob& left, const ForwardJob& right) {
            return left.nextAttemptUs < right.nextAttemptUs;
        });
    if (next->nextAttemptUs > now_us) return false;
    out = std::move(*next);
    jobs_.erase(next);
    return true;
}

bool ForwardRetryQueue::retry(ForwardJob& job, int64_t now_us)
{
    ++job.attempts;
    if (job.attempts >= PUSH_RETRY_MAX || jobs_.size() >= capacity_) return false;
    const uint32_t delay = backoff_seconds(job.attempts, job.retrySeed + job.attempts);
    job.nextAttemptUs = now_us + static_cast<int64_t>(delay) * 1000 * 1000;
    jobs_.push_back(job);
    return true;
}

bool ForwardRetryQueue::has_initial_attempt() const
{
    for (const ForwardJob& job : jobs_) {
        if (job.attempts == 0) return true;
    }
    return false;
}

int64_t ForwardRetryQueue::next_due_us() const
{
    if (jobs_.empty()) return -1;
    return std::min_element(jobs_.begin(), jobs_.end(),
        [](const ForwardJob& left, const ForwardJob& right) {
            return left.nextAttemptUs < right.nextAttemptUs;
        })->nextAttemptUs;
}

}  // namespace omnisms
