#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace omnisms {

constexpr size_t INBOX_CAPACITY = 50;
constexpr size_t SENT_CAPACITY = 10;
constexpr size_t MESSAGE_BODY_MAX_BYTES = 320;

struct InboxEntry {
    uint32_t id = 0;
    uint32_t recvEpoch = 0;
    std::string sender;
    std::string ts;
    std::string text;
    bool forwarded = false;
};

struct SentEntry {
    uint32_t id = 0;
    uint32_t sentEpoch = 0;
    std::string target;
    std::string text;
    bool ok = false;
};

struct MessageAddResult {
    uint32_t id = 0;
    uint32_t evictedId = 0;
};

// 原 ESP 固件收发件箱的纯状态机：固定容量环形槽、删除留洞、ID 单调递增、newest-first。
// 不含锁和 I/O；平台在外层加锁，并用 evictedId/get_by_id 驱动 NVS/文件持久化。
class MessageStore {
public:
    MessageAddResult add_received(const std::string& sender, const std::string& text,
                                  const std::string& timestamp, uint32_t recv_epoch);
    MessageAddResult add_sent(const std::string& target, const std::string& text,
                              bool ok, uint32_t sent_epoch);

    // 启动恢复用：调用方按 ID 旧→新传入。超容量时保持与正常环形覆盖相同的淘汰语义。
    MessageAddResult restore_received(const InboxEntry& entry);
    MessageAddResult restore_sent(const SentEntry& entry);

    bool set_forwarded(uint32_t id, bool forwarded);
    size_t received_count() const;
    size_t sent_count() const;
    bool get_received_newest(size_t index, InboxEntry& out) const;
    bool get_sent_newest(size_t index, SentEntry& out) const;
    bool get_received_by_id(uint32_t id, InboxEntry& out) const;
    bool delete_received(uint32_t id);

    std::string json(bool sent_box, int limit = 0) const;

    // Exact, versioned persistence format for platform storage. Unlike json(), this preserves
    // physical ring slots, deletion holes, heads, filled counts and monotonic sequences.
    std::string snapshot() const;
    bool restore_snapshot(const std::string& snapshot, std::string* error = nullptr);
    void clear();

private:
    struct InboxSlot {
        InboxEntry entry;
        bool deleted = true;
    };

    MessageAddResult insert_received(InboxEntry entry, bool preserve_id);
    MessageAddResult insert_sent(SentEntry entry, bool preserve_id);

    std::array<InboxSlot, INBOX_CAPACITY> inbox_{};
    std::array<SentEntry, SENT_CAPACITY> sent_{};
    size_t inboxHead_ = 0;
    size_t inboxFilled_ = 0;
    uint32_t inboxSeq_ = 0;
    size_t sentHead_ = 0;
    size_t sentFilled_ = 0;
    uint32_t sentSeq_ = 0;
};

}  // namespace omnisms
