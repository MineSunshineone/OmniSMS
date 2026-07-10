#include "omnisms/linux/config_store.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include "omnisms/rules.h"
#include "omnisms/text.h"

namespace omnisms::linux_port {
namespace {

using Fields = std::map<std::string, std::string>;

bool has(const Fields& fields, const std::string& key)
{
    return fields.find(key) != fields.end();
}

std::string field(const Fields& fields, const std::string& key)
{
    const auto found = fields.find(key);
    return found == fields.end() ? std::string() : found->second;
}

bool text_bool(const std::string& value)
{
    return value == "1" || value == "true" || value == "on" || value == "yes";
}

bool redacted(const std::string& value)
{
    return value == "__REDACTED__";
}

bool parse_integer(const Fields& fields, const std::string& key, int fallback,
                   int minimum, int maximum, int& out, std::string& error)
{
    const auto found = fields.find(key);
    if (found == fields.end() || trim(found->second).empty()) {
        out = fallback;
        return true;
    }
    const std::string raw = trim(found->second);
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(raw.c_str(), &end, 10);
    if (errno || !end || *end || parsed < minimum || parsed > maximum) {
        error = key + " 超出允许范围";
        return false;
    }
    out = static_cast<int>(parsed);
    return true;
}

bool validate(AppConfig& config, std::string& error)
{
    std::string rule_error;
    if (!validate_forward_rules(config.forwardRules, &rule_error)) {
        error = "转发规则格式错误";
        if (!rule_error.empty()) error += ": " + rule_error;
        return false;
    }
    AppConfig checked;
    if (!parse_config_json(serialize_config_json(config), checked, &error)) return false;
    config = std::move(checked);
    return true;
}

bool restart_only_changed(const AppConfig& before, const AppConfig& after)
{
    return before.modem.driver != after.modem.driver ||
           before.modem.endpoint != after.modem.endpoint ||
           before.modem.baud != after.modem.baud ||
           before.web.listenAddress != after.web.listenAddress ||
           before.web.port != after.web.port;
}

void preserve_redacted(std::string& next, const std::string& previous)
{
    if (redacted(next)) next = previous;
}

int merge_json_patch(AppConfig& target, const AppConfig& patch)
{
    int applied = 0;
    if (patch.presence.modem) { target.modem = patch.modem; ++applied; }
    if (patch.presence.receiver) { target.receiver = patch.receiver; ++applied; }
    if (patch.presence.adminPhone) { target.adminPhone = patch.adminPhone; ++applied; }
    if (patch.presence.numberBlacklist) { target.numberBlacklist = patch.numberBlacklist; ++applied; }
    if (patch.presence.forwardRules) { target.forwardRules = patch.forwardRules; ++applied; }
    if (patch.presence.callNotifyEnabled) {
        target.callNotifyEnabled = patch.callNotifyEnabled;
        ++applied;
    }
    if (patch.presence.pushEnabled) { target.pushEnabled = patch.pushEnabled; ++applied; }
    if (patch.presence.pushChannels) {
        for (size_t i = 0; i < target.pushChannels.size(); ++i) {
            PushChannel next = patch.pushChannels[i];
            preserve_redacted(next.url, target.pushChannels[i].url);
            preserve_redacted(next.key1, target.pushChannels[i].key1);
            preserve_redacted(next.key2, target.pushChannels[i].key2);
            preserve_redacted(next.customBody, target.pushChannels[i].customBody);
            target.pushChannels[i] = std::move(next);
        }
        ++applied;
    }
    if (patch.presence.email) {
        EmailConfig next = patch.email;
        preserve_redacted(next.password, target.email.password);
        target.email = std::move(next);
        ++applied;
    }
    if (patch.presence.storage) { target.storage = patch.storage; ++applied; }
    if (patch.presence.wifi) {
        WifiConfig next = patch.wifi;
        preserve_redacted(next.password, target.wifi.password);
        target.wifi = std::move(next);
        ++applied;
    }
    if (patch.presence.web) {
        WebConfig next = patch.web;
        preserve_redacted(next.password, target.web.password);
        target.web = std::move(next);
        ++applied;
    }
    if (patch.presence.time) { target.time = patch.time; ++applied; }
    if (patch.presence.keepalive) { target.keepalive = patch.keepalive; ++applied; }
    if (patch.presence.cellular) { target.cellular = patch.cellular; ++applied; }
    if (patch.presence.scheduledTasks) {
        target.scheduledTasks = patch.scheduledTasks;
        ++applied;
    }
    return applied;
}

std::string legacy_unescape(const std::string& value)
{
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size()) {
            const char next = value[i + 1];
            if (next == 'n') { out += '\n'; ++i; continue; }
            if (next == 'r') { out += '\r'; ++i; continue; }
            if (next == '\\') { out += '\\'; ++i; continue; }
        }
        out += value[i];
    }
    return out;
}

