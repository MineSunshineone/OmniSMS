// PDU 解码包装(vendored pdulib) + 长短信合并器(自 sms_forwarding idf_sms 抽取)
// ConcatAssembler 为纯逻辑：时间由调用方注入(now_us)，无平台依赖，可单测。
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace omnisms {

struct DecodedSms {
    std::string sender;
    std::string text;
    std::string timestamp;   // pdulib 原始时间戳(YYMMDDHHMMSS±zz)；端口可换算本地时间
    int concat[3] = {0, 0, 0};  // ref / part / total；total<=1 表示非长短信
};

// 解码一行 PDU 十六进制。线程不安全(内部复用解码缓冲)，调用方自行串行化。
// 返回 false = 非法 hex 或解析失败。
class PduDecoder {
public:
    explicit PduDecoder(int work_buf_size = 4096);
    ~PduDecoder();
    PduDecoder(const PduDecoder&) = delete;
    PduDecoder& operator=(const PduDecoder&) = delete;
    bool decode(const std::string& pdu_hex, DecodedSms& out);

private:
    void* impl_;  // PDU*(pdulib)，避免头文件泄漏 vendored 库
};

struct EncodedPdu {
    std::string payload;  // 十六进制 PDU，末尾包含模组提交所需 Ctrl-Z
    int tpduLength = 0;   // AT+CMGS=<length> 使用的长度
    uint8_t part = 1;
    uint8_t total = 1;
};

// 收发共用一个 pdulib 工作缓冲，适合 RAM 受限的 MCU。
// 调用方需自行保证串行化；ESP 端用同一把互斥锁保护收发。
class PduCodec {
public:
    explicit PduCodec(int work_buf_size = 4096);
    ~PduCodec();
    PduCodec(const PduCodec&) = delete;
    PduCodec& operator=(const PduCodec&) = delete;

    bool decode(const std::string& pdu_hex, DecodedSms& out);
    bool encode_text(const std::string& phone, const std::string& text,
                     uint16_t concat_reference, std::vector<EncodedPdu>& out,
                     std::string* error = nullptr);

private:
    void* impl_;
};

// UTF-8 短信发送编码。分段规则与 ESP 基准一致：GSM7 160/152 septet，
// UCS2 70/66 UTF-16 码元；输入上限 300 UTF-8 字节。
class PduEncoder {
public:
    explicit PduEncoder(int work_buf_size = 4096);
    ~PduEncoder();
    PduEncoder(const PduEncoder&) = delete;
    PduEncoder& operator=(const PduEncoder&) = delete;

    bool encode_text(const std::string& phone, const std::string& text,
                     uint16_t concat_reference, std::vector<EncodedPdu>& out,
                     std::string* error = nullptr);

private:
    void* impl_;
};

// 长短信合并：喂入分段，凑齐(或超时/槽位挤占)时通过回调吐出完整短信。
// 与原固件语义一致：缺失分段以 "[缺失分段N]" 标记，不静默丢弃。
class ConcatAssembler {
public:
    static constexpr size_t SLOTS = 5;
    static constexpr size_t PARTS = 10;
    static constexpr int64_t TIMEOUT_US = 30LL * 1000LL * 1000LL;

    using Emit = std::function<void(const std::string& sender, const std::string& text,
                                    const std::string& timestamp)>;

    enum class NoticeKind {
        PartReceived,
        InvalidPart,
        SlotEvicted,
        TimedOut,
    };
    struct Notice {
        NoticeKind kind;
        int reference = 0;
        int part = 0;
        int received = 0;
        int total = 0;
    };
    using Observe = std::function<void(const Notice& notice)>;

    explicit ConcatAssembler(Emit emit, Observe observe = {})
        : emit_(std::move(emit)), observe_(std::move(observe)) {}

    // 单条(非分段)短信也可直接走 feed，total<=1 时立即 emit
    void feed(const DecodedSms& sms, int64_t now_us);
    // 周期调用：超时槽位按现状合并吐出
    void expire(int64_t now_us);

private:
    struct Part {
        bool valid = false;
        std::string text;
    };
    struct Slot {
        bool active = false;
        int ref = 0;
        int total = 0;
        int received = 0;
        std::string sender;
        std::string timestamp;
        int64_t lastUs = 0;
        std::array<Part, PARTS> parts;
    };

    std::string assemble(const Slot& slot) const;
    void clear(Slot& slot);
    Slot& find_slot(int ref, const std::string& sender, int total, int64_t now_us);
    void notice(NoticeKind kind, int ref, int part, int received, int total) const;

    Emit emit_;
    Observe observe_;
    std::array<Slot, SLOTS> slots_ = {};
};

}  // namespace omnisms
