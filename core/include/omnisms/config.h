// OmniSMS 跨平台配置 schema v1。
// JSON 是设备间迁移格式；平台可以保留本地旧格式，但必须能导入/导出本模型。
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "omnisms/push.h"
#include "omnisms/rules.h"

namespace omnisms {

constexpr int CONFIG_SCHEMA_VERSION = 1;
constexpr size_t MAX_SCHEDULED_TASKS = 6;

struct ModemConfig {
    std::string driver = "generic-3gpp";
    std::string endpoint;
    int baud = 115200;
    int pollIntervalMs = 3000;
};

struct EmailConfig {
    bool enabled = false;
    std::string server;
    int port = 465;
    std::string username;
    std::string password;
    std::string recipient;
};

struct StorageConfig {
    std::string inboxFile;
};

struct WifiConfig {
    std::string ssid;
    std::string password;
};

struct WebConfig {
    std::string username = "admin";
    std::string password = "admin123";
    std::string listenAddress = "127.0.0.1";
    int port = 8080;
    std::string assetRoot;
};

struct TimeConfig {
    int timezoneOffsetMin = 480;
    std::string ntpServer = "ntp.aliyun.com";
    bool rebootEnabled = false;
    int rebootHour = 4;
    bool heartbeatEnabled = false;
    int heartbeatHour = 9;
};

struct KeepaliveConfig {
    bool enabled = false;
    int intervalDays = 175;
    uint8_t action = 1;
    std::string target;
    std::string url;
    std::string profile;
    uint32_t lastRun = 0;
};

struct CellularConfig {
    bool dataEnabled = false;
    bool roamingEnabled = true;
    std::string apn;
    std::string operatorPlmn;
    bool netLedEnabled = true;
};

struct ScheduledTaskConfig {
    bool enabled = false;
    std::string name;
    std::string profile;
    bool switchBack = true;
    int intervalDays = 30;
    uint8_t action = 0;
    std::string target;
    std::string payload;
    uint32_t lastRun = 0;
};

// 解析时记录 JSON 实际出现的字段，便于设备将跨平台配置作为“合并导入”，
// 不因 Linux 配置缺少 WiFi/Web 等 ESP 专有段而把现有值重置为默认值。
struct ConfigPresence {
    bool modem = false;
    bool receiver = false;
    bool adminPhone = false;
    bool numberBlacklist = false;
    bool forwardRules = false;
    bool callNotifyEnabled = false;
    bool pushEnabled = false;
    bool pushChannels = false;
    bool email = false;
    bool storage = false;
    bool wifi = false;
    bool web = false;
    bool time = false;
    bool keepalive = false;
    bool cellular = false;
    bool scheduledTasks = false;
};

struct AppConfig {
    int schemaVersion = CONFIG_SCHEMA_VERSION;
    ModemConfig modem;
    std::string receiver;
    std::string adminPhone;
    std::string numberBlacklist;
    std::string forwardRules;
    bool callNotifyEnabled = true;
    bool pushEnabled = true;
    std::array<PushChannel, MAX_PUSH_CHANNELS> pushChannels = {};
    EmailConfig email;
    StorageConfig storage;
    WifiConfig wifi;
    WebConfig web;
    TimeConfig time;
    KeepaliveConfig keepalive;
    CellularConfig cellular;
    std::array<ScheduledTaskConfig, MAX_SCHEDULED_TASKS> scheduledTasks = {};
    ConfigPresence presence;
};

// 严格解析已知字段，忽略未知字段以允许同一 schema 内向前扩展。
// 不支持的 schemaVersion、错误类型、超出 5 个通道等会返回 false。
bool parse_config_json(const std::string& json, AppConfig& out, std::string* error = nullptr);

// 输出稳定、紧凑的 UTF-8 JSON，适合配置导出和跨设备迁移。
std::string serialize_config_json(const AppConfig& config);

}  // namespace omnisms
