// 平台无关的转发重试队列：不创建线程、不睡眠，时间由端口注入。
#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <deque>
#include <utility>

#include "omnisms/push.h"
#include "omnisms/sms.h"

namespace omnisms {

// MCU 用固定容量到期队列：不扩容、不创建线程，时间由端口注入。
// 按槽位顺序取第一个已到期任务，与 ESP 原固定数组行为一致。
template <typename T, size_t Capacity>
class FixedDueQueue {
public:
    static_assert(Capacity > 0, "FixedDueQueue capacity must be positive");

    bool enqueue(const T& value, int64_t due_us)
    {
        return emplace(value, due_us);
    }

    bool enqueue(T&& value, int64_t due_us)
    {
        return emplace(std::move(value), due_us);
    }

    bool take_ready(int64_t now_us, T& out)
    {
        return take_ready_if(now_us, out, [](const T&) { return true; });
    }

    template <typename Predicate>
    bool take_ready_if(int64_t now_us, T& out, Predicate predicate)
    {
        for (Slot& slot : slots_) {
            if (!slot.used || slot.dueUs > now_us || !predicate(slot.value)) continue;
            out = std::move(slot.value);
            slot = Slot{};
            --size_;
            return true;
        }
        return false;
    }

    template <typename Visitor>
    void for_each(Visitor visitor)
    {
        for (Slot& slot : slots_) {
            if (slot.used) visitor(slot.value);
        }
    }

    void clear()
    {
        slots_ = {};
        size_ = 0;
    }

    size_t size() const { return size_; }
    size_t free() const { return Capacity - size_; }
    bool empty() const { return size_ == 0; }

private:
    struct Slot {
        bool used = false;
        int64_t dueUs = 0;
        T value{};
    };

    template <typename U>
    bool emplace(U&& value, int64_t due_us)
    {
        for (Slot& slot : slots_) {
            if (slot.used) continue;
            slot.used = true;
            slot.dueUs = due_us;
            slot.value = std::forward<U>(value);
            ++size_;
            return true;
        }
        return false;
    }

    std::array<Slot, Capacity> slots_{};
    size_t size_ = 0;
};

struct ForwardJob {
    int target = 0;            // 端口定义目标：如 0..4=推送通道，-1=邮件
    SmsEvent event;
    uint8_t attempts = 0;      // 已失败次数；0 表示尚未做首轮尝试
    int64_t nextAttemptUs = 0;
    uint32_t retrySeed = 0;
    uint32_t messageId = 0;    // 可选：关联收件箱条目，供平台汇总多目标完成状态
    bool notify = false;       // true=平台维护通知，不套短信正文模板
};

class ForwardRetryQueue {
public:
    explicit ForwardRetryQueue(size_t capacity = 64) : capacity_(capacity) {}

    bool enqueue(int target, const SmsEvent& event, int64_t now_us,
                 uint32_t retry_seed = 0, uint32_t message_id = 0, bool notify = false);

    // 取出到期时间最早且已到期的任务；任务会从队列移除。
    bool take_ready(int64_t now_us, ForwardJob& out);

    // 失败后增加 attempts 并按统一指数退避重新入队；达到 PUSH_RETRY_MAX 返回 false。
    bool retry(ForwardJob& job, int64_t now_us);

    size_t size() const { return jobs_.size(); }
    bool empty() const { return jobs_.empty(); }
    bool has_initial_attempt() const;
    int64_t next_due_us() const;  // 空队列返回 -1

private:
    size_t capacity_;
    std::deque<ForwardJob> jobs_;
};

}  // namespace omnisms
