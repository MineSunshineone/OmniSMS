// OmniSMS Linux 端口
// 串口 AT(PDU 模式)轮询收短信 → core 解码/黑名单/规则 → 多通道推送(带退避重试)。
// 适用：树莓派/随身WiFi(Debian) + AT 串口模组。ModemManager 传输后续里程碑接入。
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <curl/curl.h>

#include "omnisms/admin.h"
#include "omnisms/call.h"
#include "omnisms/config.h"
#include "omnisms/crypto.h"
#include "omnisms/email.h"
#include "omnisms/inbox.h"
#include "omnisms/linux/config_store.h"
#include "omnisms/linux/modem_backend.h"
#include "omnisms/linux/web_server.h"
#include "omnisms/pdu.h"
#include "omnisms/phone.h"
#include "omnisms/push.h"
#include "omnisms/queue.h"
#include "omnisms/rules.h"
#include "omnisms/scheduler.h"
#include "omnisms/sms.h"
#include "omnisms/text.h"

using namespace omnisms;
using namespace omnisms::linux_port;

// ===== 配置 =====
// 推送通道键名与固件配置导出格式一致(push0en/push0type/push0url/push0name/
// push0k1/push0k2/push0body … push4*)——设备导出的配置可直接粘贴复用。
struct Config {
    std::string modem_driver = "generic-3gpp";
    std::string device = "/dev/ttyUSB2";
    int baud = 115200;
    int poll_interval_ms = 3000;
    std::string receiver;
    std::string admin_phone;
    std::string blacklist;
    std::string rules;
    bool callNotifyEnabled = true;
    bool pushEnabled = true;
    PushChannel channels[MAX_PUSH_CHANNELS];
    // 邮件(键名与固件导出一致)；465=SMTPS，其他端口 STARTTLS
    bool emailEnabled = false;
    std::string smtpServer;
    int smtpPort = 465;
    std::string smtpUser;
    std::string smtpPass;
    std::string smtpSendTo;
    // 收件箱持久化(JSONL 追加)；空 = 不落盘
    std::string inbox_file;
    std::string web_listen_address = "127.0.0.1";
    int web_port = 8080;
    std::string web_asset_root;
    std::string web_username = "admin";
    std::string web_password = "admin123";
    AppConfig portable;
};

static std::string default_web_asset_root()
{
    std::ifstream local_installed("/usr/local/share/omnisms/webui/index.html");
    if (local_installed.good()) return "/usr/local/share/omnisms/webui";
    std::ifstream installed("/usr/share/omnisms/webui/index.html");
    if (installed.good()) return "/usr/share/omnisms/webui";
#ifdef OMNISMS_WEBUI_SOURCE_DIR
    return OMNISMS_WEBUI_SOURCE_DIR;
#else
    return "webui";
#endif
}

static void sync_portable_config(Config& cfg)
{
    AppConfig& out = cfg.portable;
    out.modem.driver = cfg.modem_driver;
    out.modem.endpoint = cfg.device;
    out.modem.baud = cfg.baud;
    out.modem.pollIntervalMs = cfg.poll_interval_ms;
    out.receiver = cfg.receiver;
    out.adminPhone = cfg.admin_phone;
    out.numberBlacklist = cfg.blacklist;
    out.forwardRules = cfg.rules;
    out.callNotifyEnabled = cfg.callNotifyEnabled;
    out.pushEnabled = cfg.pushEnabled;
    for (int i = 0; i < MAX_PUSH_CHANNELS; ++i) out.pushChannels[i] = cfg.channels[i];
    out.email.enabled = cfg.emailEnabled;
    out.email.server = cfg.smtpServer;
    out.email.port = cfg.smtpPort;
    out.email.username = cfg.smtpUser;
    out.email.password = cfg.smtpPass;
    out.email.recipient = cfg.smtpSendTo;
    out.storage.inboxFile = cfg.inbox_file;
    out.web.listenAddress = cfg.web_listen_address;
    out.web.port = cfg.web_port;
    out.web.assetRoot = cfg.web_asset_root;
    out.web.username = cfg.web_username;
    out.web.password = cfg.web_password;
}

static void apply_portable_config(Config& cfg, const AppConfig& common)
{
    cfg.portable = common;
    cfg.modem_driver = common.modem.driver;
    if (!common.modem.endpoint.empty()) cfg.device = common.modem.endpoint;
    cfg.baud = common.modem.baud;
    cfg.poll_interval_ms = common.modem.pollIntervalMs;
    cfg.receiver = common.receiver;
    cfg.admin_phone = common.adminPhone;
    cfg.blacklist = common.numberBlacklist;
    cfg.rules = common.forwardRules;
    cfg.callNotifyEnabled = common.callNotifyEnabled;
    cfg.pushEnabled = common.pushEnabled;
    for (int i = 0; i < MAX_PUSH_CHANNELS; ++i) cfg.channels[i] = common.pushChannels[i];
    cfg.emailEnabled = common.email.enabled;
    cfg.smtpServer = common.email.server;
    cfg.smtpPort = common.email.port;
    cfg.smtpUser = common.email.username;
    cfg.smtpPass = common.email.password;
    cfg.smtpSendTo = common.email.recipient;
    cfg.inbox_file = common.storage.inboxFile;
    cfg.web_listen_address = common.web.listenAddress;
    cfg.web_port = common.web.port;
    cfg.web_asset_root = common.web.assetRoot;
    cfg.web_username = common.web.username;
    cfg.web_password = common.web.password;
    if (cfg.web_asset_root.empty()) cfg.web_asset_root = default_web_asset_root();
    sync_portable_config(cfg);
}