bool parse_legacy_int(const std::string& value, int& out)
{
    const std::string raw = trim(value);
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(raw.c_str(), &end, 10);
    if (errno || !end || *end || parsed < INT_MIN || parsed > INT_MAX) return false;
    out = static_cast<int>(parsed);
    return true;
}

bool parse_legacy_u32(const std::string& value, uint32_t& out)
{
    const std::string raw = trim(value);
    char* end = nullptr;
    errno = 0;
    const unsigned long parsed = std::strtoul(raw.c_str(), &end, 10);
    if (errno || !end || *end || parsed > UINT32_MAX) return false;
    out = static_cast<uint32_t>(parsed);
    return true;
}

bool apply_legacy_key(AppConfig& config, const std::string& key, const std::string& value)
{
    int integer = 0;
    uint32_t unsigned_integer = 0;
    if (key == "driver") config.modem.driver = value;
    else if (key == "device") config.modem.endpoint = value;
    else if (key == "baud" && parse_legacy_int(value, integer)) config.modem.baud = integer;
    else if (key == "poll_interval_ms" && parse_legacy_int(value, integer)) config.modem.pollIntervalMs = integer;
    else if (key == "receiver" || key == "phoneNumber") config.receiver = value;
    else if (key == "adminPhone") config.adminPhone = value;
    else if (key == "numBlkList" || key == "numberBlackList") config.numberBlacklist = value;
    else if (key == "fwdRules" || key == "forwardRules") config.forwardRules = value;
    else if (key == "callNotifyEnabled") config.callNotifyEnabled = text_bool(value);
    else if (key == "pushEnabled") config.pushEnabled = text_bool(value);
    else if (key == "emailEnabled") config.email.enabled = text_bool(value);
    else if (key == "smtpServer") config.email.server = value;
    else if (key == "smtpPort" && parse_legacy_int(value, integer)) config.email.port = integer;
    else if (key == "smtpUser") config.email.username = value;
    else if (key == "smtpPass") { if (!redacted(value)) config.email.password = value; }
    else if (key == "smtpSendTo") config.email.recipient = value;
    else if (key == "inbox_file") config.storage.inboxFile = value;
    else if (key == "wifiSsid") config.wifi.ssid = value;
    else if (key == "wifiPass") { if (!redacted(value)) config.wifi.password = value; }
    else if (key == "webUser" || key == "web_username") { if (!trim(value).empty()) config.web.username = value; }
    else if (key == "webPass" || key == "web_password") {
        if (!trim(value).empty() && !redacted(value)) config.web.password = value;
    }
    else if (key == "web_listen_address") config.web.listenAddress = value;
    else if (key == "web_port" && parse_legacy_int(value, integer)) config.web.port = integer;
    else if (key == "web_asset_root") config.web.assetRoot = value;
    else if (key == "tzOffsetMin" && parse_legacy_int(value, integer)) config.time.timezoneOffsetMin = integer;
    else if (key == "ntpServer") config.time.ntpServer = value;
    else if (key == "rebootEnabled") config.time.rebootEnabled = text_bool(value);
    else if (key == "rebootHour" && parse_legacy_int(value, integer)) config.time.rebootHour = integer;
    else if (key == "hbEnabled") config.time.heartbeatEnabled = text_bool(value);
    else if (key == "hbHour" && parse_legacy_int(value, integer)) config.time.heartbeatHour = integer;
    else if (key == "kaEnabled") config.keepalive.enabled = text_bool(value);
    else if (key == "kaIntervalDays" && parse_legacy_int(value, integer)) config.keepalive.intervalDays = integer;
    else if (key == "kaAction" && parse_legacy_int(value, integer)) config.keepalive.action = static_cast<uint8_t>(integer);
    else if (key == "kaTarget") config.keepalive.target = value;
    else if (key == "kaUrl") config.keepalive.url = value;
    else if (key == "kaProfile") config.keepalive.profile = value;
    else if (key == "kaLastTime" && parse_legacy_u32(value, unsigned_integer)) config.keepalive.lastRun = unsigned_integer;
    else if (key == "netLedEnabled") config.cellular.netLedEnabled = text_bool(value);
    else if (key == "dataEnabled") config.cellular.dataEnabled = text_bool(value);
    else if (key == "roamingEnabled") config.cellular.roamingEnabled = text_bool(value);
    else if (key == "apn") config.cellular.apn = value;
    else if (key == "operatorPlmn") config.cellular.operatorPlmn = value;
    else if (key.rfind("st", 0) == 0 && key.size() > 3 && key[2] >= '0' && key[2] <= '9') {
        const int index = key[2] - '0';
        if (index >= static_cast<int>(config.scheduledTasks.size())) return false;
        ScheduledTaskConfig& task = config.scheduledTasks[index];
        const std::string suffix = key.substr(3);
        if (suffix == "En") task.enabled = text_bool(value);
        else if (suffix == "Name") task.name = value;
        else if (suffix == "Prof") task.profile = value;
        else if (suffix == "Back") task.switchBack = text_bool(value);
        else if (suffix == "Days" && parse_legacy_int(value, integer)) task.intervalDays = integer;
        else if (suffix == "Act" && parse_legacy_int(value, integer)) task.action = static_cast<uint8_t>(integer);
        else if (suffix == "Tgt") task.target = value;
        else if (suffix == "Pay") task.payload = value;
        else if (suffix == "Last" && parse_legacy_u32(value, unsigned_integer)) task.lastRun = unsigned_integer;
        else return false;
    }
    else if (key.rfind("push", 0) == 0 && key.size() > 5 && key[4] >= '0' && key[4] <= '9') {
        const int index = key[4] - '0';
        if (index >= static_cast<int>(config.pushChannels.size())) return false;
        PushChannel& channel = config.pushChannels[index];
        const std::string suffix = key.substr(5);
        if (suffix == "en") channel.enabled = text_bool(value);
        else if (suffix == "type" && parse_legacy_int(value, integer)) channel.type = static_cast<uint8_t>(integer);
        else if (suffix == "url") { if (!redacted(value)) channel.url = value; }
        else if (suffix == "name") channel.name = value;
        else if (suffix == "k1") { if (!redacted(value)) channel.key1 = value; }
        else if (suffix == "k2") { if (!redacted(value)) channel.key2 = value; }
        else if (suffix == "body") { if (!redacted(value)) channel.customBody = value; }
        else return false;
    }
    else if (key == "webhook_url") {
        config.pushChannels[0].enabled = true;
        config.pushChannels[0].type = PUSH_TYPE_POST_JSON;
        config.pushChannels[0].url = value;
    }
    else return false;
    return true;
}

