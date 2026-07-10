#include "omnisms/pdu.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <utility>

#include "omnisms/phone.h"
#include "omnisms/text.h"
#include "pdulib.h"

namespace omnisms {

static bool is_hex_string(const std::string& line)
{
    if (line.empty() || (line.size() % 2) != 0) return false;
    for (char ch : line) {
        if (!isxdigit(static_cast<unsigned char>(ch))) return false;
    }
    return true;
}

PduDecoder::PduDecoder(int work_buf_size)
{
    impl_ = new PDU(work_buf_size);
}

PduDecoder::~PduDecoder()
{
    delete static_cast<PDU*>(impl_);
}

static bool decode_with(PDU* pdu, const std::string& pdu_hex, DecodedSms& out)
{
    if (!is_hex_string(pdu_hex)) return false;
    if (!pdu->decodePDU(pdu_hex.c_str())) return false;
    out.sender = pdu->getSender();
    out.text = pdu->getText();
    out.timestamp = pdu->getTimeStamp();
    int* concat = pdu->getConcatInfo();
    if (concat) {
        out.concat[0] = concat[0];
        out.concat[1] = concat[1];
        out.concat[2] = concat[2];
    } else {
        out.concat[0] = out.concat[1] = out.concat[2] = 0;
    }
    return true;
}

bool PduDecoder::decode(const std::string& pdu_hex, DecodedSms& out)
{
    return decode_with(static_cast<PDU*>(impl_), pdu_hex, out);
}

PduCodec::PduCodec(int work_buf_size)
{
    impl_ = new PDU(work_buf_size);
}

PduCodec::~PduCodec()
{
    delete static_cast<PDU*>(impl_);
}

bool PduCodec::decode(const std::string& pdu_hex, DecodedSms& out)
{
    return decode_with(static_cast<PDU*>(impl_), pdu_hex, out);
}

PduEncoder::PduEncoder(int work_buf_size)
{
    impl_ = new PDU(work_buf_size);
}

PduEncoder::~PduEncoder()
{
    delete static_cast<PDU*>(impl_);
}

static bool basic_gsm7(const std::string& text)
{
    for (unsigned char ch : text) {
        if (ch == '\r' || ch == '\n') continue;
        if (ch < 0x20 || ch >= 0x7F) return false;
    }
    return true;
}

static std::vector<std::string> split_sms(const std::string& text, bool gsm7, size_t per_part)
{
    std::vector<std::string> parts;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t units = 0;
        size_t end = pos;
        while (end < text.size()) {
            unsigned char lead = static_cast<unsigned char>(text[end]);
            size_t char_len = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
            if (end + char_len > text.size()) char_len = text.size() - end;
            size_t char_units = gsm7
                ? (char_len == 1 && std::strchr("[]{}\\^~|", text[end]) ? 2 : 1)
                : (char_len == 4 ? 2 : 1);
            if (units + char_units > per_part) break;
            units += char_units;
            end += char_len;
        }
        if (end == pos) return {};
        parts.push_back(text.substr(pos, end - pos));
        pos = end;
    }
    return parts;
}

static bool encode_with(PDU* pdu, const std::string& phone_raw, const std::string& text_raw,
                        uint16_t concat_reference, std::vector<EncodedPdu>& out,
                        std::string* error)
{
    out.clear();
    if (error) error->clear();
    const std::string phone = trim(phone_raw);
    const std::string text = trim(text_raw);
    if (!is_valid_phone_number(phone) || text.empty() || text.size() > 300) {
        if (error) *error = "号码或内容无效";
        return false;
    }

    const bool gsm7 = basic_gsm7(text);
    std::vector<std::string> parts = split_sms(text, gsm7, gsm7 ? 160 : 70);
    if (parts.size() > 1) parts = split_sms(text, gsm7, gsm7 ? 152 : 66);
    if (parts.empty() || parts.size() > 255) {
        if (error) *error = "短信分段失败";
        return false;
    }

    const uint16_t reference = parts.size() > 1 ? (concat_reference ? concat_reference : 1) : 0;
    out.reserve(parts.size());
    for (size_t i = 0; i < parts.size(); ++i) {
        pdu->setSCAnumber();
        const uint8_t total = reference ? static_cast<uint8_t>(parts.size()) : 0;
        const uint8_t part = reference ? static_cast<uint8_t>(i + 1) : 0;
        const int length = pdu->encodePDU(phone.c_str(), parts[i].c_str(), reference, total, part);
        if (length < 0 || !pdu->getSMS()) {
            out.clear();
            if (error) *error = "PDU 编码失败(" + std::to_string(length) + ")";
            return false;
        }
        EncodedPdu encoded;
        encoded.payload.assign(pdu->getSMS(), std::strlen(pdu->getSMS()));
        encoded.tpduLength = length;
        encoded.part = static_cast<uint8_t>(i + 1);
        encoded.total = static_cast<uint8_t>(parts.size());
        out.push_back(std::move(encoded));
    }
    return true;
}

