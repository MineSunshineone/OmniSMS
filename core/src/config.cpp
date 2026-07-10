#include "omnisms/config.h"

#include <cctype>
#include <cstdint>
#include <limits>
#include <utility>

#include "omnisms/text.h"

namespace omnisms {
namespace {

class JsonParser {
public:
    JsonParser(const std::string& input, std::string* error) : input_(input), error_(error) {}

    bool parse(AppConfig& out)
    {
        if (input_.size() > 1024 * 1024) return fail("配置超过 1 MiB 上限");
        AppConfig next;
        bool saw_version = false;
        if (!root(next, saw_version)) return false;
        whitespace();
        if (pos_ != input_.size()) return fail("JSON 末尾存在多余内容");
        if (!saw_version) return fail("缺少 schemaVersion");
        if (next.schemaVersion != CONFIG_SCHEMA_VERSION) return fail("不支持的 schemaVersion");
        out = std::move(next);
        return true;
    }

private:
    bool fail(const std::string& message)
    {
        if (error_) *error_ = message + "（位置 " + std::to_string(pos_) + "）";
        return false;
    }

    void whitespace()
    {
        while (pos_ < input_.size() &&
               std::isspace(static_cast<unsigned char>(input_[pos_]))) ++pos_;
    }

    bool take(char expected)
    {
        whitespace();
        if (pos_ >= input_.size() || input_[pos_] != expected) {
            return fail(std::string("应为 '") + expected + "'");
        }
        ++pos_;
        return true;
    }

    bool literal(const char* value)
    {
        whitespace();
        size_t n = 0;
        while (value[n]) ++n;
        if (input_.compare(pos_, n, value) != 0) return fail("JSON 字面量无效");
        pos_ += n;
        return true;
    }

    static int hex(char ch)
    {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
    }

    bool unicode_unit(uint32_t& value)
    {
        if (pos_ + 4 > input_.size()) return fail("Unicode 转义不完整");
        value = 0;
        for (int i = 0; i < 4; ++i) {
            int digit = hex(input_[pos_++]);
            if (digit < 0) return fail("Unicode 转义包含非十六进制字符");
            value = (value << 4) | static_cast<uint32_t>(digit);
        }
        return true;
    }

    static void append_utf8(std::string& out, uint32_t cp)
    {
        if (cp <= 0x7F) {
            out += static_cast<char>(cp);
        } else if (cp <= 0x7FF) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp <= 0xFFFF) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool string(std::string& out)
    {
        whitespace();
        if (pos_ >= input_.size() || input_[pos_] != '"') return fail("应为 JSON 字符串");
        ++pos_;
        out.clear();
        while (pos_ < input_.size()) {
            unsigned char ch = static_cast<unsigned char>(input_[pos_++]);
            if (ch == '"') return true;
            if (ch < 0x20) return fail("字符串含未转义控制字符");
            if (ch != '\\') {
                out += static_cast<char>(ch);
                continue;
            }
            if (pos_ >= input_.size()) return fail("字符串转义不完整");
            char esc = input_[pos_++];
            switch (esc) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t cp = 0;
                    if (!unicode_unit(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        if (pos_ + 2 > input_.size() || input_[pos_] != '\\' ||
                            input_[pos_ + 1] != 'u') return fail("高代理项缺少低代理项");
                        pos_ += 2;
                        uint32_t low = 0;
                        if (!unicode_unit(low)) return false;
                        if (low < 0xDC00 || low > 0xDFFF) return fail("低代理项无效");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        return fail("孤立的低代理项");
                    }
                    append_utf8(out, cp);
                    break;
                }
                default: return fail("未知字符串转义");
            }
        }
        return fail("字符串未结束");
    }

    bool boolean(bool& out)
    {
        whitespace();
        if (input_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            out = true;
            return true;
        }
        if (input_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            out = false;
            return true;
        }
        return fail("应为布尔值");
    }

