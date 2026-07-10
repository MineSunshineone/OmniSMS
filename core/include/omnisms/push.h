// 推送通道报文组装(自 sms_forwarding idf_push 抽取)
// 纯逻辑：给定通道配置 + 短信事件 + 注入的时间，产出 HttpRequest；发送由端口执行。
// 报文格式与固件版逐字节一致，同一配置在任何端口推送效果相同。
#pragma once

#include <cstdint>
#include <string>

#include "omnisms/sms.h"

namespace omnisms {

enum : uint8_t {
    PUSH_TYPE_NONE = 0,
    PUSH_TYPE_POST_JSON = 1,
    PUSH_TYPE_BARK = 2,
    PUSH_TYPE_GET = 3,
    PUSH_TYPE_DINGTALK = 4,
    PUSH_TYPE_PUSHPLUS = 5,
    PUSH_TYPE_SERVERCHAN = 6,
    PUSH_TYPE_CUSTOM = 7,
    PUSH_TYPE_FEISHU = 8,
    PUSH_TYPE_GOTIFY = 9,
    PUSH_TYPE_TELEGRAM = 10,
};

struct PushChannel {
    bool enabled = false;
    uint8_t type = PUSH_TYPE_POST_JSON;
    std::string name;
    std::string url;
    std::string key1;        // Bark key / 钉钉 Secret / PushPlus token / TG ChatID…
    std::string key2;        // Bark 参数 / PushPlus 渠道 / TG Bot Token…
    std::string customBody;  // PUSH_TYPE_CUSTOM 请求体模板
};

// 通道配置是否可用(未启用/缺必填项返回 false)
bool channel_valid(const PushChannel& ch);

// 组装推送请求。notify=true 时 sender 视为任务名(不套短信模板)。
// now_epoch_ms 用于钉钉/飞书签名(注入避免 core 读钟)。
// 返回 false = 通道无效或类型未知。
bool build_push_request(const PushChannel& ch, const SmsEvent& ev, bool notify,
                        int64_t now_epoch_ms, HttpRequest& out);

// 指数退避：base 20s 起倍增至 600s 封顶，附 1/4 抖动(seed 取模)
uint32_t backoff_seconds(uint8_t attempts, uint32_t seed);

constexpr uint8_t PUSH_RETRY_MAX = 6;

}  // namespace omnisms