bool write_all(int fd, const std::string& data)
{
    size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t written = ::write(fd, data.data() + offset, data.size() - offset);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return false;
        offset += static_cast<size_t>(written);
    }
    return true;
}

std::string parent_directory(const std::string& path)
{
    const size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) return ".";
    if (slash == 0) return "/";
    return path.substr(0, slash);
}

}  // namespace

ConfigUpdateResult apply_web_config_form(const AppConfig& current, const Fields& fields)
{
    ConfigUpdateResult result;
    result.config = current;
    static const char* markers[] = {
        "accountForm", "tzForm", "ledForm", "emailForm", "pushForm", "filterForm",
        "rulesForm", "kaForm", "stForm", "systemSchedForm", "simForm", "callForm"
    };
    std::string selected;
    for (const char* marker : markers) {
        if (!has(fields, marker)) continue;
        if (!selected.empty()) {
            result.message = "保存请求包含多个表单标记";
            return result;
        }
        selected = marker;
    }
    if (selected.empty()) {
        result.message = "保存请求缺少表单标记";
        return result;
    }

    AppConfig& next = result.config;
    std::string error;
    int number = 0;
    if (selected == "accountForm") {
        const std::string username = trim(field(fields, "webUser"));
        if (username.empty()) { result.message = "管理账号不能为空"; return result; }
        next.web.username = username;
        const std::string password = field(fields, "webPass");
        if (!password.empty()) next.web.password = password;
        result.message = "管理账号已保存并热加载";
    } else if (selected == "tzForm") {
        if (!parse_integer(fields, "tzOffsetMin", 480, -720, 840, number, error)) {
            result.message = error; return result;
        }
        next.time.timezoneOffsetMin = number;
        next.time.ntpServer = field(fields, "ntpServer");
        result.message = "时间设置已保存并热加载";
    } else if (selected == "ledForm") {
        next.cellular.netLedEnabled = has(fields, "netLedEnabled");
        result.message = "NET 指示灯偏好已保存；当前 Linux 通用 AT 驱动不直接控制该指示灯";
    } else if (selected == "callForm") {
        next.callNotifyEnabled = has(fields, "callNotifyEnabled");
        result.message = "来电通知设置已保存并热加载";
    } else if (selected == "emailForm") {
        if (!parse_integer(fields, "smtpPort", 465, 1, 65535, number, error)) {
            result.message = error; return result;
        }
        next.email.enabled = has(fields, "emailEnabled");
        next.email.server = field(fields, "smtpServer");
        next.email.port = number;
        next.email.username = field(fields, "smtpUser");
        const std::string password = field(fields, "smtpPass");
        if (!password.empty()) next.email.password = password;
        next.email.recipient = field(fields, "smtpSendTo");
        result.message = "邮件设置已保存并热加载";
    } else if (selected == "pushForm") {
        next.pushEnabled = has(fields, "pushEnabled");
        for (size_t i = 0; i < next.pushChannels.size(); ++i) {
            const std::string prefix = "push" + std::to_string(i);
            PushChannel updated;
            updated.enabled = has(fields, prefix + "en");
            if (!parse_integer(fields, prefix + "type", 1, 1, 10, number, error)) {
                result.message = error; return result;
            }
            updated.type = static_cast<uint8_t>(number);
            updated.url = field(fields, prefix + "url");
            updated.name = field(fields, prefix + "name");
            updated.customBody = field(fields, prefix + "body");
            for (int slot = 1; slot <= 2; ++slot) {
                const std::string name = prefix + "key" + std::to_string(slot);
                const bool clear = has(fields, name + "Clear");
                const bool keep = field(fields, name + "Keep") == "1";
                std::string value = field(fields, name);
                if (clear) value.clear();
                else if (keep && updated.type == current.pushChannels[i].type && value.empty()) {
                    value = slot == 1 ? current.pushChannels[i].key1 : current.pushChannels[i].key2;
                }
                if (slot == 1) updated.key1 = std::move(value);
                else updated.key2 = std::move(value);
            }
            next.pushChannels[i] = std::move(updated);
        }
        result.message = "推送通道已保存并热加载";
    } else if (selected == "filterForm") {
        if (has(fields, "adminPhone")) next.adminPhone = field(fields, "adminPhone");
        if (has(fields, "numberBlackList")) next.numberBlacklist = field(fields, "numberBlackList");
        result.message = "权限与过滤设置已保存并热加载";
    } else if (selected == "rulesForm") {
        next.forwardRules = field(fields, "forwardRules");
        result.message = "转发规则已保存并热加载";
    } else if (selected == "kaForm") {
        if (!parse_integer(fields, "kaIntervalDays", 175, 1, 3650, number, error)) {
            result.message = error; return result;
        }
        next.keepalive.enabled = has(fields, "kaEnabled");
        next.keepalive.intervalDays = number;
        if (!parse_integer(fields, "kaAction", 1, 1, 3, number, error)) {
            result.message = error; return result;
        }
        next.keepalive.action = static_cast<uint8_t>(number);
        next.keepalive.target = field(fields, "kaTarget");
        next.keepalive.url = field(fields, "kaUrl");
        next.keepalive.profile = field(fields, "kaProfile");
        result.message = "SIM 保号设置已保存并热加载";
    } else if (selected == "stForm") {
        for (size_t i = 0; i < next.scheduledTasks.size(); ++i) {
            const std::string prefix = "st" + std::to_string(i);
            ScheduledTaskConfig updated;
            updated.enabled = has(fields, prefix + "En");
            updated.name = field(fields, prefix + "Name");
            updated.profile = field(fields, prefix + "Prof");
            updated.switchBack = has(fields, prefix + "Back");
            if (!parse_integer(fields, prefix + "Days", 30, 1, 3650, number, error)) {
                result.message = error; return result;
            }
            updated.intervalDays = number;
            if (!parse_integer(fields, prefix + "Act", 0, 0, 3, number, error)) {
                result.message = error; return result;
            }
            updated.action = static_cast<uint8_t>(number);
            updated.target = field(fields, prefix + "Tgt");
            updated.payload = field(fields, prefix + "Pay");
            updated.lastRun = current.scheduledTasks[i].lastRun;
            next.scheduledTasks[i] = std::move(updated);
        }
        result.message = "自定义定时任务已保存并热加载";
    } else if (selected == "systemSchedForm") {
        next.time.rebootEnabled = has(fields, "rebootEnabled");
        if (!parse_integer(fields, "rebootHour", 4, 0, 23, number, error)) {
            result.message = error; return result;
        }
        next.time.rebootHour = number;
        next.time.heartbeatEnabled = has(fields, "hbEnabled");
        if (!parse_integer(fields, "hbHour", 9, 0, 23, number, error)) {
            result.message = error; return result;
        }
        next.time.heartbeatHour = number;
        result.message = "系统定时设置已保存并热加载";
    } else if (selected == "simForm") {
        next.cellular.dataEnabled = has(fields, "dataEnabled");
        next.cellular.roamingEnabled = has(fields, "roamingEnabled");
        next.cellular.apn = field(fields, "apn");
        next.cellular.operatorPlmn = field(fields, "operatorPlmn");
        next.receiver = field(fields, "phoneNumber");
        result.message = "蜂窝设置已保存；通用 Linux AT 驱动不会在线切换 PDP/APN/运营商";
    }

    if (!validate(next, error)) { result.message = error; return result; }
    result.success = true;
    result.applied = 1;
    result.restartRequired = restart_only_changed(current, next);
    return result;
}