static std::string read_file(const std::string& path)
{
    std::ifstream in(path);
    if (!in) return {};
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static bool apply_channel_key(Config& cfg, const std::string& key, const std::string& value)
{
    if (key.rfind("push", 0) != 0 || key.size() < 6) return false;
    int idx = key[4] - '0';
    if (idx < 0 || idx >= MAX_PUSH_CHANNELS) return false;
    std::string suffix = key.substr(5);
    PushChannel& ch = cfg.channels[idx];
    if (suffix == "en") ch.enabled = (value == "1" || value == "true");
    else if (suffix == "type") ch.type = static_cast<uint8_t>(atoi(value.c_str()));
    else if (suffix == "url") ch.url = value;
    else if (suffix == "name") ch.name = value;
    else if (suffix == "k1") ch.key1 = value;
    else if (suffix == "k2") ch.key2 = value;
    else if (suffix == "body") ch.customBody = value;
    else return false;
    return true;
}

static bool load_config(const std::string& path, Config& cfg)
{
    const std::string raw = read_file(path);
    const size_t first = raw.find_first_not_of(" \t\r\n");
    if (first != std::string::npos && raw[first] == '{') {
        AppConfig common;
        std::string error;
        if (!parse_config_json(raw, common, &error)) {
            fprintf(stderr, "JSON 配置无效: %s\n", error.c_str());
            return false;
        }
        apply_portable_config(cfg, common);
        std::string rule_err;
        if (!validate_forward_rules(cfg.rules, &rule_err)) {
            fprintf(stderr, "转发规则无效: %s\n", rule_err.c_str());
            return false;
        }
        return true;
    }

    std::ifstream in(path);
    if (!in) {
        fprintf(stderr, "无法打开配置文件: %s\n", path.c_str());
        return false;
    }
    std::string line;
    while (std::getline(in, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(t.substr(0, eq));
        std::string value = trim(t.substr(eq + 1));
        if (apply_channel_key(cfg, key, value)) continue;
        if (key == "driver") cfg.modem_driver = value;
        else if (key == "device") cfg.device = value;
        else if (key == "baud") cfg.baud = atoi(value.c_str());
        else if (key == "poll_interval_ms") cfg.poll_interval_ms = atoi(value.c_str());
        else if (key == "receiver") cfg.receiver = value;
        else if (key == "adminPhone") cfg.admin_phone = value;
        else if (key == "blacklist_file") cfg.blacklist = read_file(value);
        else if (key == "rules_file") cfg.rules = read_file(value);
        else if (key == "callNotifyEnabled") cfg.callNotifyEnabled = (value == "1" || value == "true");
        else if (key == "pushEnabled") cfg.pushEnabled = (value == "1" || value == "true");
        else if (key == "emailEnabled") cfg.emailEnabled = (value == "1" || value == "true");
        else if (key == "smtpServer") cfg.smtpServer = value;
        else if (key == "smtpPort") cfg.smtpPort = atoi(value.c_str());
        else if (key == "smtpUser") cfg.smtpUser = value;
        else if (key == "smtpPass") cfg.smtpPass = value;
        else if (key == "smtpSendTo") cfg.smtpSendTo = value;
        else if (key == "inbox_file") cfg.inbox_file = value;
        else if (key == "web_listen_address") cfg.web_listen_address = value;
        else if (key == "web_port") cfg.web_port = atoi(value.c_str());
        else if (key == "web_asset_root") cfg.web_asset_root = value;
        else if (key == "web_username") cfg.web_username = value;
        else if (key == "web_password") cfg.web_password = value;
        // 兼容 MVP 单 webhook 键：映射为通道 0 的 POST JSON
        else if (key == "webhook_url") {
            cfg.channels[0].enabled = true;
            cfg.channels[0].type = PUSH_TYPE_POST_JSON;
            cfg.channels[0].url = value;
            if (cfg.channels[0].name.empty()) cfg.channels[0].name = "webhook";
        } else if (key == "webhook_template_file") {
            cfg.channels[0].type = PUSH_TYPE_CUSTOM;
            cfg.channels[0].customBody = read_file(value);
        }
    }
    std::string rule_err;
    if (!validate_forward_rules(cfg.rules, &rule_err)) {
        fprintf(stderr, "转发规则无效: %s\n", rule_err.c_str());
        return false;
    }
    if (cfg.web_asset_root.empty()) cfg.web_asset_root = default_web_asset_root();
    sync_portable_config(cfg);
    return true;
}

struct LinuxSchedulerState {
    uint32_t keepaliveLast = 0;
    std::array<uint32_t, MAX_SCHEDULED_TASKS> taskLast{};
    int64_t heartbeatLastDay = -1;
    int64_t rebootLastDay = -1;
};

static LinuxSchedulerState load_scheduler_state(const std::string& path, const Config& cfg)
{
    LinuxSchedulerState state;
    state.keepaliveLast = cfg.portable.keepalive.lastRun;
    for (size_t i = 0; i < state.taskLast.size(); ++i) {
        state.taskLast[i] = cfg.portable.scheduledTasks[i].lastRun;
    }
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        const size_t equal = line.find('=');
        if (equal == std::string::npos) continue;
        const std::string key = trim(line.substr(0, equal));
        const std::string value = trim(line.substr(equal + 1));
        char* end = nullptr;
        if (key == "heartbeatLastDay" || key == "rebootLastDay") {
            const long long parsed = std::strtoll(value.c_str(), &end, 10);
            if (!end || *end) continue;
            if (key == "heartbeatLastDay") state.heartbeatLastDay = parsed;
            else state.rebootLastDay = parsed;
            continue;
        }
        const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
        if (!end || *end || parsed > UINT32_MAX) continue;
        if (key == "keepaliveLast") state.keepaliveLast = static_cast<uint32_t>(parsed);
        else if (key.rfind("task", 0) == 0) {
            const int index = atoi(key.c_str() + 4);
            if (index >= 0 && index < static_cast<int>(state.taskLast.size())) {
                state.taskLast[index] = static_cast<uint32_t>(parsed);
            }
        }
    }
    return state;
}

static bool save_scheduler_state(const std::string& path, const LinuxSchedulerState& state)
{
    const std::string temporary = path + ".tmp";
    {
        std::ofstream out(temporary, std::ios::trunc);
        if (!out) return false;
        out << "keepaliveLast=" << state.keepaliveLast << '\n';
        out << "heartbeatLastDay=" << state.heartbeatLastDay << '\n';
        out << "rebootLastDay=" << state.rebootLastDay << '\n';
        for (size_t i = 0; i < state.taskLast.size(); ++i) {
            out << "task" << i << '=' << state.taskLast[i] << '\n';
        }
        out.flush();
        if (!out) return false;
    }
    return std::rename(temporary.c_str(), path.c_str()) == 0;
}

// ===== HttpClient(libcurl 实现) =====
static size_t curl_collect(char* data, size_t size, size_t nmemb, void* userp)
{
    static_cast<std::string*>(userp)->append(data, size * nmemb);
    return size * nmemb;
}

class CurlHttpClient : public HttpClient {
public:
    HttpResponse request(const HttpRequest& req) override
    {
        HttpResponse resp;
        CURL* curl = curl_easy_init();
        if (!curl) return resp;
        std::string ct_header = "Content-Type: " + req.content_type;
        curl_slist* headers = curl_slist_append(nullptr, ct_header.c_str());
        curl_easy_setopt(curl, CURLOPT_URL, req.url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(req.timeout_ms));
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_collect);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        if (req.method == "GET") {
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        } else {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req.body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(req.body.size()));
        }
        if (curl_easy_perform(curl) == CURLE_OK) {
            long status = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
            resp.status = static_cast<int>(status);
        }
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return resp;
    }
};

// ===== SMTP 邮件(libcurl) =====
struct SmtpUpload {
    std::string data;
    size_t off = 0;
};

static size_t smtp_read(char* buf, size_t size, size_t nmemb, void* userp)
{
    SmtpUpload* up = static_cast<SmtpUpload*>(userp);
    size_t room = size * nmemb;
    size_t left = up->data.size() - up->off;
    size_t take = left < room ? left : room;
    memcpy(buf, up->data.data() + up->off, take);
    up->off += take;
    return take;
}

// RFC 2047 UTF-8 主题编码
static std::string encode_subject(const std::string& subject)
{
    return "=?UTF-8?B?" +
           base64_encode(reinterpret_cast<const uint8_t*>(subject.data()), subject.size()) + "?=";
}

static bool send_email(const Config& cfg, const std::string& subject, const std::string& body)
{
    std::string scheme = cfg.smtpPort == 465 ? "smtps" : "smtp";
    std::string url = scheme + "://" + cfg.smtpServer + ":" + std::to_string(cfg.smtpPort);
    std::string to = cfg.smtpSendTo.empty() ? cfg.smtpUser : cfg.smtpSendTo;

    SmtpUpload up;
    up.data = "From: <" + cfg.smtpUser + ">\r\n"
              "To: <" + to + ">\r\n"
              "Subject: " + encode_subject(subject) + "\r\n"
              "MIME-Version: 1.0\r\n"
              "Content-Type: text/plain; charset=utf-8\r\n"
              "\r\n" + body + "\r\n";

    CURL* curl = curl_easy_init();
    if (!curl) return false;
    std::string rcpt_addr = "<" + to + ">";
    std::string from_addr = "<" + cfg.smtpUser + ">";
    curl_slist* rcpt = curl_slist_append(nullptr, rcpt_addr.c_str());
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERNAME, cfg.smtpUser.c_str());
    curl_easy_setopt(curl, CURLOPT_PASSWORD, cfg.smtpPass.c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, from_addr.c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, rcpt);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, smtp_read);
    curl_easy_setopt(curl, CURLOPT_READDATA, &up);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 30000L);
    if (cfg.smtpPort != 465) curl_easy_setopt(curl, CURLOPT_USE_SSL, static_cast<long>(CURLUSESSL_TRY));
    CURLcode rc = curl_easy_perform(curl);
    curl_slist_free_all(rcpt);
    curl_easy_cleanup(curl);
    return rc == CURLE_OK;
}

// ===== 收件箱持久化(JSONL 追加) =====
static void inbox_append(const Config& cfg, const SmsEvent& ev)
{
    if (cfg.inbox_file.empty()) return;
    std::string line = "{";
    json_prop(line, "timestamp", ev.timestamp); line += ",";
    json_prop(line, "sender", ev.sender); line += ",";
    json_prop(line, "receiver", ev.receiver); line += ",";
    json_prop(line, "message", ev.text);
    line += "}\n";
    std::ofstream out(cfg.inbox_file, std::ios::app);
    if (out) out << line;
    else fprintf(stderr, "\u6536\u4ef6\u7bb1\u5199\u5165\u5931\u8d25: %s\n", cfg.inbox_file.c_str());
}

static std::string message_state_path(const Config& cfg)
{
    return cfg.inbox_file.empty() ? std::string() : cfg.inbox_file + ".state";
}

class ThreadSafeMessageStore {
public:
    bool restore_state(const std::string& path, std::string* error = nullptr)
    {
        std::lock_guard<std::mutex> lock(mu_);
        state_path_ = path;
        persistence_error_.clear();
        if (error) error->clear();
        if (path.empty()) return true;
        std::ifstream input(path, std::ios::binary);
        if (!input) return true;  // First start: the first mutation creates the snapshot.
        std::ostringstream raw;
        raw << input.rdbuf();
        if (!input.eof() && input.fail()) {
            persistence_error_ = "读取消息状态失败";
            if (error) *error = persistence_error_;
            return false;
        }
        std::string restore_error;
        if (!store_.restore_snapshot(raw.str(), &restore_error)) {
            persistence_error_ = restore_error;
            if (error) *error = restore_error;
            return false;
        }
        return true;
    }

