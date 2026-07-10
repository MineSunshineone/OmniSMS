#include "omnisms/inbox.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <sstream>
#include <vector>

#include "omnisms/crypto.h"
#include "omnisms/text.h"

namespace omnisms {

MessageAddResult MessageStore::insert_received(InboxEntry entry, bool preserve_id)
{
    InboxSlot& slot = inbox_[inboxHead_];
    MessageAddResult result;
    if (inboxFilled_ == INBOX_CAPACITY && !slot.deleted) result.evictedId = slot.entry.id;
    if (!preserve_id) entry.id = ++inboxSeq_;
    else inboxSeq_ = std::max(inboxSeq_, entry.id);
    if (!preserve_id) entry.text = utf8_truncate(entry.text, MESSAGE_BODY_MAX_BYTES);
    slot.entry = std::move(entry);
    slot.deleted = false;
    result.id = slot.entry.id;
    inboxHead_ = (inboxHead_ + 1) % INBOX_CAPACITY;
    if (inboxFilled_ < INBOX_CAPACITY) ++inboxFilled_;
    return result;
}

MessageAddResult MessageStore::insert_sent(SentEntry entry, bool preserve_id)
{
    SentEntry& slot = sent_[sentHead_];
    MessageAddResult result;
    if (sentFilled_ == SENT_CAPACITY && slot.id != 0) result.evictedId = slot.id;
    if (!preserve_id) entry.id = ++sentSeq_;
    else sentSeq_ = std::max(sentSeq_, entry.id);
    if (!preserve_id) entry.text = utf8_truncate(entry.text, MESSAGE_BODY_MAX_BYTES);
    slot = std::move(entry);
    result.id = slot.id;
    sentHead_ = (sentHead_ + 1) % SENT_CAPACITY;
    if (sentFilled_ < SENT_CAPACITY) ++sentFilled_;
    return result;
}

MessageAddResult MessageStore::add_received(const std::string& sender, const std::string& text,
                                             const std::string& timestamp, uint32_t recv_epoch)
{
    InboxEntry entry;
    entry.recvEpoch = recv_epoch;
    entry.sender = sender;
    entry.ts = timestamp;
    entry.text = text;
    return insert_received(std::move(entry), false);
}

MessageAddResult MessageStore::add_sent(const std::string& target, const std::string& text,
                                        bool ok, uint32_t sent_epoch)
{
    SentEntry entry;
    entry.sentEpoch = sent_epoch;
    entry.target = target;
    entry.text = text;
    entry.ok = ok;
    return insert_sent(std::move(entry), false);
}

MessageAddResult MessageStore::restore_received(const InboxEntry& entry)
{
    if (entry.id == 0) return {};
    return insert_received(entry, true);
}

MessageAddResult MessageStore::restore_sent(const SentEntry& entry)
{
    if (entry.id == 0) return {};
    return insert_sent(entry, true);
}

bool MessageStore::set_forwarded(uint32_t id, bool forwarded)
{
    if (id == 0) return false;
    for (size_t i = 0; i < inboxFilled_; ++i) {
        InboxSlot& slot = inbox_[i];
        if (!slot.deleted && slot.entry.id == id) {
            slot.entry.forwarded = forwarded;
            return true;
        }
    }
    return false;
}

size_t MessageStore::received_count() const
{
    size_t count = 0;
    for (size_t i = 0; i < inboxFilled_; ++i) if (!inbox_[i].deleted) ++count;
    return count;
}

size_t MessageStore::sent_count() const
{
    return sentFilled_;
}

bool MessageStore::get_received_newest(size_t index, InboxEntry& out) const
{
    size_t seen = 0;
    for (size_t k = 0; k < inboxFilled_; ++k) {
        const size_t physical = (inboxHead_ + INBOX_CAPACITY - 1 - k) % INBOX_CAPACITY;
        if (inbox_[physical].deleted) continue;
        if (seen++ == index) {
            out = inbox_[physical].entry;
            return true;
        }
    }
    return false;
}

bool MessageStore::get_sent_newest(size_t index, SentEntry& out) const
{
    if (index >= sentFilled_) return false;
    out = sent_[(sentHead_ + SENT_CAPACITY - 1 - index) % SENT_CAPACITY];
    return true;
}

bool MessageStore::get_received_by_id(uint32_t id, InboxEntry& out) const
{
    if (id == 0) return false;
    for (size_t i = 0; i < inboxFilled_; ++i) {
        if (!inbox_[i].deleted && inbox_[i].entry.id == id) {
            out = inbox_[i].entry;
            return true;
        }
    }
    return false;
}

bool MessageStore::delete_received(uint32_t id)
{
    if (id == 0) return false;
    for (size_t i = 0; i < inboxFilled_; ++i) {
        InboxSlot& slot = inbox_[i];
        if (!slot.deleted && slot.entry.id == id) {
            slot.deleted = true;
            slot.entry = {};
            return true;
        }
    }
    return false;
}

std::string MessageStore::json(bool sent_box, int limit) const
{
    if (limit < 0) limit = 0;
    std::string out = "[";
    size_t emitted = 0;
    if (sent_box) {
        for (size_t k = 0; k < sentFilled_; ++k) {
            if (limit > 0 && emitted >= static_cast<size_t>(limit)) break;
            SentEntry entry;
            get_sent_newest(k, entry);
            if (emitted++) out += ',';
            out += "{\"id\":" + std::to_string(entry.id) +
                   ",\"sent\":" + std::to_string(entry.sentEpoch) + ',';
            json_prop(out, "target", entry.target); out += ',';
            json_prop(out, "text", entry.text); out += ",\"ok\":";
            out += entry.ok ? "true}" : "false}";
        }
    } else {
        for (size_t k = 0; k < received_count(); ++k) {
            if (limit > 0 && emitted >= static_cast<size_t>(limit)) break;
            InboxEntry entry;
            if (!get_received_newest(k, entry)) break;
            if (emitted++) out += ',';
            out += "{\"id\":" + std::to_string(entry.id) +
                   ",\"recv\":" + std::to_string(entry.recvEpoch) + ',';
            json_prop(out, "sender", entry.sender); out += ',';
            json_prop(out, "ts", entry.ts); out += ',';
            json_prop(out, "text", entry.text); out += ",\"fwd\":";
            out += entry.forwarded ? "true}" : "false}";
        }
    }
    out += ']';
    return out;
}

std::string MessageStore::snapshot() const
{
    auto encoded = [](const std::string& value) {
        return base64_encode(reinterpret_cast<const uint8_t*>(value.data()), value.size());
    };
    std::string out = "OMNISMS_MESSAGE_STORE_V1\nM\t";
    out += std::to_string(inboxHead_) + '\t' + std::to_string(inboxFilled_) + '\t' +
           std::to_string(inboxSeq_) + '\t' + std::to_string(sentHead_) + '\t' +
           std::to_string(sentFilled_) + '\t' + std::to_string(sentSeq_) + '\n';
    for (size_t i = 0; i < inbox_.size(); ++i) {
        const InboxSlot& slot = inbox_[i];
        out += "R\t" + std::to_string(i) + '\t' + (slot.deleted ? "1" : "0") + '\t' +
               std::to_string(slot.entry.id) + '\t' + std::to_string(slot.entry.recvEpoch) + '\t' +
               (slot.entry.forwarded ? "1" : "0") + '\t' + encoded(slot.entry.sender) + '\t' +
               encoded(slot.entry.ts) + '\t' + encoded(slot.entry.text) + '\n';
    }
    for (size_t i = 0; i < sent_.size(); ++i) {
        const SentEntry& entry = sent_[i];
        out += "S\t" + std::to_string(i) + '\t' + std::to_string(entry.id) + '\t' +
               std::to_string(entry.sentEpoch) + '\t' + (entry.ok ? "1" : "0") + '\t' +
               encoded(entry.target) + '\t' + encoded(entry.text) + '\n';
    }
    out += "END\n";
    return out;
}

bool MessageStore::restore_snapshot(const std::string& data, std::string* error)
{
    if (error) error->clear();
    auto fail = [&](const std::string& message) {
        if (error) *error = message;
        return false;
    };
    if (data.size() > 256 * 1024) return fail("message snapshot 超过 256 KiB");
    auto fields = [](const std::string& line) {
        std::vector<std::string> out;
        size_t start = 0;
        while (true) {
            const size_t tab = line.find('\t', start);
            if (tab == std::string::npos) {
                out.push_back(line.substr(start));
                break;
            }
            out.push_back(line.substr(start, tab - start));
            start = tab + 1;
        }
        return out;
    };
    auto number = [](const std::string& raw, uint64_t maximum, uint64_t& out) {
        if (raw.empty() || raw[0] == '-') return false;
        char* end = nullptr;
        errno = 0;
        const unsigned long long value = std::strtoull(raw.c_str(), &end, 10);
        if (errno || !end || *end || value > maximum) return false;
        out = static_cast<uint64_t>(value);
        return true;
    };
    auto line = [](std::istringstream& input, std::string& out) {
        if (!std::getline(input, out)) return false;
        if (!out.empty() && out.back() == '\r') out.pop_back();
        return true;
    };
    auto boolean = [](const std::string& raw, bool& out) {
        if (raw == "0") { out = false; return true; }
        if (raw == "1") { out = true; return true; }
        return false;
    };
    auto decode = [&](const std::string& raw, size_t maximum, std::string& out) {
        return base64_decode(raw, out) && out.size() <= maximum;
    };

    std::istringstream input(data);
    std::string current;
    if (!line(input, current) || current != "OMNISMS_MESSAGE_STORE_V1") {
        return fail("message snapshot 版本或文件头无效");
    }
    if (!line(input, current)) return fail("message snapshot 缺少元数据");
    const std::vector<std::string> meta = fields(current);
    if (meta.size() != 7 || meta[0] != "M") return fail("message snapshot 元数据格式无效");
    uint64_t inbox_head = 0, inbox_filled = 0, inbox_seq = 0;
    uint64_t sent_head = 0, sent_filled = 0, sent_seq = 0;
    if (!number(meta[1], INBOX_CAPACITY - 1, inbox_head) ||
        !number(meta[2], INBOX_CAPACITY, inbox_filled) ||
        !number(meta[3], UINT32_MAX, inbox_seq) ||
        !number(meta[4], SENT_CAPACITY - 1, sent_head) ||
        !number(meta[5], SENT_CAPACITY, sent_filled) ||
        !number(meta[6], UINT32_MAX, sent_seq)) {
        return fail("message snapshot 元数据超出范围");
    }
    if (inbox_filled < INBOX_CAPACITY && inbox_head != inbox_filled) {
        return fail("message snapshot 收件箱 head/filled 不一致");
    }
    if (sent_filled < SENT_CAPACITY && sent_head != sent_filled) {
        return fail("message snapshot 发件箱 head/filled 不一致");
    }

    MessageStore next;
    uint32_t max_inbox_id = 0;
    std::vector<uint32_t> inbox_ids;
    for (size_t i = 0; i < INBOX_CAPACITY; ++i) {
        if (!line(input, current)) return fail("message snapshot 收件槽数量不足");
        const std::vector<std::string> item = fields(current);
        if (item.size() != 9 || item[0] != "R") return fail("message snapshot 收件槽格式无效");
        uint64_t index = 0, id = 0, epoch = 0;
        bool deleted = false, forwarded = false;
        if (!number(item[1], INBOX_CAPACITY - 1, index) || index != i ||
            !boolean(item[2], deleted) || !number(item[3], UINT32_MAX, id) ||
            !number(item[4], UINT32_MAX, epoch) || !boolean(item[5], forwarded)) {
            return fail("message snapshot 收件槽字段无效");
        }
        InboxSlot& slot = next.inbox_[i];
        slot.deleted = deleted;
        slot.entry.id = static_cast<uint32_t>(id);
        slot.entry.recvEpoch = static_cast<uint32_t>(epoch);
        slot.entry.forwarded = forwarded;
        if (!decode(item[6], 1024, slot.entry.sender) ||
            !decode(item[7], 256, slot.entry.ts) ||
            !decode(item[8], MESSAGE_BODY_MAX_BYTES + 3, slot.entry.text)) {
            return fail("message snapshot 收件槽文本无效");
        }
        const bool outside = i >= inbox_filled && inbox_filled < INBOX_CAPACITY;
        if (outside || deleted) {
            if (!deleted || id != 0 || epoch != 0 || forwarded || !slot.entry.sender.empty() ||
                !slot.entry.ts.empty() || !slot.entry.text.empty()) {
                return fail("message snapshot 空/删除收件槽不规范");
            }
        } else {
            if (id == 0) return fail("message snapshot 活跃收件槽 ID 为 0");
            const uint32_t narrowed = static_cast<uint32_t>(id);
            if (std::find(inbox_ids.begin(), inbox_ids.end(), narrowed) != inbox_ids.end()) {
                return fail("message snapshot 收件 ID 重复");
            }
            inbox_ids.push_back(narrowed);
            max_inbox_id = std::max(max_inbox_id, narrowed);
        }
    }

    uint32_t max_sent_id = 0;
    std::vector<uint32_t> sent_ids;
    for (size_t i = 0; i < SENT_CAPACITY; ++i) {
        if (!line(input, current)) return fail("message snapshot 发件槽数量不足");
        const std::vector<std::string> item = fields(current);
        if (item.size() != 7 || item[0] != "S") return fail("message snapshot 发件槽格式无效");
        uint64_t index = 0, id = 0, epoch = 0;
        bool ok = false;
        if (!number(item[1], SENT_CAPACITY - 1, index) || index != i ||
            !number(item[2], UINT32_MAX, id) || !number(item[3], UINT32_MAX, epoch) ||
            !boolean(item[4], ok)) {
            return fail("message snapshot 发件槽字段无效");
        }
        SentEntry& entry = next.sent_[i];
        entry.id = static_cast<uint32_t>(id);
        entry.sentEpoch = static_cast<uint32_t>(epoch);
        entry.ok = ok;
        if (!decode(item[5], 1024, entry.target) ||
            !decode(item[6], MESSAGE_BODY_MAX_BYTES + 3, entry.text)) {
            return fail("message snapshot 发件槽文本无效");
        }
        const bool outside = i >= sent_filled && sent_filled < SENT_CAPACITY;
        if (outside) {
            if (id != 0 || epoch != 0 || ok || !entry.target.empty() || !entry.text.empty()) {
                return fail("message snapshot 空发件槽不规范");
            }
        } else {
            if (id == 0) return fail("message snapshot 活跃发件槽 ID 为 0");
            const uint32_t narrowed = static_cast<uint32_t>(id);
            if (std::find(sent_ids.begin(), sent_ids.end(), narrowed) != sent_ids.end()) {
                return fail("message snapshot 发件 ID 重复");
            }
            sent_ids.push_back(narrowed);
            max_sent_id = std::max(max_sent_id, narrowed);
        }
    }
    if (!line(input, current) || current != "END") return fail("message snapshot 缺少结束标记");
    while (line(input, current)) {
        if (!trim(current).empty()) return fail("message snapshot 结束标记后存在额外内容");
    }
    if (max_inbox_id > inbox_seq || max_sent_id > sent_seq) {
        return fail("message snapshot sequence 小于现有 ID");
    }
    next.inboxHead_ = static_cast<size_t>(inbox_head);
    next.inboxFilled_ = static_cast<size_t>(inbox_filled);
    next.inboxSeq_ = static_cast<uint32_t>(inbox_seq);
    next.sentHead_ = static_cast<size_t>(sent_head);
    next.sentFilled_ = static_cast<size_t>(sent_filled);
    next.sentSeq_ = static_cast<uint32_t>(sent_seq);
    *this = std::move(next);
    return true;
}

void MessageStore::clear()
{
    *this = MessageStore();
}

}  // namespace omnisms