ConfigUpdateResult import_portable_config(const AppConfig& current, const std::string& raw)
{
    ConfigUpdateResult result;
    result.config = current;
    const size_t first = raw.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        result.message = "导入内容为空";
        return result;
    }

    std::string error;
    if (raw[first] == '{') {
        AppConfig patch;
        if (!parse_config_json(raw, patch, &error)) {
            result.message = "JSON 配置无效: " + error;
            return result;
        }
        result.applied = merge_json_patch(result.config, patch);
    } else {
        std::istringstream lines(raw);
        std::string line;
        while (std::getline(lines, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const size_t equal = line.find('=');
            if (equal == std::string::npos || equal == 0) continue;
            const std::string key = trim(line.substr(0, equal));
            const std::string value = legacy_unescape(line.substr(equal + 1));
            if (!key.empty() && apply_legacy_key(result.config, key, value)) ++result.applied;
        }
    }
    if (result.applied == 0) {
        result.message = "导入内容没有可识别的配置项";
        return result;
    }
    if (!validate(result.config, error)) {
        result.message = "导入配置无效: " + error;
        return result;
    }
    result.success = true;
    result.restartRequired = restart_only_changed(current, result.config);
    result.message = "已导入 " + std::to_string(result.applied) + " 项并热加载";
    if (result.restartRequired) result.message += "；串口/驱动或监听地址变更需重启服务后生效";
    return result;
}