    bool integer(int& out)
    {
        whitespace();
        bool negative = false;
        if (pos_ < input_.size() && input_[pos_] == '-') {
            negative = true;
            ++pos_;
        }
        if (pos_ >= input_.size() || !std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
            return fail("应为整数");
        }
        int64_t value = 0;
        while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
            value = value * 10 + (input_[pos_++] - '0');
            if (value > static_cast<int64_t>(std::numeric_limits<int>::max()) + 1) {
                return fail("整数超出范围");
            }
        }
        if (pos_ < input_.size() && (input_[pos_] == '.' || input_[pos_] == 'e' ||
                                     input_[pos_] == 'E')) return fail("此字段必须是整数");
        if (negative) value = -value;
        if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()) {
            return fail("整数超出范围");
        }
        out = static_cast<int>(value);
        return true;
    }

    bool unsigned_integer(uint32_t& out)
    {
        whitespace();
        if (pos_ >= input_.size() || !std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
            return fail("应为非负整数");
        }
        uint64_t value = 0;
        while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
            value = value * 10 + static_cast<uint64_t>(input_[pos_++] - '0');
            if (value > std::numeric_limits<uint32_t>::max()) return fail("非负整数超出范围");
        }
        if (pos_ < input_.size() && (input_[pos_] == '.' || input_[pos_] == 'e' ||
                                     input_[pos_] == 'E')) return fail("此字段必须是整数");
        out = static_cast<uint32_t>(value);
        return true;
    }

    bool skip_number()
    {
        whitespace();
        size_t start = pos_;
        if (pos_ < input_.size() && input_[pos_] == '-') ++pos_;
        if (pos_ >= input_.size()) return fail("数字不完整");
        if (input_[pos_] == '0') {
            ++pos_;
        } else {
            if (!std::isdigit(static_cast<unsigned char>(input_[pos_]))) return fail("数字无效");
            while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) ++pos_;
        }
        if (pos_ < input_.size() && input_[pos_] == '.') {
            ++pos_;
            if (pos_ >= input_.size() || !std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
                return fail("小数无效");
            }
            while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) ++pos_;
        }
        if (pos_ < input_.size() && (input_[pos_] == 'e' || input_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < input_.size() && (input_[pos_] == '+' || input_[pos_] == '-')) ++pos_;
            if (pos_ >= input_.size() || !std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
                return fail("指数无效");
            }
            while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) ++pos_;
        }
        return pos_ > start;
    }

    bool skip_value(int depth = 0)
    {
        if (depth > 32) return fail("JSON 嵌套超过 32 层");
        whitespace();
        if (pos_ >= input_.size()) return fail("缺少 JSON 值");
        char ch = input_[pos_];
        if (ch == '"') {
            std::string ignored;
            return string(ignored);
        }
        if (ch == '{') {
            ++pos_;
            whitespace();
            if (pos_ < input_.size() && input_[pos_] == '}') { ++pos_; return true; }
            while (true) {
                std::string key;
                if (!string(key) || !take(':') || !skip_value(depth + 1)) return false;
                whitespace();
                if (pos_ < input_.size() && input_[pos_] == '}') { ++pos_; return true; }
                if (!take(',')) return false;
            }
        }
        if (ch == '[') {
            ++pos_;
            whitespace();
            if (pos_ < input_.size() && input_[pos_] == ']') { ++pos_; return true; }
            while (true) {
                if (!skip_value(depth + 1)) return false;
                whitespace();
                if (pos_ < input_.size() && input_[pos_] == ']') { ++pos_; return true; }
                if (!take(',')) return false;
            }
        }
        if (ch == 't') return literal("true");
        if (ch == 'f') return literal("false");
        if (ch == 'n') return literal("null");
        return skip_number();
    }

    bool modem(ModemConfig& out)
    {
        if (!take('{')) return false;
        whitespace();
        if (pos_ < input_.size() && input_[pos_] == '}') { ++pos_; return true; }
        while (true) {
            std::string key;
            if (!string(key) || !take(':')) return false;
            if (key == "driver") { if (!string(out.driver)) return false; }
            else if (key == "endpoint") { if (!string(out.endpoint)) return false; }
            else if (key == "baud") { if (!integer(out.baud)) return false; }
            else if (key == "pollIntervalMs") { if (!integer(out.pollIntervalMs)) return false; }
            else if (!skip_value()) return false;
            whitespace();
            if (pos_ < input_.size() && input_[pos_] == '}') { ++pos_; break; }
            if (!take(',')) return false;
        }
        if (out.driver.empty()) return fail("modem.driver 不能为空");
        if (out.baud < 1200 || out.baud > 4000000) return fail("modem.baud 超出范围");
        if (out.pollIntervalMs < 100 || out.pollIntervalMs > 3600000) {
            return fail("modem.pollIntervalMs 超出范围");
        }
        return true;
    }

    bool channel(PushChannel& out)
    {
        if (!take('{')) return false;
        whitespace();
        if (pos_ < input_.size() && input_[pos_] == '}') { ++pos_; return true; }
        while (true) {
            std::string key;
            if (!string(key) || !take(':')) return false;
            if (key == "enabled") { if (!boolean(out.enabled)) return false; }
            else if (key == "type") {
                int type = 0;
                if (!integer(type)) return false;
                if (type < PUSH_TYPE_NONE || type > PUSH_TYPE_TELEGRAM) {
                    return fail("pushChannels.type 超出范围");
                }
                out.type = static_cast<uint8_t>(type);
            } else if (key == "name") { if (!string(out.name)) return false; }
            else if (key == "url") { if (!string(out.url)) return false; }
            else if (key == "key1") { if (!string(out.key1)) return false; }
            else if (key == "key2") { if (!string(out.key2)) return false; }
            else if (key == "customBody") { if (!string(out.customBody)) return false; }
            else if (!skip_value()) return false;
            whitespace();
            if (pos_ < input_.size() && input_[pos_] == '}') { ++pos_; return true; }
            if (!take(',')) return false;
        }
    }

    bool channels(std::array<PushChannel, MAX_PUSH_CHANNELS>& out)
    {
        if (!take('[')) return false;
        out = {};
        whitespace();
        if (pos_ < input_.size() && input_[pos_] == ']') { ++pos_; return true; }
        size_t count = 0;
        while (true) {
            if (count >= out.size()) return fail("pushChannels 最多 5 个");
            if (!channel(out[count++])) return false;
            whitespace();
            if (pos_ < input_.size() && input_[pos_] == ']') { ++pos_; return true; }
            if (!take(',')) return false;
        }
    }

    bool email(EmailConfig& out)
    {
        if (!take('{')) return false;
        whitespace();
        if (pos_ < input_.size() && input_[pos_] == '}') { ++pos_; return true; }
        while (true) {
            std::string key;
            if (!string(key) || !take(':')) return false;
            if (key == "enabled") { if (!boolean(out.enabled)) return false; }
            else if (key == "server") { if (!string(out.server)) return false; }
            else if (key == "port") { if (!integer(out.port)) return false; }
            else if (key == "username") { if (!string(out.username)) return false; }
            else if (key == "password") { if (!string(out.password)) return false; }
            else if (key == "recipient") { if (!string(out.recipient)) return false; }
            else if (!skip_value()) return false;
            whitespace();
            if (pos_ < input_.size() && input_[pos_] == '}') { ++pos_; break; }
            if (!take(',')) return false;
        }
        if (out.port < 1 || out.port > 65535) return fail("email.port 超出范围");
        return true;
    }

    bool storage(StorageConfig& out)
    {
        if (!take('{')) return false;
        whitespace();
        if (pos_ < input_.size() && input_[pos_] == '}') { ++pos_; return true; }
        while (true) {
            std::string key;
            if (!string(key) || !take(':')) return false;
            if (key == "inboxFile") { if (!string(out.inboxFile)) return false; }
            else if (!skip_value()) return false;
            whitespace();
            if (pos_ < input_.size() && input_[pos_] == '}') { ++pos_; return true; }
            if (!take(',')) return false;
        }
    }

    bool wifi(WifiConfig& out)
    {
        if (!take('{')) return false;
        whitespace();
        if (pos_ < input_.size() && input_[pos_] == '}') { ++pos_; return true; }
        while (true) {
            std::string key;
            if (!string(key) || !take(':')) return false;
            if (key == "ssid") { if (!string(out.ssid)) return false; }
            else if (key == "password") { if (!string(out.password)) return false; }
            else if (!skip_value()) return false;
            whitespace();
            if (pos_ < input_.size() && input_[pos_] == '}') { ++pos_; return true; }
            if (!take(',')) return false;
        }
    }

    bool web(WebConfig& out)
    {
        if (!take('{')) return false;
        whitespace();
        if (pos_ < input_.size() && input_[pos_] == '}') { ++pos_; return true; }
        while (true) {
            std::string key;
            if (!string(key) || !take(':')) return false;
            if (key == "username") { if (!string(out.username)) return false; }
            else if (key == "password") { if (!string(out.password)) return false; }
            else if (key == "listenAddress") { if (!string(out.listenAddress)) return false; }
            else if (key == "port") { if (!integer(out.port)) return false; }
            else if (key == "assetRoot") { if (!string(out.assetRoot)) return false; }
            else if (!skip_value()) return false;
            whitespace();
            if (pos_ < input_.size() && input_[pos_] == '}') { ++pos_; break; }
            if (!take(',')) return false;
        }
        if (out.port < 1 || out.port > 65535) return fail("web.port 超出范围");
        return true;
    }

    bool time_config(TimeConfig& out)
    {
        if (!take('{')) return false;
        whitespace();
        if (pos_ < input_.size() && input_[pos_] == '}') { ++pos_; return true; }
        while (true) {
            std::string key;
            if (!string(key) || !take(':')) return false;
            if (key == "timezoneOffsetMin") { if (!integer(out.timezoneOffsetMin)) return false; }
            else if (key == "ntpServer") { if (!string(out.ntpServer)) return false; }
            else if (key == "rebootEnabled") { if (!boolean(out.rebootEnabled)) return false; }
            else if (key == "rebootHour") { if (!integer(out.rebootHour)) return false; }
            else if (key == "heartbeatEnabled") { if (!boolean(out.heartbeatEnabled)) return false; }
            else if (key == "heartbeatHour") { if (!integer(out.heartbeatHour)) return false; }
            else if (!skip_value()) return false;
            whitespace();
            if (pos_ < input_.size() && input_[pos_] == '}') { ++pos_; break; }
            if (!take(',')) return false;
        }
        if (out.timezoneOffsetMin < -720 || out.timezoneOffsetMin > 840) return fail("time.timezoneOffsetMin 超出范围");
        if (out.rebootHour < 0 || out.rebootHour > 23) return fail("time.rebootHour 超出范围");
        if (out.heartbeatHour < 0 || out.heartbeatHour > 23) return fail("time.heartbeatHour 超出范围");
        return true;
    }

    bool keepalive(KeepaliveConfig& out)
    {
        if (!take('{')) return false;
        whitespace();
        if (pos_ < input_.size() && input_[pos_] == '}') { ++pos_; return true; }
        while (true) {
            std::string key;
            if (!string(key) || !take(':')) return false;
            if (key == "enabled") { if (!boolean(out.enabled)) return false; }
            else if (key == "intervalDays") { if (!integer(out.intervalDays)) return false; }
            else if (key == "action") {
                int action = 0;
                if (!integer(action)) return false;
                if (action < 0 || action > 3) return fail("keepalive.action 超出范围");
                out.action = static_cast<uint8_t>(action);
            } else if (key == "target") { if (!string(out.target)) return false; }
            else if (key == "url") { if (!string(out.url)) return false; }
            else if (key == "profile") { if (!string(out.profile)) return false; }
            else if (key == "lastRun") { if (!unsigned_integer(out.lastRun)) return false; }
            else if (!skip_value()) return false;
            whitespace();
            if (pos_ < input_.size() && input_[pos_] == '}') { ++pos_; break; }
            if (!take(',')) return false;
        }
        if (out.intervalDays < 1 || out.intervalDays > 3650) return fail("keepalive.intervalDays 超出范围");
        return true;
    }

    bool cellular(CellularConfig& out)
    {
        if (!take('{')) return false;
        whitespace();
        if (pos_ < input_.size() && input_[pos_] == '}') { ++pos_; return true; }
        while (true) {
            std::string key;
            if (!string(key) || !take(':')) return false;
            if (key == "dataEnabled") { if (!boolean(out.dataEnabled)) return false; }
            else if (key == "roamingEnabled") { if (!boolean(out.roamingEnabled)) return false; }
            else if (key == "apn") { if (!string(out.apn)) return false; }
            else if (key == "operatorPlmn") { if (!string(out.operatorPlmn)) return false; }
            else if (key == "netLedEnabled") { if (!boolean(out.netLedEnabled)) return false; }
            else if (!skip_value()) return false;
            whitespace();
            if (pos_ < input_.size() && input_[pos_] == '}') { ++pos_; return true; }
            if (!take(',')) return false;
        }
    }

    bool scheduled_task(ScheduledTaskConfig& out)
    {
        if (!take('{')) return false;
        whitespace();
        if (pos_ < input_.size() && input_[pos_] == '}') { ++pos_; return true; }
        while (true) {
            std::string key;
            if (!string(key) || !take(':')) return false;
            if (key == "enabled") { if (!boolean(out.enabled)) return false; }
            else if (key == "name") { if (!string(out.name)) return false; }
            else if (key == "profile") { if (!string(out.profile)) return false; }
            else if (key == "switchBack") { if (!boolean(out.switchBack)) return false; }
            else if (key == "intervalDays") { if (!integer(out.intervalDays)) return false; }
            else if (key == "action") {
                int action = 0;
                if (!integer(action)) return false;
                if (action < 0 || action > 3) return fail("scheduledTasks.action 超出范围");
                out.action = static_cast<uint8_t>(action);
            } else if (key == "target") { if (!string(out.target)) return false; }
            else if (key == "payload") { if (!string(out.payload)) return false; }
            else if (key == "lastRun") { if (!unsigned_integer(out.lastRun)) return false; }
            else if (!skip_value()) return false;
            whitespace();
            if (pos_ < input_.size() && input_[pos_] == '}') { ++pos_; break; }
            if (!take(',')) return false;
        }
        if (out.intervalDays < 1 || out.intervalDays > 3650) return fail("scheduledTasks.intervalDays 超出范围");
        return true;
    }

    bool scheduled_tasks(std::array<ScheduledTaskConfig, MAX_SCHEDULED_TASKS>& out)
    {
        if (!take('[')) return false;
        out = {};
        whitespace();
        if (pos_ < input_.size() && input_[pos_] == ']') { ++pos_; return true; }
        size_t count = 0;
        while (true) {
            if (count >= out.size()) return fail("scheduledTasks 最多 6 个");
            if (!scheduled_task(out[count++])) return false;
            whitespace();
            if (pos_ < input_.size() && input_[pos_] == ']') { ++pos_; return true; }
            if (!take(',')) return false;
        }
    }

    bool root(AppConfig& out, bool& saw_version)
    {
        if (!take('{')) return false;
        whitespace();
        if (pos_ < input_.size() && input_[pos_] == '}') { ++pos_; return true; }
        while (true) {
            std::string key;
            if (!string(key) || !take(':')) return false;
            if (key == "schemaVersion") {
                if (!integer(out.schemaVersion)) return false;
                saw_version = true;
            } else if (key == "modem") {
                out.presence.modem = true; if (!modem(out.modem)) return false;
            } else if (key == "receiver") {
                out.presence.receiver = true; if (!string(out.receiver)) return false;
            } else if (key == "adminPhone") {
                out.presence.adminPhone = true; if (!string(out.adminPhone)) return false;
            } else if (key == "numberBlacklist") {
                out.presence.numberBlacklist = true; if (!string(out.numberBlacklist)) return false;
            } else if (key == "forwardRules") {
                out.presence.forwardRules = true; if (!string(out.forwardRules)) return false;
            } else if (key == "callNotifyEnabled") {
                out.presence.callNotifyEnabled = true; if (!boolean(out.callNotifyEnabled)) return false;
            } else if (key == "pushEnabled") {
                out.presence.pushEnabled = true; if (!boolean(out.pushEnabled)) return false;
            } else if (key == "pushChannels") {
                out.presence.pushChannels = true; if (!channels(out.pushChannels)) return false;
            } else if (key == "email") {
                out.presence.email = true; if (!email(out.email)) return false;
            } else if (key == "storage") {
                out.presence.storage = true; if (!storage(out.storage)) return false;
            } else if (key == "wifi") {
                out.presence.wifi = true; if (!wifi(out.wifi)) return false;
            } else if (key == "web") {
                out.presence.web = true; if (!web(out.web)) return false;
            } else if (key == "time") {
                out.presence.time = true; if (!time_config(out.time)) return false;
            } else if (key == "keepalive") {
                out.presence.keepalive = true; if (!keepalive(out.keepalive)) return false;
            } else if (key == "cellular") {
                out.presence.cellular = true; if (!cellular(out.cellular)) return false;
            } else if (key == "scheduledTasks") {
                out.presence.scheduledTasks = true; if (!scheduled_tasks(out.scheduledTasks)) return false;
            }
            else if (!skip_value()) return false;
            whitespace();
            if (pos_ < input_.size() && input_[pos_] == '}') { ++pos_; return true; }
            if (!take(',')) return false;
        }
    }

    const std::string& input_;
    std::string* error_ = nullptr;
    size_t pos_ = 0;
};