    bool set_state_path(const std::string& path, std::string* error = nullptr)
    {
        std::lock_guard<std::mutex> lock(mu_);
        state_path_ = path;
        persistence_error_.clear();
        if (state_path_.empty()) {
            if (error) error->clear();
            return true;
        }
        return persist_locked(error);
    }

    uint32_t add_received(const std::string& sender, const std::string& body,
                          const std::string& timestamp)
    {
        std::lock_guard<std::mutex> lock(mu_);
        const uint32_t id = store_.add_received(
            sender, body, timestamp, static_cast<uint32_t>(std::time(nullptr))).id;
        persist_locked(nullptr);
        return id;
    }

    void add_sent(const std::string& target, const std::string& body, bool ok)
    {
        std::lock_guard<std::mutex> lock(mu_);
        store_.add_sent(target, body, ok, static_cast<uint32_t>(std::time(nullptr)));
        persist_locked(nullptr);
    }

    bool set_forwarded(uint32_t id, bool forwarded)
    {
        std::lock_guard<std::mutex> lock(mu_);
        const bool changed = store_.set_forwarded(id, forwarded);
        if (changed) persist_locked(nullptr);
        return changed;
    }

    bool get_received(uint32_t id, InboxEntry& out) const
    {
        std::lock_guard<std::mutex> lock(mu_);
        return store_.get_received_by_id(id, out);
    }

    bool delete_received(uint32_t id)
    {
        std::lock_guard<std::mutex> lock(mu_);
        const bool deleted = store_.delete_received(id);
        if (deleted) persist_locked(nullptr);
        return deleted;
    }

    size_t received_count() const
    {
        std::lock_guard<std::mutex> lock(mu_);
        return store_.received_count();
    }

    std::string json(bool sent, int limit = 0) const
    {
        std::lock_guard<std::mutex> lock(mu_);
        return store_.json(sent, limit);
    }

    bool persistence_configured() const
    {
        std::lock_guard<std::mutex> lock(mu_);
        return !state_path_.empty();
    }

    bool persistence_ok() const
    {
        std::lock_guard<std::mutex> lock(mu_);
        return state_path_.empty() || persistence_error_.empty();
    }

    std::string persistence_error() const
    {
        std::lock_guard<std::mutex> lock(mu_);
        return persistence_error_;
    }

private:
    bool persist_locked(std::string* error)
    {
        if (error) error->clear();
        if (state_path_.empty()) return true;
        std::string write_error;
        const bool ok = atomic_write_file(state_path_, store_.snapshot(), &write_error);
        if (ok) {
            persistence_error_.clear();
            return true;
        }
        if (write_error != persistence_error_) {
            fprintf(stderr, "消息状态写入失败 %s: %s\n",
                    state_path_.c_str(), write_error.c_str());
        }
        persistence_error_ = write_error;
        if (error) *error = write_error;
        return false;
    }

    mutable std::mutex mu_;
    omnisms::MessageStore store_;
    std::string state_path_;
    std::string persistence_error_;
};

struct RuntimeState {
    std::atomic<bool> modemReady{false};
    std::atomic<int> simState{static_cast<int>(SimState::Unknown)};
    std::atomic<uint64_t> smsTotal{0};
    std::atomic<int64_t> lastSmsEpoch{0};
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
};

static std::map<std::string, std::string> parse_form(const std::string& body)
{
    std::map<std::string, std::string> fields;
    size_t pos = 0;
    while (pos <= body.size()) {
        size_t end = body.find('&', pos);
        if (end == std::string::npos) end = body.size();
        std::string item = body.substr(pos, end - pos);
        size_t equal = item.find('=');
        std::string key = url_decode_component(item.substr(0, equal));
        std::string value = equal == std::string::npos ? std::string() :
                            url_decode_component(item.substr(equal + 1));
        if (!key.empty()) fields[key] = value;
        if (end == body.size()) break;
        pos = end + 1;
    }
    return fields;
}

static std::string query_parameter(const std::string& query, const std::string& name)
{
    return parse_form(query)[name];
}

static bool query_u32_parameter(const std::string& query, const std::string& name, uint32_t& value)
{
    const std::string raw = query_parameter(query, name);
    if (raw.empty()) return false;
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(raw.c_str(), &end, 10);
    if (!end || *end != '\0' || parsed == 0 || parsed > UINT32_MAX) return false;
    value = static_cast<uint32_t>(parsed);
    return true;
}

static WebResponse api_result(bool success, const std::string& message, int status = 200)
{
    WebResponse response;
    response.status = status;
    response.body = std::string("{\"success\":") + (success ? "true" : "false") + ",";
    json_prop(response.body, "message", message);
    response.body += '}';
    return response;
}

static std::string linux_config_json(const Config& cfg)
{
    int enabled_channels = 0;
    for (const PushChannel& channel : cfg.channels) {
        if (cfg.pushEnabled && channel.enabled) ++enabled_channels;
    }
    const bool email_configured = !cfg.smtpServer.empty() && !cfg.smtpUser.empty();
    std::string out = "{";
    json_prop(out, "platform", "linux"); out += ',';
    json_prop(out, "webUser", cfg.web_username); out += ',';
    json_prop(out, "webPass", ""); out += ',';
    json_prop(out, "smtpServer", cfg.smtpServer); out += ',';
    out += "\"smtpPort\":" + std::to_string(cfg.smtpPort) + ',';
    json_prop(out, "smtpUser", cfg.smtpUser); out += ',';
    json_prop(out, "smtpPass", ""); out += ',';
    out += std::string("\"smtpPassSet\":") + (cfg.smtpPass.empty() ? "false," : "true,");
    json_prop(out, "smtpSendTo", cfg.smtpSendTo); out += ',';
    json_prop(out, "adminPhone", cfg.admin_phone); out += ',';
    json_prop(out, "numberBlackList", cfg.blacklist); out += ',';
    json_prop(out, "forwardRules", cfg.rules); out += ',';
    out += std::string("\"emailEnabled\":") + (cfg.emailEnabled ? "true," : "false,");
    out += std::string("\"emailConfigured\":") + (email_configured ? "true," : "false,");
    out += std::string("\"pushEnabled\":") + (cfg.pushEnabled ? "true," : "false,");
    out += "\"pushEnabledCount\":" + std::to_string(enabled_channels) + ',';
    out += "\"modemReady\":true,\"inboxMax\":50,";
    json_prop(out, "ntpServer", cfg.portable.time.ntpServer); out += ',';
    out += "\"tzOffsetMin\":" + std::to_string(cfg.portable.time.timezoneOffsetMin) + ',';
    out += std::string("\"rebootEnabled\":") + (cfg.portable.time.rebootEnabled ? "true," : "false,");
    out += "\"rebootHour\":" + std::to_string(cfg.portable.time.rebootHour) + ',';
    out += std::string("\"hbEnabled\":") + (cfg.portable.time.heartbeatEnabled ? "true," : "false,");
    out += "\"hbHour\":" + std::to_string(cfg.portable.time.heartbeatHour) + ',';
    out += std::string("\"dataEnabled\":") + (cfg.portable.cellular.dataEnabled ? "true," : "false,");
    out += std::string("\"roamingEnabled\":") + (cfg.portable.cellular.roamingEnabled ? "true," : "false,");
    json_prop(out, "apn", cfg.portable.cellular.apn); out += ',';
    json_prop(out, "phoneNumber", cfg.receiver); out += ',';
    json_prop(out, "operatorPlmn", cfg.portable.cellular.operatorPlmn); out += ',';
    json_prop(out, "kaProfile", cfg.portable.keepalive.profile); out += ',';
    out += std::string("\"netLedEnabled\":") + (cfg.portable.cellular.netLedEnabled ? "true," : "false,");
    out += std::string("\"callNotifyEnabled\":") + (cfg.callNotifyEnabled ? "true," : "false,");
    out += "\"pushChannels\":[";
    for (int i = 0; i < MAX_PUSH_CHANNELS; ++i) {
        if (i) out += ',';
        const PushChannel& channel = cfg.channels[i];
        out += std::string("{\"enabled\":") + (channel.enabled ? "true," : "false,");
        out += "\"type\":" + std::to_string(channel.type) + ',';
        json_prop(out, "name", channel.name); out += ',';
        json_prop(out, "url", channel.url); out += ',';
        json_prop(out, "key1", ""); out += ',';
        out += std::string("\"key1Set\":") + (channel.key1.empty() ? "false," : "true,");
        out += std::string("\"key1Redacted\":") + (channel.key1.empty() ? "false," : "true,");
        json_prop(out, "key2", ""); out += ',';
        out += std::string("\"key2Set\":") + (channel.key2.empty() ? "false," : "true,");
        out += std::string("\"key2Redacted\":") + (channel.key2.empty() ? "false," : "true,");
        json_prop(out, "customBody", channel.customBody);
        out += '}';
    }
    out += "]}";
    return out;
}