bool PduEncoder::encode_text(const std::string& phone_raw, const std::string& text_raw,
                             uint16_t concat_reference, std::vector<EncodedPdu>& out,
                             std::string* error)
{
    return encode_with(static_cast<PDU*>(impl_), phone_raw, text_raw,
                       concat_reference, out, error);
}

bool PduCodec::encode_text(const std::string& phone_raw, const std::string& text_raw,
                           uint16_t concat_reference, std::vector<EncodedPdu>& out,
                           std::string* error)
{
    return encode_with(static_cast<PDU*>(impl_), phone_raw, text_raw,
                       concat_reference, out, error);
}

std::string ConcatAssembler::assemble(const Slot& slot) const
{
    std::string text;
    for (int i = 0; i < slot.total && i < static_cast<int>(PARTS); ++i) {
        if (slot.parts[i].valid) {
            text += slot.parts[i].text;
        } else {
            // 超时/挤占强制合并时标记缺口，收件人能看出内容不完整
            text += "[缺失分段";
            text += std::to_string(i + 1);
            text += "]";
        }
    }
    return text;
}

void ConcatAssembler::notice(NoticeKind kind, int ref, int part, int received, int total) const
{
    if (observe_) observe_(Notice{kind, ref, part, received, total});
}

void ConcatAssembler::clear(Slot& slot)
{
    slot.active = false;
    slot.ref = 0;
    slot.total = 0;
    slot.received = 0;
    slot.sender.clear();
    slot.timestamp.clear();
    slot.lastUs = 0;
    for (auto& part : slot.parts) {
        part.valid = false;
        part.text.clear();
    }
}

ConcatAssembler::Slot& ConcatAssembler::find_slot(int ref, const std::string& sender, int total,
                                                  int64_t now_us)
{
    for (auto& slot : slots_) {
        if (slot.active && slot.ref == ref && slot.sender == sender && slot.total == total) return slot;
    }
    for (auto& slot : slots_) {
        if (!slot.active || now_us - slot.lastUs > TIMEOUT_US) {
            clear(slot);
            slot.active = true;
            slot.ref = ref;
            slot.total = total;
            slot.sender = sender;
            slot.lastUs = now_us;
            return slot;
        }
    }
    auto oldest = std::min_element(slots_.begin(), slots_.end(),
        [](const Slot& a, const Slot& b) { return a.lastUs < b.lastUs; });
    // 槽位全忙被挤占：已收到的分段不能悄悄扔掉——分段可能已从 SIM 删除，
    // 丢了就再也拿不回。与超时路径一致，先按现状合并吐出(缺口有标记)。
    {
        std::string partial = assemble(*oldest);
        if (!partial.empty()) {
            notice(NoticeKind::SlotEvicted, oldest->ref, 0, oldest->received, oldest->total);
            emit_(oldest->sender, partial, oldest->timestamp);
        }
    }
    clear(*oldest);
    oldest->active = true;
    oldest->ref = ref;
    oldest->total = total;
    oldest->sender = sender;
    oldest->lastUs = now_us;
    return *oldest;
}

void ConcatAssembler::feed(const DecodedSms& sms, int64_t now_us)
{
    int ref = sms.concat[0];
    int part = sms.concat[1];
    int total = sms.concat[2];

    if (total > 1 && part > 0) {
        if (total > static_cast<int>(PARTS) || part > total) {
            // 分段参数超限：按单条处理，不丢内容
            notice(NoticeKind::InvalidPart, ref, part, 1, total);
            emit_(sms.sender, sms.text, sms.timestamp);
            return;
        }
        Slot& slot = find_slot(ref, sms.sender, total, now_us);
        int idx = part - 1;
        if (!slot.parts[idx].valid) {
            slot.parts[idx].valid = true;
            slot.parts[idx].text = sms.text;
            slot.received++;
            slot.lastUs = now_us;
            if (slot.timestamp.empty()) slot.timestamp = sms.timestamp;
            notice(NoticeKind::PartReceived, ref, part, slot.received, total);
        }
        if (slot.received >= slot.total) {
            std::string full = assemble(slot);
            emit_(slot.sender, full, slot.timestamp);
            clear(slot);
        }
        return;
    }

    emit_(sms.sender, sms.text, sms.timestamp);
}

void ConcatAssembler::expire(int64_t now_us)
{
    for (auto& slot : slots_) {
        if (!slot.active || now_us - slot.lastUs <= TIMEOUT_US) continue;
        std::string full = assemble(slot);
        if (!full.empty()) {
            notice(NoticeKind::TimedOut, slot.ref, 0, slot.received, slot.total);
            emit_(slot.sender, full, slot.timestamp);
        }
        clear(slot);
    }
}

}  // namespace omnisms
