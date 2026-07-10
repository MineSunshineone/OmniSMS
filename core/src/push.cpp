#include "omnisms/push.h"

#include <cctype>

#include "omnisms/crypto.h"
#include "omnisms/text.h"

namespace omnisms {

namespace {

constexpr uint32_t PUSH_RETRY_BASE_SEC = 20;
constexpr uint32_t PUSH_RETRY_MAX_SEC = 600;

bool is_url_like(const std::string& value)
{
    return value.rfind("http://", 0) == 0 || value.rfind("https://", 0) == 0;
}

size_t url_path_start(const std::string& url)
{
    size_t scheme = url.find("://");
    size_t host = scheme == std::string::npos ? 0 : scheme + 3;
    return url.find('/', host);
}

std::string url_path_without_query(const std::string& url)
{
    size_t start = url_path_start(url);
    if (start == std::string::npos) return {};
    size_t end = url.find_first_of("?#", start);
    return url.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

bool bark_url_path_is_push(const std::string& url)
{
    std::string path = url_path_without_query(url);
    while (path.size() > 1 && path.back() == '/') path.pop_back();
    return path == "/push" || (path.size() > 5 && path.compare(path.size() - 5, 5, "/push") == 0);
}

bool bark_url_has_key_path(const std::string& url)
{
    std::string path = url_path_without_query(url);
    while (path.size() > 1 && path.back() == '/') path.pop_back();
    return !path.empty() && path != "/" && !bark_url_path_is_push(url);
}

std::string bark_push_endpoint(std::string base)
{
    size_t suffix_pos = base.find_first_of("?#");
    std::string suffix;
    if (suffix_pos != std::string::npos) {
        suffix = base.substr(suffix_pos);
        base.erase(suffix_pos);
    }
    while (!base.empty() && base.back() == '/') base.pop_back();
    return base + "/push" + suffix;
}

struct BarkTarget {
    bool ok = false;
    std::string url;
    std::string device_key;
};

BarkTarget bark_target_from_channel(const PushChannel& ch)
{
    BarkTarget target;
    std::string key = trim(ch.key1);
    std::string raw_url = trim(ch.url);
    if (!key.empty()) {
        if (raw_url.empty()) raw_url = "https://api.day.app";
        if (!is_url_like(raw_url)) return target;
        target.url = bark_url_path_is_push(raw_url) ? raw_url : bark_push_endpoint(raw_url);
        target.device_key = key;
        target.ok = true;
        return target;
    }

    // 兼容旧配置：URL 框里直接填 https://api.day.app/<key> 时仍按原端点发送。
    if (raw_url.empty() || !is_url_like(raw_url) || !bark_url_has_key_path(raw_url)) return target;
    target.url = raw_url;
    target.ok = true;
    return target;
}

bool is_integer_literal(const std::string& value)
{
    if (value.empty()) return false;
    size_t i = (value[0] == '-' || value[0] == '+') ? 1 : 0;
    if (i >= value.size()) return false;
    for (; i < value.size(); ++i) {
        if (!isdigit(static_cast<unsigned char>(value[i]))) return false;
    }
    return true;
}

bool bark_numeric_param(const std::string& key)
{
    return key == "badge" || key == "volume" || key == "ttl";
}

bool bark_reserved_param(const std::string& key)
{
    return key == "title" || key == "body" || key == "device_key" || key == "device_keys";
}

void append_bark_params(std::string& json, const std::string& params,
                        const std::string& sender, const std::string& text,
                        const std::string& timestamp, const std::string& receiver)
{
    std::string spec = trim(params);
    if (!spec.empty() && spec[0] == '?') spec.erase(0, 1);
    size_t pos = 0;
    while (pos <= spec.size()) {
        size_t end = spec.find_first_of("&\n", pos);
        if (end == std::string::npos) end = spec.size();
        std::string item = trim(spec.substr(pos, end - pos));
        pos = end + (end < spec.size() ? 1 : 0);
        if (item.empty()) {
            if (end == spec.size()) break;
            continue;
        }

        size_t eq = item.find('=');
        std::string key = trim(url_decode_component(eq == std::string::npos ? item : item.substr(0, eq)));
        if (key.empty() || bark_reserved_param(key)) {
            if (end == spec.size()) break;
            continue;
        }

        std::string value = eq == std::string::npos ? "1" : url_decode_component(item.substr(eq + 1));
        value = apply_push_placeholders(trim(value), sender, text, timestamp, receiver);
        json += ",";
        json += "\"";
        json_escape_append(json, key);
        if (bark_numeric_param(key) && is_integer_literal(value)) {
            json += "\":";
            json += value;
        } else {
            json += "\":\"";
            json_escape_append(json, value);
            json += "\"";
        }

        if (end == spec.size()) break;
    }
}

}  // namespace

bool channel_valid(const PushChannel& ch)
{
    if (!ch.enabled || ch.type == PUSH_TYPE_NONE) return false;
    if (ch.type == PUSH_TYPE_BARK) return bark_target_from_channel(ch).ok;
    bool needs_url = ch.type == PUSH_TYPE_POST_JSON || ch.type == PUSH_TYPE_GET || ch.type == PUSH_TYPE_DINGTALK ||
                     ch.type == PUSH_TYPE_CUSTOM || ch.type == PUSH_TYPE_FEISHU ||
                     ch.type == PUSH_TYPE_GOTIFY;
    if (needs_url && ch.url.empty()) return false;
    if (ch.type == PUSH_TYPE_CUSTOM && ch.customBody.empty()) return false;
    if (ch.type == PUSH_TYPE_PUSHPLUS && ch.key1.empty()) return false;
    if (ch.type == PUSH_TYPE_SERVERCHAN && ch.key1.empty() && ch.url.empty()) return false;
    if (ch.type == PUSH_TYPE_GOTIFY && ch.key1.empty()) return false;
    if (ch.type == PUSH_TYPE_TELEGRAM && (ch.key1.empty() || ch.key2.empty())) return false;
    return true;
}

bool build_push_request(const PushChannel& channel, const SmsEvent& ev, bool notify,
                        int64_t now_epoch_ms, HttpRequest& out)
{
    if (!channel_valid(channel)) return false;

    const std::string& sender = ev.sender;
    const std::string& text = ev.text;
    const std::string& timestamp = ev.timestamp;
    std::string receiver = notify ? std::string() : ev.receiver;
    std::string sender_json = json_escape(sender);
    std::string text_json = json_escape(text);
    std::string ts_json = json_escape(timestamp);
    std::string receiver_json = json_escape(receiver);
    std::string receiver_line_json = receiver.empty() ? std::string() : ("\\n本机号码: " + receiver_json);
    std::string receiver_block_json = receiver.empty() ? std::string() : ("\\n\\n本机号码: " + receiver_json);
    std::string receiver_html_json = receiver.empty() ? std::string() : ("<br><b>本机号码:</b> " + json_escape(html_escape(receiver)));
    std::string time_line_json = "\\n时间: " + ts_json;
    std::string time_block_json = "\\n\\n时间: " + ts_json;
    std::string time_html_json = "<br><b>时间:</b> " + ts_json;
    // 自定义提醒(notify)：sender 里放的是任务名，直接当标题用
    std::string title = notify ? sender : ("短信来自: " + sender);
    std::string title_json = json_escape(title);
    std::string url;
    std::string body;
    const char* content_type = "application/json";
    const char* method = "POST";

    switch (channel.type) {
        case PUSH_TYPE_POST_JSON:
            url = channel.url;
            body = "{\"sender\":\"" + sender_json + "\",\"receiver\":\"" + receiver_json +
                   "\",\"message\":\"" + text_json + "\",\"timestamp\":\"" + ts_json + "\"}";
            break;
        case PUSH_TYPE_BARK: {
            BarkTarget bark = bark_target_from_channel(channel);
            if (!bark.ok) return false;
            url = bark.url;
            body = "{";
            if (!bark.device_key.empty()) {
                json_prop(body, "device_key", bark.device_key);
                body += ",";
            }
            json_prop(body, "title", title);
            body += ",";
            json_prop(body, "body", text + (receiver.empty() ? std::string() : ("\n\n本机号码: " + receiver)) +
                                   "\n\n时间: " + timestamp);
            append_bark_params(body, channel.key2, sender, text, timestamp, receiver);
            body += "}";
            break;
        }
        case PUSH_TYPE_GET:
            method = "GET";
            url = channel.url + (channel.url.find('?') == std::string::npos ? "?" : "&") +
                  "sender=" + url_encode(sender) + "&receiver=" + url_encode(receiver) +
                  "&message=" + url_encode(text) + "&timestamp=" + url_encode(timestamp);
            break;
        case PUSH_TYPE_DINGTALK: {
            url = channel.url;
            if (!channel.key1.empty()) {
                std::string sign_data = std::to_string(now_epoch_ms) + "\n" + channel.key1;
                std::string sign = url_encode(hmac_sha256_base64(sign_data, channel.key1));
                url += (url.find('?') == std::string::npos ? "?" : "&");
                url += "timestamp=" + std::to_string(now_epoch_ms) + "&sign=" + sign;
            }
            body = notify
                ? ("{\"msgtype\":\"text\",\"text\":{\"content\":\"" + title_json + "\\n" +
                   text_json + time_line_json + "\"}}")
                : ("{\"msgtype\":\"text\",\"text\":{\"content\":\"短信通知\\n发送者: " +
                   sender_json + receiver_line_json + "\\n内容: " + text_json + time_line_json + "\"}}");
            break;
        }
        case PUSH_TYPE_PUSHPLUS: {
            url = channel.url.empty() ? "https://www.pushplus.plus/send" : channel.url;
            std::string push_channel = channel.key2.empty() ? "wechat" : channel.key2;
            if (push_channel != "wechat" && push_channel != "extension" && push_channel != "app") push_channel = "wechat";
            std::string text_html = json_escape(html_escape(text));
            std::string sender_html = json_escape(html_escape(sender));
            std::string pp_content = notify
                ? (text_html + time_html_json)
                : ("<b>发送者:</b> " + sender_html + receiver_html_json + time_html_json + "<br><b>内容:</b><br>" + text_html);
            body = "{\"token\":\"" + json_escape(channel.key1) + "\",\"title\":\"" + title_json +
                   "\",\"content\":\"" + pp_content + "\",\"channel\":\"" + push_channel + "\"}";
            break;
        }
        case PUSH_TYPE_SERVERCHAN: {
            std::string send_key = trim(channel.key1);
            std::string base = trim(channel.url);
            if (send_key.empty() && !base.empty() && !is_url_like(base)) {
                send_key = base;
                base.clear();
            }
            if (base.empty()) base = "https://sctapi.ftqq.com";
            if (is_url_like(send_key)) {
                base = send_key;
                send_key.clear();
            }
            while (!base.empty() && base.back() == '/') base.pop_back();
            url = base.find(".send") != std::string::npos ? base : (base + "/" + send_key + ".send");
            content_type = "application/x-www-form-urlencoded";
            std::string sc_desp = notify
                ? ("**时间:** " + timestamp + "\n\n" + text)
                : ("**发送者:** " + sender +
                   (receiver.empty() ? std::string() : ("\n\n**本机号码:** " + receiver)) +
                   "\n\n**时间:** " + timestamp + "\n\n**内容:**\n\n" + text);
            body = "title=" + url_encode(title) + "&desp=" + url_encode(sc_desp);
            break;
        }
        case PUSH_TYPE_CUSTOM:
            url = channel.url;
            body = apply_push_placeholders(channel.customBody, sender_json, text_json, ts_json, receiver_json);
            break;
        case PUSH_TYPE_FEISHU: {
            url = channel.url;
            body = "{";
            if (!channel.key1.empty()) {
                int64_t ts = now_epoch_ms / 1000;
                // 飞书签名与钉钉相反: 以 ts+"\n"+secret 为密钥、空串为消息做 HMAC-SHA256
                std::string sign = hmac_sha256_base64("", std::to_string(ts) + "\n" + channel.key1);
                body += "\"timestamp\":\"" + std::to_string(ts) + "\",\"sign\":\"" + sign + "\",";
            }
            body += notify
                ? ("\"msg_type\":\"text\",\"content\":{\"text\":\"" + title_json + "\\n" +
                   text_json + time_line_json + "\"}}")
                : ("\"msg_type\":\"text\",\"content\":{\"text\":\"短信通知\\n发送者: " +
                   sender_json + receiver_line_json + "\\n内容: " + text_json + time_line_json + "\"}}");
            break;
        }
        case PUSH_TYPE_GOTIFY:
            url = channel.url;
            if (!url.empty() && url.back() != '/') url += "/";
            url += "message?token=" + url_encode(channel.key1);
            body = "{\"title\":\"" + title_json + "\",\"message\":\"" + text_json +
                   receiver_block_json + time_block_json + "\",\"priority\":5}";
            break;
        case PUSH_TYPE_TELEGRAM: {
            std::string base = channel.url.empty() ? "https://api.telegram.org" : channel.url;
            while (!base.empty() && base.back() == '/') base.pop_back();
            url = base + "/bot" + channel.key2 + "/sendMessage";
            body = notify
                ? ("{\"chat_id\":\"" + json_escape(channel.key1) + "\",\"text\":\"" + title_json + "\\n" +
                   text_json + time_line_json + "\"}")
                : ("{\"chat_id\":\"" + json_escape(channel.key1) + "\",\"text\":\"短信通知\\n发送者: " +
                   sender_json + receiver_line_json + "\\n内容: " + text_json + time_line_json + "\"}");
            break;
        }
        default:
            return false;
    }

    out.method = method;
    out.url = std::move(url);
    out.content_type = content_type;
    out.body = std::move(body);
    return true;
}

uint32_t backoff_seconds(uint8_t attempts, uint32_t seed)
{
    uint32_t step = PUSH_RETRY_BASE_SEC;
    for (uint8_t i = 1; i < attempts && step < PUSH_RETRY_MAX_SEC; ++i) step <<= 1;
    if (step > PUSH_RETRY_MAX_SEC) step = PUSH_RETRY_MAX_SEC;
    uint32_t jitter = step / 4;
    return step + (jitter ? (seed % (jitter + 1)) : 0);
}

}  // namespace omnisms