static std::string portable_export_json(const AppConfig& source, bool full)
{
    if (full) return serialize_config_json(source);
    AppConfig exported = source;
    auto redact = [](std::string& value) {
        if (!value.empty()) value = "__REDACTED__";
    };
    redact(exported.wifi.password);
    redact(exported.web.password);
    redact(exported.email.password);
    for (PushChannel& channel : exported.pushChannels) {
        redact(channel.url);
        redact(channel.key1);
        redact(channel.key2);
        redact(channel.customBody);
    }
    return serialize_config_json(exported);
}

static std::string linux_status_json(const Config& cfg, const RuntimeState& runtime,
                                     const ThreadSafeMessageStore& messages,
                                     const ModemBackend& backend)
{
    const int64_t uptime = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - runtime.started).count();
    int enabled_channels = 0;
    for (const PushChannel& channel : cfg.channels) {
        if (cfg.pushEnabled && channel.enabled) ++enabled_channels;
    }
    std::string out = "{\"platform\":\"linux\",\"version\":\"linux-dev\",";
    out += std::string("\"modemReady\":") + (runtime.modemReady.load() ? "true," : "false,");
    out += std::string("\"modemInitPhase\":\"") + (runtime.modemReady.load() ? "ready\"," : "failed\",");
    out += "\"signalFresh\":false,\"identityFresh\":false,\"csq\":null,\"ber\":99,";
    out += "\"rsrp\":null,\"rsrq\":null,\"sinr\":null,\"rssi\":null,";
    out += "\"tz\":" + std::to_string(cfg.portable.time.timezoneOffsetMin) + ',';
    out += "\"nowEpoch\":" + std::to_string(std::time(nullptr)) + ',';
    out += "\"uptime\":" + std::to_string(uptime) + ',';
    out += "\"smsTotal\":" + std::to_string(runtime.smsTotal.load()) + ',';
    out += "\"lastSmsEpoch\":" + std::to_string(runtime.lastSmsEpoch.load()) + ',';
    out += "\"inboxCount\":" + std::to_string(messages.received_count()) + ',';
    out += std::string("\"inboxPersistenceConfigured\":") +
           (messages.persistence_configured() ? "true," : "false,");
    out += std::string("\"inboxPersistenceOk\":") +
           (messages.persistence_ok() ? "true," : "false,");
    json_prop(out, "inboxPersistenceError", messages.persistence_error()); out += ',';
    out += "\"queueDepth\":0,\"fwdQueueDepth\":0,\"outSmsQueueDepth\":0,\"emailQueueDepth\":0,\"slowBusy\":false,";
    out += "\"freeHeap\":0,\"minFreeHeap\":0,\"maxAllocHeap\":0,\"resetReason\":0,\"chipTemp\":null,";
    out += std::string("\"emailEnabled\":") + (cfg.emailEnabled ? "true," : "false,");
    out += std::string("\"emailConfigured\":") + ((!cfg.smtpServer.empty() && !cfg.smtpUser.empty()) ? "true," : "false,");
    out += std::string("\"pushEnabled\":") + (cfg.pushEnabled ? "true," : "false,");
    out += "\"pushEnabledCount\":" + std::to_string(enabled_channels) + ',';
    out += std::string("\"dataEnabled\":") + (cfg.portable.cellular.dataEnabled ? "true," : "false,");
    json_prop(out, "adminPhone", cfg.admin_phone); out += ',';
    json_prop(out, "phone", cfg.receiver); out += ',';
    json_prop(out, "apn", cfg.portable.cellular.apn); out += ',';
    json_prop(out, "model", cfg.modem_driver); out += ',';
    json_prop(out, "modemEndpoint", backend.endpoint()); out += ',';
    const ModemCapabilities capabilities = backend.capabilities();
    out += "\"modemCapabilities\":{";
    out += std::string("\"pduSms\":") + (capabilities.pduSms ? "true," : "false,");
    out += std::string("\"textSms\":") + (capabilities.textSms ? "true," : "false,");
    out += std::string("\"incomingCall\":") + (capabilities.incomingCall ? "true," : "false,");
    out += std::string("\"ussd\":") + (capabilities.ussd ? "true," : "false,");
    out += std::string("\"simHotplug\":") + (capabilities.simHotplug ? "true," : "false,");
    out += std::string("\"rawAt\":") + (capabilities.rawAt ? "true," : "false,");
    out += std::string("\"reset\":") + (capabilities.reset ? "true" : "false");
    out += "},";
    json_prop(out, "mfr", ""); out += ',';
    json_prop(out, "fwver", ""); out += ',';
    json_prop(out, "operator", ""); out += ',';
    json_prop(out, "imei", ""); out += ',';
    json_prop(out, "iccid", ""); out += ',';
    json_prop(out, "imsi", "");
    out += ",\"apMode\":false,\"ssid\":\"\",\"ip\":\"\",\"gw\":\"\",\"mask\":\"\",\"dns\":\"\",\"mac\":\"\",\"bssid\":\"\",\"chan\":0,\"timeSynced\":true}";
    return out;
}

// ===== 推送 worker（调度/退避状态由 core::ForwardRetryQueue 持有） =====
static int64_t steady_now_us()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

class PushWorker {
public:
    using CompletionCallback = std::function<void(uint32_t, bool)>;

    PushWorker(const Config& cfg, HttpClient& http, CompletionCallback completion)
        : cfg_(cfg), http_(http), completion_(std::move(completion))
    {
        thread_ = std::thread([this] { run(); });
    }

    ~PushWorker()
    {
        {
            std::lock_guard<std::mutex> lk(mu_);
            stop_ = true;
        }
        cv_.notify_all();
        if (thread_.joinable()) thread_.join();
    }

    bool begin_message(uint32_t message_id, int targets)
    {
        if (message_id == 0) return true;
        if (targets <= 0) return false;
        std::lock_guard<std::mutex> lk(mu_);
        if (completions_.find(message_id) != completions_.end()) return false;
        completions_[message_id] = Completion{targets, false};
        return true;
    }

    bool enqueue(int channel, const SmsEvent& ev, uint32_t message_id = 0, bool notify = false)
    {
        bool accepted = false;
        {
            std::lock_guard<std::mutex> lk(mu_);
            accepted = jobs_.enqueue(channel, ev, steady_now_us(),
                                     static_cast<uint32_t>((channel + 1) * 7), message_id, notify);
        }
        if (accepted) cv_.notify_one();
        return accepted;
    }

    void reject_leg(uint32_t message_id)
    {
        const CompletionNotice notice = finish_leg(message_id, false);
        if (notice.ready && completion_) completion_(notice.messageId, notice.success);
    }

    void mark_message(uint32_t message_id, bool success)
    {
        if (message_id != 0 && completion_) completion_(message_id, success);
    }

    bool idle()
    {
        std::lock_guard<std::mutex> lk(mu_);
        return !busy_ && jobs_.empty();
    }

    void update_config(const Config& config)
    {
        std::lock_guard<std::mutex> lock(config_mu_);
        cfg_ = config;
    }

    // 等待所有任务完成首轮尝试(测试/单发模式用)；失败进退避重试的不等
    void drain()
    {
        std::unique_lock<std::mutex> lk(mu_);
        idle_cv_.wait(lk, [this] {
            if (busy_) return false;
            return !jobs_.has_initial_attempt();
        });
    }

private:
    struct Completion {
        int remaining = 0;
        bool failed = false;
    };

    struct CompletionNotice {
        bool ready = false;
        uint32_t messageId = 0;
        bool success = false;
    };

    CompletionNotice finish_leg_locked(uint32_t message_id, bool success)
    {
        if (message_id == 0) return {};
        auto found = completions_.find(message_id);
        if (found == completions_.end()) return {};
        if (!success) found->second.failed = true;
        if (--found->second.remaining > 0) return {};
        CompletionNotice notice{true, message_id, !found->second.failed};
        completions_.erase(found);
        return notice;
    }

    CompletionNotice finish_leg(uint32_t message_id, bool success)
    {
        std::lock_guard<std::mutex> lk(mu_);
        return finish_leg_locked(message_id, success);
    }

