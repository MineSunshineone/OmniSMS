// OmniSMS core —— 平台无关的短信事件模型与端口接口
// core 的输入切在"短信事件"层而不是 AT 层：ESP 端口喂 PDU 解码结果，
// Linux 端口可走 ModemManager 或 AT tty，Android 端口走 Telephony API。
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace omnisms {

// 规范化短信事件：所有端口交给 core 的统一形态
struct SmsEvent {
    std::string sender;     // 发送方号码(尽量含国际前缀)
    std::string text;       // 完整正文(长短信由端口或 core/concat 合并后)
    std::string timestamp;  // "YYYY-MM-DD HH:MM:SS" 本地时间；未知可为空
    std::string receiver;   // 本机号码(多设备/多卡场景区分接收端)；未知可为空
};

struct CallEvent {
    std::string caller;     // 空字符串表示隐藏号码或平台未提供号码
    std::string timestamp;
    std::string receiver;
};

// ===== 端口基础接口 =====

// 短信/来电通道。可选能力用默认 false/no-op 表示，禁止伪实现。
class ModemInterface {
public:
    virtual ~ModemInterface() = default;
    virtual void set_sms_handler(std::function<void(const SmsEvent&)> on_sms) = 0;
    virtual void set_call_handler(std::function<void(const CallEvent&)> on_call)
    {
        (void)on_call;
    }
    virtual bool send_sms(const std::string& to, const std::string& text) = 0;
    virtual bool send_ussd(const std::string& code, std::string& response)
    {
        (void)code;
        response.clear();
        return false;
    }
};

// 旧端口源码兼容名；新代码统一使用 ModemInterface。
using SmsTransport = ModemInterface;

// HTTP(S) 客户端：推送通道出口。实现需支持自定义 method/headers/body。
struct HttpRequest {
    std::string method = "POST";
    std::string url;
    std::string content_type = "application/json";
    std::string body;
    int timeout_ms = 10000;
};
struct HttpResponse {
    int status = 0;        // 0 = 传输层失败
    std::string body;
};
class HttpClient {
public:
    virtual ~HttpClient() = default;
    virtual HttpResponse request(const HttpRequest& req) = 0;
};

// 键值存储：配置持久化(ESP=NVS，Linux=文件，Android=SharedPreferences)
class KeyValueStore {
public:
    virtual ~KeyValueStore() = default;
    virtual bool get(const std::string& key, std::string& out) = 0;
    virtual bool set(const std::string& key, const std::string& value) = 0;
    virtual bool erase(const std::string& key) = 0;
};

// 日志：core 只输出一行文本，格式化/环形缓冲由端口决定
class Logger {
public:
    virtual ~Logger() = default;
    virtual void line(const std::string& msg) = 0;
};

// 单调时钟(微秒)：重试退避、长短信合并超时用；禁止用墙钟
class Clock {
public:
    virtual ~Clock() = default;
    virtual int64_t now_us() = 0;
};

}  // namespace omnisms
