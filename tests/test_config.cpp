#include <cstdio>
#include <string>

#include "omnisms/config.h"

using namespace omnisms;

static int g_failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        ++g_failures; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

static void test_roundtrip()
{
    AppConfig source;
    source.modem.driver = "ec200";
    source.modem.endpoint = "/dev/serial/by-id/modem\"1";
    source.modem.baud = 921600;
    source.modem.pollIntervalMs = 1500;
    source.receiver = "+8613800138000";
    source.adminPhone = "+8613900139000";
    source.numberBlacklist = "10086\n+44123";
    source.forwardRules = "kw\t验证码\t1,email";
    source.callNotifyEnabled = false;
    source.pushEnabled = false;
    source.pushChannels[0].enabled = true;
    source.pushChannels[0].type = PUSH_TYPE_BARK;
    source.pushChannels[0].name = "我的 iPhone";
    source.pushChannels[0].url = "https://bark.example.com";
    source.pushChannels[0].key1 = "device-key";
    source.pushChannels[1].enabled = true;
    source.pushChannels[1].type = PUSH_TYPE_CUSTOM;
    source.pushChannels[1].url = "https://example.com/hook";
    source.pushChannels[1].customBody = "{\"text\":\"{message}\"}";
    source.email.enabled = true;
    source.email.server = "smtp.example.com";
    source.email.port = 587;
    source.email.username = "bot@example.com";
    source.email.password = "p\\\"ass\nword";
    source.email.recipient = "me@example.com";
    source.storage.inboxFile = "/var/lib/omnisms/inbox.jsonl";
    source.wifi.ssid = "Home WiFi";
    source.wifi.password = "wifi-secret";
    source.web.username = "owner";
    source.web.password = "web-secret";
    source.web.listenAddress = "0.0.0.0";
    source.web.port = 9090;
    source.web.assetRoot = "/usr/share/omnisms/webui";
    source.time.timezoneOffsetMin = 330;
    source.time.ntpServer = "pool.ntp.org";
    source.time.rebootEnabled = true;
    source.time.rebootHour = 3;
    source.time.heartbeatEnabled = true;
    source.time.heartbeatHour = 10;
    source.keepalive.enabled = true;
    source.keepalive.intervalDays = 90;
    source.keepalive.action = 2;
    source.keepalive.target = "10086";
    source.keepalive.url = "https://example.com/ping";
    source.keepalive.profile = "profile-a";
    source.keepalive.lastRun = 4000000000U;
    source.cellular.dataEnabled = true;
    source.cellular.roamingEnabled = false;
    source.cellular.apn = "internet";
    source.cellular.operatorPlmn = "46000";
    source.cellular.netLedEnabled = false;
    source.scheduledTasks[0].enabled = true;
    source.scheduledTasks[0].name = "monthly";
    source.scheduledTasks[0].profile = "profile-b";
    source.scheduledTasks[0].switchBack = false;
    source.scheduledTasks[0].intervalDays = 31;
    source.scheduledTasks[0].action = 3;
    source.scheduledTasks[0].target = "*100#";
    source.scheduledTasks[0].payload = "query";
    source.scheduledTasks[0].lastRun = 3999999999U;

    std::string json = serialize_config_json(source);
    AppConfig parsed;
    std::string error;
    CHECK(parse_config_json(json, parsed, &error));
    CHECK(error.empty());
    CHECK(parsed.schemaVersion == CONFIG_SCHEMA_VERSION);
    CHECK(parsed.presence.receiver && parsed.presence.wifi && parsed.presence.scheduledTasks);
    CHECK(parsed.modem.driver == source.modem.driver);
    CHECK(parsed.modem.endpoint == source.modem.endpoint);
    CHECK(parsed.modem.baud == source.modem.baud);
    CHECK(parsed.receiver == source.receiver);
    CHECK(parsed.adminPhone == source.adminPhone);
    CHECK(parsed.numberBlacklist == source.numberBlacklist);
    CHECK(parsed.forwardRules == source.forwardRules);
    CHECK(parsed.callNotifyEnabled == source.callNotifyEnabled);
    CHECK(parsed.pushEnabled == source.pushEnabled);
    CHECK(parsed.pushChannels[0].type == PUSH_TYPE_BARK);
    CHECK(parsed.pushChannels[0].name == source.pushChannels[0].name);
    CHECK(parsed.pushChannels[1].customBody == source.pushChannels[1].customBody);
    CHECK(parsed.email.password == source.email.password);
    CHECK(parsed.storage.inboxFile == source.storage.inboxFile);
    CHECK(parsed.wifi.password == source.wifi.password);
    CHECK(parsed.web.username == source.web.username);
    CHECK(parsed.web.listenAddress == source.web.listenAddress && parsed.web.port == 9090);
    CHECK(parsed.time.timezoneOffsetMin == source.time.timezoneOffsetMin);
    CHECK(parsed.time.heartbeatHour == source.time.heartbeatHour);
    CHECK(parsed.keepalive.lastRun == source.keepalive.lastRun);
    CHECK(parsed.cellular.operatorPlmn == source.cellular.operatorPlmn);
    CHECK(parsed.scheduledTasks[0].action == source.scheduledTasks[0].action);
    CHECK(parsed.scheduledTasks[0].lastRun == source.scheduledTasks[0].lastRun);
    CHECK(serialize_config_json(parsed) == json);
}

static void test_compatibility_and_validation()
{
    AppConfig config;
    std::string error;
    const std::string extensible =
        "{\"schemaVersion\":1,\"receiver\":\"\\u4e2d\\u6587\","
        "\"future\":{\"nested\":[1,true,null,{\"emoji\":\"\\ud83d\\ude80\"}]}}";
    CHECK(parse_config_json(extensible, config, &error));
    CHECK(config.receiver == "中文");
    CHECK(config.presence.receiver && !config.presence.wifi && !config.presence.email);

    CHECK(!parse_config_json("{\"receiver\":\"x\"}", config, &error));
    CHECK(error.find("schemaVersion") != std::string::npos);
    CHECK(!parse_config_json("{\"schemaVersion\":2}", config, &error));
    CHECK(!parse_config_json("{\"schemaVersion\":1,\"email\":{\"port\":70000}}",
                             config, &error));
    CHECK(!parse_config_json("{\"schemaVersion\":1,\"pushChannels\":["
                             "{},{},{},{},{},{}]}", config, &error));
    CHECK(!parse_config_json("{\"schemaVersion\":1,\"callNotifyEnabled\":1}",
                             config, &error));
    CHECK(!parse_config_json("{\"schemaVersion\":1,\"time\":{\"rebootHour\":24}}",
                             config, &error));
    CHECK(!parse_config_json("{\"schemaVersion\":1,\"keepalive\":{\"lastRun\":4294967296}}",
                             config, &error));
    CHECK(!parse_config_json("{\"schemaVersion\":1,\"scheduledTasks\":["
                             "{},{},{},{},{},{},{}]}", config, &error));
    CHECK(!parse_config_json("{\"schemaVersion\":1} trailing", config, &error));
}

int main()
{
    test_roundtrip();
    test_compatibility_and_validation();
    if (g_failures == 0) {
        printf("all config tests passed\n");
        return 0;
    }
    fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
}