    void run()
    {
        std::unique_lock<std::mutex> lk(mu_);
        while (!stop_) {
            if (jobs_.empty()) {
                idle_cv_.notify_all();
                cv_.wait(lk, [this] { return stop_ || !jobs_.empty(); });
                continue;
            }
            const int64_t now_us = steady_now_us();
            const int64_t due_us = jobs_.next_due_us();
            if (due_us > now_us) {
                idle_cv_.notify_all();  // 只剩未来重试任务：视为首轮完成
                cv_.wait_for(lk, std::chrono::microseconds(due_us - now_us));
                continue;
            }
            ForwardJob job;
            if (!jobs_.take_ready(now_us, job)) continue;
            busy_ = true;
            lk.unlock();
            std::string name;
            const bool ok = process(job, name);
            lk.lock();
            bool leg_finished = ok;
            bool leg_success = ok;
            if (!ok) {
                if (!jobs_.retry(job, steady_now_us())) {
                    fprintf(stderr, "  %s 达到重试上限或队列已满（失败 %d 次），放弃\n",
                            name.c_str(), job.attempts);
                    leg_finished = true;
                    leg_success = false;
                }
            }
            const CompletionNotice notice = leg_finished
                ? finish_leg_locked(job.messageId, leg_success) : CompletionNotice{};
            busy_ = false;
            idle_cv_.notify_all();
            if (notice.ready && completion_) {
                lk.unlock();
                completion_(notice.messageId, notice.success);
                lk.lock();
            }
        }
    }

    bool process(const ForwardJob& job, std::string& name)
    {
        Config config;
        {
            std::lock_guard<std::mutex> lock(config_mu_);
            config = cfg_;
        }
        bool ok = false;
        if (job.target < 0) {
            name = "邮件";
            const EmailContent content = job.notify
                ? build_notification_email(job.event.sender, job.event.text)
                : build_sms_email(job.event);
            ok = send_email(config, content.subject, content.body);
            printf("  邮件发送%s\n", ok ? "成功" : "失败");
        } else {
            const PushChannel& ch = config.channels[job.target];
            name = ch.name.empty() ? ("通道" + std::to_string(job.target + 1)) : ch.name;
            HttpRequest req;
            int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::system_clock::now().time_since_epoch()).count();
            if (!build_push_request(ch, job.event, job.notify, now_ms, req)) {
                fprintf(stderr, "  %s 配置无效，任务丢弃\n", name.c_str());
                return true;
            }
            HttpResponse resp = http_.request(req);
            ok = resp.status >= 200 && resp.status < 300;
            printf("  %s 推送%s (HTTP %d)\n", name.c_str(), ok ? "成功" : "失败", resp.status);
        }
        return ok;
    }

    Config cfg_;
    std::mutex config_mu_;
    HttpClient& http_;
    CompletionCallback completion_;
    ForwardRetryQueue jobs_;
    std::map<uint32_t, Completion> completions_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::condition_variable idle_cv_;
    bool busy_ = false;
    bool stop_ = false;
    std::thread thread_;
};

// pdulib 时间戳 "YYMMDDHHMMSS" → "20YY-MM-DD HH:MM:SS"(尽力而为)
static std::string format_pdu_timestamp(const std::string& ts)
{
    if (ts.size() >= 19 && ts[4] == '-' && ts[7] == '-' &&
        (ts[10] == 'T' || ts[10] == ' ')) {
        std::string normalized = ts.substr(0, 19);
        normalized[10] = ' ';
        return normalized;
    }
    if (ts.size() < 12) return ts;
    return "20" + ts.substr(0, 2) + "-" + ts.substr(2, 2) + "-" + ts.substr(4, 2) + " " +
           ts.substr(6, 2) + ":" + ts.substr(8, 2) + ":" + ts.substr(10, 2);
}

static int64_t mono_us()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

static std::string format_local_now();

// ===== 收件处理管道 =====
static bool forward_event(const Config& cfg, PushWorker& worker, const SmsEvent& ev,
                          uint32_t message_id = 0)
{
    if (number_blacklisted(cfg.blacklist, ev.sender)) {
        printf("  黑名单命中，丢弃\n");
        return false;
    }
    ForwardDecision fd = eval_forward_rules(cfg.rules, ev.sender, ev.text);
    if (fd.matched && fd.drop) {
        printf("  转发规则命中 drop，丢弃\n");
        worker.mark_message(message_id, true);
        return true;
    }
    uint32_t mask = fd.matched ? fd.chMask : 0xFFFFFFFFu;
    bool email_selected = fd.matched ? fd.email : true;
    int target_count = 0;
    for (int i = 0; cfg.pushEnabled && i < MAX_PUSH_CHANNELS; ++i) {
        if ((mask & (1u << i)) && channel_valid(cfg.channels[i])) ++target_count;
    }
    const bool use_email = email_selected && email_configured(cfg.portable.email);
    if (use_email) ++target_count;
    if (target_count == 0) {
        printf("  无有效推送目标\n");
        return false;
    }
    if (!worker.begin_message(message_id, target_count)) {
        fprintf(stderr, "  短信 id=%u 已有转发任务在执行\n", static_cast<unsigned>(message_id));
        return false;
    }
    int dispatched = 0;
    for (int i = 0; cfg.pushEnabled && i < MAX_PUSH_CHANNELS; ++i) {
        if (!(mask & (1u << i))) continue;
        if (!channel_valid(cfg.channels[i])) continue;
        if (worker.enqueue(i, ev, message_id)) ++dispatched;
        else {
            worker.reject_leg(message_id);
            fprintf(stderr, "  转发队列已满，通道 %d 未入队\n", i + 1);
        }
    }
    if (use_email) {
        if (worker.enqueue(-1, ev, message_id)) ++dispatched;
        else {
            worker.reject_leg(message_id);
            fprintf(stderr, "  转发队列已满，邮件未入队\n");
        }
    }
    if (dispatched == 0) printf("  所有转发目标入队失败\n");
    return dispatched > 0;
}

static bool notify_event(const Config& cfg, PushWorker& worker,
                         const std::string& title, const std::string& body)
{
    SmsEvent event{title, body, format_local_now(), cfg.receiver};
    int dispatched = 0;
    for (int i = 0; cfg.pushEnabled && i < MAX_PUSH_CHANNELS; ++i) {
        if (channel_valid(cfg.channels[i]) && worker.enqueue(i, event, 0, true)) ++dispatched;
    }
    if (email_configured(cfg.portable.email) &&
        worker.enqueue(-1, event, 0, true)) {
        ++dispatched;
    }
    return dispatched > 0;
}

static void handle_sms(const Config& cfg, PushWorker& worker, ThreadSafeMessageStore& messages,
                       RuntimeState& runtime,
                       const std::function<bool(const std::string&, const std::string&)>& admin_handler,
                       const std::string& sender,
                       const std::string& text, const std::string& timestamp)
{
    SmsEvent ev{sender, text, format_pdu_timestamp(timestamp), cfg.receiver};
    printf("[短信] %s | %s | %s\n", ev.sender.c_str(), ev.timestamp.c_str(), ev.text.c_str());
    if (number_blacklisted(cfg.blacklist, sender)) {
        printf("  黑名单命中，丢弃\n");
        return;
    }
    runtime.smsTotal.fetch_add(1);
    runtime.lastSmsEpoch.store(static_cast<int64_t>(std::time(nullptr)));
    if (admin_handler && admin_handler(sender, text)) return;
    const uint32_t message_id = messages.add_received(sender, text, ev.timestamp);
    inbox_append(cfg, ev);
    forward_event(cfg, worker, ev, message_id);
}

static std::string format_local_now()
{
    const std::time_t now = std::time(nullptr);
    std::tm local = {};
    localtime_r(&now, &local);
    char buf[24];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &local);
    return buf;
}

static void handle_call(const Config& cfg, PushWorker& worker, const std::string& caller)
{
    if (!cfg.callNotifyEnabled) return;
    const std::string display = caller.empty() ? "未知号码" : caller;
    printf("[来电] %s\n", display.c_str());
    SmsEvent ev{display, "来电：" + display, format_local_now(), cfg.receiver};
    forward_event(cfg, worker, ev);
}

static void process_modem_result(
    ModemBackend& backend, const ModemEventBatch& result, PduDecoder& dec,
    ConcatAssembler& concat, IncomingCallTracker& calls,
    const std::function<void(const std::string&, const std::string&, const std::string&)>& direct_sms)
{
    for (const std::string& line : result.urcLines) {
        calls.feed(line, mono_us());
    }
    for (const ModemMessage& message : result.messages) {
        if (message.pduEncoded) {
            DecodedSms sms;
            if (!dec.decode(message.pdu, sms)) {
                fprintf(stderr, "PDU 解析失败(标识=%s)，保留待查\n", message.id.c_str());
                continue;
            }
            concat.feed(sms, mono_us());
        } else {
            direct_sms(message.sender, message.text, message.timestamp);
        }
        std::string error;
        if (!message.id.empty() && !backend.acknowledge_sms(message.id, &error)) {
            fprintf(stderr, "短信删除失败(标识=%s)，后续可能再次看到: %s\n",
                    message.id.c_str(), error.c_str());
        }
    }
}