bool atomic_write_file(const std::string& path, const std::string& data, std::string* error)
{
    if (error) error->clear();
    if (path.empty()) {
        if (error) *error = "配置路径为空";
        return false;
    }
    struct stat existing{};
    const bool had_existing = ::stat(path.c_str(), &existing) == 0;
    const mode_t mode = had_existing ? static_cast<mode_t>(existing.st_mode & 0777) : 0600;
    const std::string temporary = path + ".tmp." + std::to_string(static_cast<long long>(::getpid()));
    const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd < 0) {
        if (error) *error = std::strerror(errno);
        return false;
    }
    bool ok = ::fchmod(fd, mode) == 0 && write_all(fd, data) && ::fsync(fd) == 0;
    int saved_errno = errno;
    if (::close(fd) != 0 && ok) { ok = false; saved_errno = errno; }
    if (ok && ::rename(temporary.c_str(), path.c_str()) != 0) {
        ok = false;
        saved_errno = errno;
    }
    if (!ok) {
        ::unlink(temporary.c_str());
        if (error) *error = std::strerror(saved_errno);
        return false;
    }
    const std::string parent = parent_directory(path);
#ifdef O_DIRECTORY
    const int directory = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY);
#else
    const int directory = ::open(parent.c_str(), O_RDONLY);
#endif
    if (directory >= 0) {
        ::fsync(directory);
        ::close(directory);
    }
    return true;
}

bool atomic_write_config(const std::string& path, const AppConfig& config, std::string* error)
{
    return atomic_write_file(path, serialize_config_json(config) + "\n", error);
}

}  // namespace omnisms::linux_port