void quoted(std::string& out, const std::string& value)
{
    out += '"';
    json_escape_append(out, value);
    out += '"';
}

void prop(std::string& out, const char* key, const std::string& value)
{
    quoted(out, key);
    out += ':';
    quoted(out, value);
}

void bool_prop(std::string& out, const char* key, bool value)
{
    quoted(out, key);
    out += value ? ":true" : ":false";
}

void int_prop(std::string& out, const char* key, int value)
{
    quoted(out, key);
    out += ':' + std::to_string(value);
}

void uint_prop(std::string& out, const char* key, uint32_t value)
{
    quoted(out, key);
    out += ':' + std::to_string(value);
}

}  // namespace

bool parse_config_json(const std::string& json, AppConfig& out, std::string* error)
{
    if (error) error->clear();
    JsonParser parser(json, error);
    return parser.parse(out);
}

std::string serialize_config_json(const AppConfig& config)
{
    std::string out;
    out.reserve(4096);
    out += '{';
    int_prop(out, "schemaVersion", CONFIG_SCHEMA_VERSION);
    out += ",\"modem\":{";
    prop(out, "driver", config.modem.driver);
    out += ','; prop(out, "endpoint", config.modem.endpoint);
    out += ','; int_prop(out, "baud", config.modem.baud);
    out += ','; int_prop(out, "pollIntervalMs", config.modem.pollIntervalMs);
    out += "},"; prop(out, "receiver", config.receiver);
    out += ','; prop(out, "adminPhone", config.adminPhone);
    out += ','; prop(out, "numberBlacklist", config.numberBlacklist);
    out += ','; prop(out, "forwardRules", config.forwardRules);
    out += ','; bool_prop(out, "callNotifyEnabled", config.callNotifyEnabled);
    out += ','; bool_prop(out, "pushEnabled", config.pushEnabled);
    out += ",\"pushChannels\":[";
    for (size_t i = 0; i < config.pushChannels.size(); ++i) {
        if (i) out += ',';
        const PushChannel& channel = config.pushChannels[i];
        out += '{';
        bool_prop(out, "enabled", channel.enabled);
        out += ','; int_prop(out, "type", channel.type);
        out += ','; prop(out, "name", channel.name);
        out += ','; prop(out, "url", channel.url);
        out += ','; prop(out, "key1", channel.key1);
        out += ','; prop(out, "key2", channel.key2);
        out += ','; prop(out, "customBody", channel.customBody);
        out += '}';
    }
    out += "],\"email\":{";
    bool_prop(out, "enabled", config.email.enabled);
    out += ','; prop(out, "server", config.email.server);
    out += ','; int_prop(out, "port", config.email.port);
    out += ','; prop(out, "username", config.email.username);
    out += ','; prop(out, "password", config.email.password);
    out += ','; prop(out, "recipient", config.email.recipient);
    out += "},\"storage\":{";
    prop(out, "inboxFile", config.storage.inboxFile);
    out += "},\"wifi\":{";
    prop(out, "ssid", config.wifi.ssid);
    out += ','; prop(out, "password", config.wifi.password);
    out += "},\"web\":{";
    prop(out, "username", config.web.username);
    out += ','; prop(out, "password", config.web.password);
    out += ','; prop(out, "listenAddress", config.web.listenAddress);
    out += ','; int_prop(out, "port", config.web.port);
    out += ','; prop(out, "assetRoot", config.web.assetRoot);
    out += "},\"time\":{";
    int_prop(out, "timezoneOffsetMin", config.time.timezoneOffsetMin);
    out += ','; prop(out, "ntpServer", config.time.ntpServer);
    out += ','; bool_prop(out, "rebootEnabled", config.time.rebootEnabled);
    out += ','; int_prop(out, "rebootHour", config.time.rebootHour);
    out += ','; bool_prop(out, "heartbeatEnabled", config.time.heartbeatEnabled);
    out += ','; int_prop(out, "heartbeatHour", config.time.heartbeatHour);
    out += "},\"keepalive\":{";
    bool_prop(out, "enabled", config.keepalive.enabled);
    out += ','; int_prop(out, "intervalDays", config.keepalive.intervalDays);
    out += ','; int_prop(out, "action", config.keepalive.action);
    out += ','; prop(out, "target", config.keepalive.target);
    out += ','; prop(out, "url", config.keepalive.url);
    out += ','; prop(out, "profile", config.keepalive.profile);
    out += ','; uint_prop(out, "lastRun", config.keepalive.lastRun);
    out += "},\"cellular\":{";
    bool_prop(out, "dataEnabled", config.cellular.dataEnabled);
    out += ','; bool_prop(out, "roamingEnabled", config.cellular.roamingEnabled);
    out += ','; prop(out, "apn", config.cellular.apn);
    out += ','; prop(out, "operatorPlmn", config.cellular.operatorPlmn);
    out += ','; bool_prop(out, "netLedEnabled", config.cellular.netLedEnabled);
    out += "},\"scheduledTasks\":[";
    for (size_t i = 0; i < config.scheduledTasks.size(); ++i) {
        if (i) out += ',';
        const ScheduledTaskConfig& task = config.scheduledTasks[i];
        out += '{';
        bool_prop(out, "enabled", task.enabled);
        out += ','; prop(out, "name", task.name);
        out += ','; prop(out, "profile", task.profile);
        out += ','; bool_prop(out, "switchBack", task.switchBack);
        out += ','; int_prop(out, "intervalDays", task.intervalDays);
        out += ','; int_prop(out, "action", task.action);
        out += ','; prop(out, "target", task.target);
        out += ','; prop(out, "payload", task.payload);
        out += ','; uint_prop(out, "lastRun", task.lastRun);
        out += '}';
    }
    out += "]}";
    return out;
}

}  // namespace omnisms