static bool send_text_sms(ModemBackend& backend, const std::string& phone,
                          const std::string& text)
{
    std::string error;
    if (!backend.send_sms(phone, text, &error)) {
        fprintf(stderr, "%s\n", error.c_str());
        return false;
    }
    printf("短信发送成功\n");
    return true;
}

static const char* sim_state_name(SimState state)
{
    switch (state) {
        case SimState::Ready: return "已就绪";
        case SimState::Missing: return "未插卡";
        case SimState::Locked: return "需要 PIN/PUK";
        default: return "未知";
    }
}

int main(int argc, char** argv)
{
    std::string conf_path = "/etc/omnisms/omnisms.conf";
    std::string test_pdu;
    std::string send_to;
    std::string send_text;
    std::string ussd_code;
    bool list_modems = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-c" && i + 1 < argc) conf_path = argv[++i];
        else if (arg == "--list-modems") list_modems = true;
        else if (arg == "--test-pdu" && i + 1 < argc) test_pdu = argv[++i];
        else if (arg == "--send-sms" && i + 2 < argc) {
            send_to = argv[++i];
            send_text = argv[++i];
        } else if (arg == "--ussd" && i + 1 < argc) ussd_code = argv[++i];
    }

    if (list_modems) {
        std::string error;
        const std::vector<std::string> modems = list_modemmanager_modems({}, &error);
        if (modems.empty()) {
            fprintf(stderr, "%s\n", error.empty() ? "ModemManager 未发现 modem" : error.c_str());
            return 1;
        }
        printf("ModemManager devices (%u):\n", static_cast<unsigned>(modems.size()));
        for (size_t i = 0; i < modems.size(); ++i) {
            printf("  %u: %s\n", static_cast<unsigned>(i), modems[i].c_str());
        }
        return 0;
    }

    Config cfg;
    if (!load_config(conf_path, cfg)) return 1;
    curl_global_init(CURL_GLOBAL_DEFAULT);

    CurlHttpClient http;
    std::mutex config_mutex;
    auto snapshot_config = [&] {
        std::lock_guard<std::mutex> lock(config_mutex);
        return cfg;
    };
    ThreadSafeMessageStore messages;
    std::string message_restore_error;
    if (!messages.restore_state(message_state_path(cfg), &message_restore_error)) {
        fprintf(stderr, "消息状态恢复失败，保留损坏文件并从空状态运行: %s\n",
                message_restore_error.c_str());
    } else if (messages.persistence_configured()) {
        printf("消息状态持久化: %s\n", message_state_path(cfg).c_str());
    }
    PushWorker worker(cfg, http, [&](uint32_t id, bool success) {
        messages.set_forwarded(id, success);
        if (!success) {
            fprintf(stderr, "短信 id=%u 存在最终投递失败，保持未处理供手动重发\n",
                    static_cast<unsigned>(id));
        }
    });
    RuntimeState runtime;
    std::function<bool(const std::string&, const std::string&)> admin_handler;
    PduDecoder dec;
    ConcatAssembler concat([&](const std::string& s, const std::string& t, const std::string& ts) {
        const Config current = snapshot_config();
        handle_sms(current, worker, messages, runtime, admin_handler, s, t, ts);
    });
    IncomingCallTracker calls([&](const std::string& caller) {
        const Config current = snapshot_config();
        handle_call(current, worker, caller);
    });

    // --test-pdu：不开串口，直接解码一条 PDU 走完整管道(联调用)
    if (!test_pdu.empty()) {
        DecodedSms sms;
        if (!dec.decode(test_pdu, sms)) {
            fprintf(stderr, "PDU 解析失败\n");
            return 1;
        }
        concat.feed(sms, mono_us());
        concat.expire(mono_us() + ConcatAssembler::TIMEOUT_US + 1);
        worker.drain();
        // 测试模式不等重试退避；_exit 跳过 PushWorker 线程析构(joinable 会 terminate)，
        // 先冲刷 stdio 避免管道缓冲吞掉输出
        fflush(stdout);
        fflush(stderr);
        _exit(0);
    }

    std::string modem_error;
    std::unique_ptr<ModemBackend> backend = create_modem_backend(
        cfg.modem_driver, cfg.device, cfg.baud, &modem_error);
    if (!backend) {
        fprintf(stderr, "未知 modem driver: %s（可用:", cfg.modem_driver.c_str());
        for (const std::string& id : registered_modem_drivers()) fprintf(stderr, " %s", id.c_str());
        fprintf(stderr, " modemmanager）: %s\n", modem_error.c_str());
        return 1;
    }
    if (!backend->initialize(&modem_error)) {
        fprintf(stderr, "%s\n", modem_error.c_str());
        return 1;
    }
    runtime.modemReady.store(true);
    printf("OmniSMS Linux 端口启动：driver=%s，设备=%s，轮询 %dms\n",
           backend->id(), backend->endpoint().c_str(), cfg.poll_interval_ms);
    const ModemCapabilities backend_capabilities = backend->capabilities();
    printf("模组能力：PDU=%s text=%s call=%s USSD=%s SIM-hotplug=%s raw-AT=%s reset=%s\n",
           backend_capabilities.pduSms ? "yes" : "no",
           backend_capabilities.textSms ? "yes" : "no",
           backend_capabilities.incomingCall ? "yes" : "no",
           backend_capabilities.ussd ? "yes" : "no",
           backend_capabilities.simHotplug ? "yes" : "no",
           backend_capabilities.rawAt ? "yes" : "no",
           backend_capabilities.reset ? "yes" : "no");

    if (!send_to.empty()) return send_text_sms(*backend, send_to, send_text) ? 0 : 1;
    if (!ussd_code.empty()) {
        std::string response;
        if (!backend->send_ussd(ussd_code, response, &modem_error)) {
            fprintf(stderr, "%s\n", modem_error.c_str());
            return 1;
        }
        printf("USSD 响应: %s\n", response.c_str());
        return 0;
    }

    SimState sim_state = SimState::Unknown;
    if (backend->query_sim_state(sim_state, &modem_error)) {
        runtime.simState.store(static_cast<int>(sim_state));
        printf("SIM 状态: %s\n", sim_state_name(sim_state));
    } else {
        fprintf(stderr, "SIM 状态读取失败: %s\n", modem_error.c_str());
    }

    std::mutex modem_mutex;
    std::deque<AdminCommandDecision> admin_actions;
    std::atomic<bool> admin_sms_busy{false};
    std::atomic<bool> admin_reset_pending{false};
    std::atomic<bool> restart_requested{false};
    admin_handler = [&](const std::string& sender, const std::string& body) {
        const Config current = snapshot_config();
        AdminCommandDecision decision = evaluate_admin_command(
            current.admin_phone, sender, body,
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - runtime.started).count(),
            admin_sms_busy.load(), admin_reset_pending.load());
        if (!decision.handled()) return false;
        if (decision.status == AdminCommandStatus::SendSms) {
            bool expected = false;
            if (!admin_sms_busy.compare_exchange_strong(expected, true)) {
                decision.status = AdminCommandStatus::SmsBusy;
            }
        } else if (decision.status == AdminCommandStatus::Reset) {
            bool expected = false;
            if (!admin_reset_pending.compare_exchange_strong(expected, true)) {
                decision.status = AdminCommandStatus::ResetPending;
            }
        }
        admin_actions.push_back(std::move(decision));
        return true;
    };
    auto notify_admin = [&](const std::string& subject, const std::string& body) {
        const Config current = snapshot_config();
        if (!current.emailEnabled || current.smtpServer.empty() || current.smtpUser.empty()) {
            fprintf(stderr, "管理员命令通知未发送（SMTP 未配置）: %s\n", body.c_str());
            return false;
        }
        const bool ok = send_email(current, subject, body);
        fprintf(stderr, "管理员命令通知邮件%s: %s\n", ok ? "成功" : "失败", subject.c_str());
        return ok;
    };
    auto process_admin_action = [&] {
        if (admin_actions.empty()) return;
        AdminCommandDecision decision = std::move(admin_actions.front());
        admin_actions.pop_front();
        if (decision.status == AdminCommandStatus::SendSms) {
            bool ok = false;
            {
                std::lock_guard<std::mutex> lock(modem_mutex);
                ok = send_text_sms(*backend, decision.target, decision.content);
            }
            messages.add_sent(decision.target, decision.content, ok);
            admin_sms_busy.store(false);
            const std::string subject = ok ? "短信发送成功" : "短信发送失败";
            const std::string body = "管理员命令执行结果:\n命令: " + decision.command +
                                     "\n目标号码: " + decision.target +
                                     "\n短信内容: " + decision.content +
                                     "\n执行结果: " + (ok ? "成功" : "失败");
            notify_admin(subject, body);
            return;
        }
        if (decision.status == AdminCommandStatus::Reset) {
            notify_admin("重启命令已执行", "收到RESET命令，即将重启模组和OmniSMS服务...");
            {
                std::lock_guard<std::mutex> lock(modem_mutex);
                if (!backend->reset(&modem_error)) {
                    fprintf(stderr, "模组重启失败: %s\n", modem_error.c_str());
                }
            }
            restart_requested.store(true);
            return;
        }
        const std::string message = admin_command_message(decision.status);
        const bool reset_rejected = decision.status == AdminCommandStatus::ResetTooSoon ||
                                    decision.status == AdminCommandStatus::ResetPending;
        notify_admin(reset_rejected ? "RESET已忽略" : "命令执行失败", message);
    };

    const std::string scheduler_state_path = conf_path + ".state";
    LinuxSchedulerState scheduler_state = load_scheduler_state(scheduler_state_path, cfg);
    {
        std::lock_guard<std::mutex> lock(config_mutex);
        cfg.portable.keepalive.lastRun = scheduler_state.keepaliveLast;
        for (size_t i = 0; i < scheduler_state.taskLast.size(); ++i) {
            cfg.portable.scheduledTasks[i].lastRun = scheduler_state.taskLast[i];
        }
    }

    const Config web_initial = snapshot_config();
    WebServerOptions web_options;
    web_options.listenAddress = web_initial.web_listen_address;
    web_options.port = web_initial.web_port;
    web_options.assetRoot = web_initial.web_asset_root;
    web_options.username = web_initial.web_username;
    web_options.password = web_initial.web_password;
    web_options.assetHash = "linux";
    LinuxWebServer* web_server_runtime = nullptr;
    auto persist_and_apply_config = [&](ConfigUpdateResult update) -> WebResponse {
        if (!update.success) return api_result(false, update.message, 400);
        std::string write_error;
        Config next;
        {
            std::lock_guard<std::mutex> lock(config_mutex);
            // Scheduler state has its own sidecar and may have advanced after the Web request
            // took its snapshot. Never let a settings save roll those timestamps backwards.
            update.config.keepalive.lastRun = cfg.portable.keepalive.lastRun;
            for (size_t i = 0; i < update.config.scheduledTasks.size(); ++i) {
                update.config.scheduledTasks[i].lastRun = cfg.portable.scheduledTasks[i].lastRun;
            }
            if (!atomic_write_config(conf_path, update.config, &write_error)) {
                return api_result(false, "配置写入失败: " + write_error, 500);
            }
            next = cfg;
            apply_portable_config(next, update.config);
            cfg = next;
        }
        worker.update_config(next);
        std::string message_state_error;
        const bool message_state_ok = messages.set_state_path(
            message_state_path(next), &message_state_error);
        if (web_server_runtime) {
            web_server_runtime->update_runtime_options(
                next.web_asset_root, next.web_username, next.web_password);
        }
        std::string message = update.message;
        if (update.restartRequired && message.find("需重启") == std::string::npos) {
            message += "；串口/驱动或监听地址变更需重启服务后生效";
        }
        if (!message_state_ok) message += "；消息状态写入失败: " + message_state_error;
        printf("Web 配置更新: %s\n", message.c_str());
        return api_result(true, message);
    };
    WebCallbacks web_callbacks;
    web_callbacks.configJson = [&] {
        std::lock_guard<std::mutex> lock(config_mutex);
        return linux_config_json(cfg);
    };
    web_callbacks.statusJson = [&] {
        std::lock_guard<std::mutex> lock(config_mutex);
        return linux_status_json(cfg, runtime, messages, *backend);
    };
    web_callbacks.messagesJson = [&](const WebRequest& request) {
        const std::string limit_raw = query_parameter(request.query, "limit");
        const int limit = limit_raw.empty() ? 0 : std::max(0, atoi(limit_raw.c_str()));
        return messages.json(query_parameter(request.query, "box") == "sent", limit);
    };
    web_callbacks.exportJson = [&](const WebRequest& request) {
        std::lock_guard<std::mutex> lock(config_mutex);
        return portable_export_json(cfg.portable,
                                    query_parameter(request.query, "full") == "1");
    };
    web_callbacks.dynamic = [&](const WebRequest& request) -> WebResponse {
        if (request.path == "/sendsms" && request.method == "POST") {
            const auto fields = parse_form(request.body);
            const auto phone = fields.find("phone");
            const auto content = fields.find("content");
            if (phone == fields.end() || !is_valid_phone_number(phone->second)) {
                return api_result(false, "目标号码格式无效", 400);
            }
            if (content == fields.end() || trim(content->second).empty()) {
                return api_result(false, "短信内容为空", 400);
            }
            bool ok = false;
            {
                std::lock_guard<std::mutex> lock(modem_mutex);
                ok = send_text_sms(*backend, phone->second, content->second);
            }
            messages.add_sent(phone->second, content->second, ok);
            return api_result(ok, ok ? "短信发送成功" : "短信发送失败", ok ? 200 : 500);
        }
        if (request.path == "/ussd" && request.method == "POST") {
            const std::string code = query_parameter(request.query, "code");
            if (code.empty()) return api_result(false, "USSD 码为空", 400);
            std::string response;
            std::string error;
            bool ok = false;
            {
                std::lock_guard<std::mutex> lock(modem_mutex);
                ok = backend->send_ussd(code, response, &error);
            }
            return api_result(ok, ok ? response : error, ok ? 200 : 500);
        }
        if (request.path == "/at" && request.method == "POST") {
            const std::string command = trim(query_parameter(request.query, "cmd"));
            if (command.rfind("AT", 0) != 0 || command.size() > 256) {
                return api_result(false, "AT 指令格式无效", 400);
            }
            std::string response;
            {
                std::lock_guard<std::mutex> lock(modem_mutex);
                std::string error;
                if (!backend->raw_at(command, response, &error)) {
                    return api_result(false, error, 501);
                }
            }
            return api_result(!response.empty(), response.empty() ? "AT 指令无响应" : response,
                              response.empty() ? 500 : 200);
        }
        if (request.path == "/delete" && request.method == "POST") {
            uint32_t id = 0;
            const bool ok = query_u32_parameter(request.query, "id", id) &&
                            messages.delete_received(id);
            return api_result(ok, ok ? "已删除" : "未找到", ok ? 200 : 404);
        }
        if (request.path == "/resend" && request.method == "POST") {
            uint32_t id = 0;
            InboxEntry entry;
            if (!query_u32_parameter(request.query, "id", id) ||
                !messages.get_received(id, entry)) {
                return api_result(false, "未找到该短信", 404);
            }
            messages.set_forwarded(id, false);
            const Config current = snapshot_config();
            SmsEvent event{entry.sender, entry.text, entry.ts, current.receiver};
            const bool queued = forward_event(current, worker, event, id);
            return api_result(queued, queued ? "已重新入队转发" : "没有可用目标或转发队列繁忙",
                              queued ? 200 : 503);
        }
        if (request.path == "/save" && request.method == "POST") {
            const Config current = snapshot_config();
            return persist_and_apply_config(
                apply_web_config_form(current.portable, parse_form(request.body)));
        }
        if (request.path == "/import" && request.method == "POST") {
            const Config current = snapshot_config();
            return persist_and_apply_config(import_portable_config(current.portable, request.body));
        }
        return api_result(false, "该接口在 Linux 平台尚未实现", 501);
    };
    LinuxWebServer web_server(web_options, web_callbacks);
    web_server_runtime = &web_server;
    if (web_initial.web_port > 0) {
        std::string web_error;
        if (!web_server.start(&web_error)) {
            fprintf(stderr, "Web 服务启动失败: %s\n", web_error.c_str());
            return 1;
        }
        printf("Web 管理界面: http://%s:%d/\n",
               web_initial.web_listen_address.c_str(), web_server.bound_port());
    }

    auto persist_scheduler_state = [&] {
        {
            std::lock_guard<std::mutex> lock(config_mutex);
            cfg.portable.keepalive.lastRun = scheduler_state.keepaliveLast;
            for (size_t i = 0; i < scheduler_state.taskLast.size(); ++i) {
                cfg.portable.scheduledTasks[i].lastRun = scheduler_state.taskLast[i];
            }
        }
        if (!save_scheduler_state(scheduler_state_path, scheduler_state)) {
            fprintf(stderr, "调度状态写入失败: %s\n", scheduler_state_path.c_str());
        }
    };
    auto run_scheduled_action = [&](const Config& action_config, uint8_t action,
                                     const std::string& target,
                                     const std::string& payload, const std::string& url,
                                    const std::string& profile, const std::string& label,
                                    bool keepalive) -> std::pair<bool, std::string> {
        if (!profile.empty()) {
            return {false, "Linux eSIM Profile 切换尚未实现"};
        }
        if (action == 0 && !keepalive) {
            const std::string body = payload.empty() ? ("定时提醒触发：" + label) : payload;
            const bool ok = notify_event(action_config, worker, label, body);
            return {ok, ok ? "提醒已入队" : "没有可用的推送或邮件目标"};
        }
        if (action == 1 || (keepalive && action != 2 && action != 3)) {
            std::string request_url = url.empty() ? target : url;
            if (keepalive && request_url.empty()) {
                request_url = "http://gg.incrafttime.top/api/payload?size=64342";
            }
            if (request_url.empty()) return {false, "HTTP URL 为空"};
            HttpRequest request;
            request.method = "GET";
            request.url = request_url;
            request.timeout_ms = 30000;
            const HttpResponse response = http.request(request);
            const bool ok = response.status >= 200 && response.status < 300;
            return {ok, "HTTP " + std::to_string(response.status)};
        }
        if (action == 2) {
            const std::string body = keepalive ? "keepalive" :
                                     (payload.empty() ? "scheduled task" : payload);
            bool ok = false;
            {
                std::lock_guard<std::mutex> lock(modem_mutex);
                ok = send_text_sms(*backend, target, body);
            }
            messages.add_sent(target, body, ok);
            return {ok, ok ? "短信发送成功" : "短信发送失败"};
        }
        if (action == 3) {
            std::string response;
            std::string error;
            bool ok = false;
            {
                std::lock_guard<std::mutex> lock(modem_mutex);
                ok = backend->send_ussd(target, response, &error);
            }
            return {ok, ok ? response : error};
        }
        return {false, "未知调度动作"};
    };

    auto next_periodic_check = std::chrono::steady_clock::now();

    auto next_poll = std::chrono::steady_clock::now();
    auto next_sim_check = std::chrono::steady_clock::now() + std::chrono::seconds(15);

    while (!restart_requested.load()) {
        const Config loop_config = snapshot_config();
        const auto now = std::chrono::steady_clock::now();
        const bool poll_due = now >= next_poll;
        {
            std::lock_guard<std::mutex> lock(modem_mutex);
            ModemEventBatch result;
            if (!backend->collect_events(result, poll_due, 100, &modem_error)) {
                fprintf(stderr, "模组事件读取失败: %s\n", modem_error.c_str());
            } else {
                process_modem_result(
                    *backend, result, dec, concat, calls,
                    [&](const std::string& sender, const std::string& text,
                        const std::string& timestamp) {
                        const Config current = snapshot_config();
                        handle_sms(current, worker, messages, runtime, admin_handler,
                                   sender, text,
                                   timestamp.empty() ? format_local_now() : timestamp);
                    });
            }
        }
        if (poll_due) {
            next_poll = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(loop_config.poll_interval_ms);
        }
        if (now >= next_sim_check) {
            SimState current = SimState::Unknown;
            bool queried = false;
            {
                std::lock_guard<std::mutex> lock(modem_mutex);
                queried = backend->query_sim_state(current, &modem_error);
            }
            if (queried && current != sim_state) {
                printf("SIM 状态变化: %s -> %s\n",
                       sim_state_name(sim_state), sim_state_name(current));
                const SimState previous = sim_state;
                sim_state = current;
                runtime.simState.store(static_cast<int>(current));
                if (current == SimState::Ready && previous != SimState::Ready) {
                    std::lock_guard<std::mutex> lock(modem_mutex);
                    if (!backend->initialize(&modem_error)) {
                        fprintf(stderr, "SIM 插入后模组重新初始化失败: %s\n", modem_error.c_str());
                    }
                }
            }
            next_sim_check = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        }
        concat.expire(mono_us());
        calls.expire(mono_us());
        process_admin_action();

        const uint32_t wall_now = static_cast<uint32_t>(std::time(nullptr));
        const auto scheduler_now = std::chrono::steady_clock::now();
        if (epoch_valid(wall_now) && scheduler_now >= next_periodic_check) {
            next_periodic_check = scheduler_now + std::chrono::hours(1);
            bool state_changed = false;
            bool action_ran = false;
            const PeriodicState keepalive_state = evaluate_periodic(
                loop_config.portable.keepalive.enabled, scheduler_state.keepaliveLast, wall_now,
                loop_config.portable.keepalive.intervalDays);
            if (keepalive_state == PeriodicState::EstablishBaseline) {
                scheduler_state.keepaliveLast = wall_now;
                state_changed = true;
                printf("保号基准日已建立（首次启用不执行动作）\n");
            } else if (keepalive_state == PeriodicState::Due) {
                const auto result = run_scheduled_action(
                    loop_config, loop_config.portable.keepalive.action,
                    loop_config.portable.keepalive.target, "",
                    loop_config.portable.keepalive.url, loop_config.portable.keepalive.profile,
                    "保号任务", true);
                action_ran = true;
                if (result.first) {
                    scheduler_state.keepaliveLast = wall_now;
                    state_changed = true;
                    notify_event(loop_config, worker, "保号动作已执行",
                                 "保号动作已成功执行。\n结果: " + result.second);
                } else if (!loop_config.portable.keepalive.profile.empty()) {
                    scheduler_state.keepaliveLast = failure_retry_baseline(
                        wall_now, loop_config.portable.keepalive.intervalDays);
                    state_changed = true;
                }
                printf("保号动作%s: %s\n", result.first ? "成功" : "失败", result.second.c_str());
            }

            if (!action_ran) {
                for (size_t i = 0; i < loop_config.portable.scheduledTasks.size(); ++i) {
                    const ScheduledTaskConfig& task = loop_config.portable.scheduledTasks[i];
                    const PeriodicState task_state = evaluate_periodic(
                        task.enabled, scheduler_state.taskLast[i], wall_now, task.intervalDays);
                    if (task_state == PeriodicState::EstablishBaseline) {
                        scheduler_state.taskLast[i] = wall_now;
                        state_changed = true;
                        continue;
                    }
                    if (task_state != PeriodicState::Due) continue;
                    const std::string label = task.name.empty()
                        ? ("定时任务" + std::to_string(i + 1)) : task.name;
                    const auto result = run_scheduled_action(
                        loop_config, task.action, task.target, task.payload, "", task.profile,
                        label, false);
                    scheduler_state.taskLast[i] = result.first
                        ? wall_now : failure_retry_baseline(wall_now, task.intervalDays);
                    state_changed = true;
                    if (task.action != 0 || !result.first) {
                        notify_event(loop_config, worker,
                                     result.first ? "定时任务已执行" : "定时任务失败",
                                     "任务: " + label + "\n结果: " + result.second);
                    }
                    printf("%s%s: %s\n", label.c_str(), result.first ? "完成" : "失败",
                           result.second.c_str());
                    break;
                }
            }
            if (state_changed) persist_scheduler_state();
        }

        if (epoch_valid(wall_now)) {
            const LocalDayHour local = local_day_hour(
                wall_now, loop_config.portable.time.timezoneOffsetMin);
            if (daily_due(loop_config.portable.time.heartbeatEnabled,
                          loop_config.portable.time.heartbeatHour,
                          scheduler_state.heartbeatLastDay, wall_now,
                          loop_config.portable.time.timezoneOffsetMin)) {
                scheduler_state.heartbeatLastDay = local.day;
                persist_scheduler_state();
                notify_event(loop_config, worker, "设备每日心跳",
                              "设备运行正常。\n累计处理: " +
                             std::to_string(runtime.smsTotal.load()) + " 条");
            }
            const int64_t uptime_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - runtime.started).count();
            const bool idle = worker.idle() && admin_actions.empty() && !admin_sms_busy.load();
            if (daily_reboot_due(loop_config.portable.time.rebootEnabled,
                                 loop_config.portable.time.rebootHour,
                                 scheduler_state.rebootLastDay, wall_now,
                                 loop_config.portable.time.timezoneOffsetMin, uptime_seconds, idle)) {
                scheduler_state.rebootLastDay = local.day;
                persist_scheduler_state();
                printf("每日定时重启 OmniSMS 服务\n");
                {
                    std::lock_guard<std::mutex> lock(modem_mutex);
                    if (!backend->reset(&modem_error)) {
                        fprintf(stderr, "每日模组重启失败: %s\n", modem_error.c_str());
                    }
                }
                restart_requested.store(true);
            }
        }
    }
    // systemd Restart=on-failure / OpenWrt procd respawn 会重新拉起服务；不擅自重启 Linux 主机。
    return restart_requested.load() ? 75 : 0;
}
