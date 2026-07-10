// crypto + push 报文组装单测
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#include "omnisms/crypto.h"
#include "omnisms/push.h"

using namespace omnisms;

static int g_failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        ++g_failures; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

static std::string hex(const uint8_t* d, size_t n)
{
    static const char* t = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < n; ++i) { out += t[d[i] >> 4]; out += t[d[i] & 15]; }
    return out;
}

static void test_crypto()
{
    uint8_t d[32];
    // FIPS 180-4 向量
    sha256(reinterpret_cast<const uint8_t*>(""), 0, d);
    CHECK(hex(d, 32) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    sha256(reinterpret_cast<const uint8_t*>("abc"), 3, d);
    CHECK(hex(d, 32) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    // 跨块边界(>64 字节)
    std::string s2 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    sha256(reinterpret_cast<const uint8_t*>(s2.data()), s2.size(), d);
    CHECK(hex(d, 32) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    // HMAC-SHA256 已知向量
    hmac_sha256(reinterpret_cast<const uint8_t*>("key"), 3,
                reinterpret_cast<const uint8_t*>("The quick brown fox jumps over the lazy dog"), 43,
                d);
    CHECK(hex(d, 32) == "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8");

    CHECK(base64_encode(reinterpret_cast<const uint8_t*>("Man"), 3) == "TWFu");
    CHECK(base64_encode(reinterpret_cast<const uint8_t*>("Ma"), 2) == "TWE=");
    CHECK(base64_encode(reinterpret_cast<const uint8_t*>("M"), 1) == "TQ==");
    std::string decoded;
    CHECK(base64_decode("TWFu", decoded) && decoded == "Man");
    CHECK(base64_decode("TWE=", decoded) && decoded == "Ma");
    CHECK(base64_decode("TQ==", decoded) && decoded == "M");
    CHECK(!base64_decode("T===", decoded));
    CHECK(!base64_decode("TR==", decoded));
}

static void test_push_payloads()
{
    SmsEvent ev{"+8610086", "余额 10 元", "2026-07-10 12:00:00", "+8613800138000"};
    HttpRequest req;

    // POST JSON
    PushChannel ch;
    ch.enabled = true;
    ch.type = PUSH_TYPE_POST_JSON;
    ch.url = "https://example.com/hook";
    CHECK(build_push_request(ch, ev, false, 0, req));
    CHECK(req.method == "POST" && req.url == ch.url);
    CHECK(req.body == "{\"sender\":\"+8610086\",\"receiver\":\"+8613800138000\","
                      "\"message\":\"余额 10 元\",\"timestamp\":\"2026-07-10 12:00:00\"}");

    // GET：参数 URL 编码
    ch.type = PUSH_TYPE_GET;
    CHECK(build_push_request(ch, ev, false, 0, req));
    CHECK(req.method == "GET");
    CHECK(req.url.find("sender=%2B8610086") != std::string::npos);

    // Bark：key1 → 官方端点 /push，device_key 进请求体
    ch.type = PUSH_TYPE_BARK;
    ch.url.clear();
    ch.key1 = "devkey123";
    CHECK(build_push_request(ch, ev, false, 0, req));
    CHECK(req.url == "https://api.day.app/push");
    CHECK(req.body.find("\"device_key\":\"devkey123\"") != std::string::npos);
    CHECK(req.body.find("本机号码") != std::string::npos);

    // 钉钉加签：URL 追加 timestamp+sign
    ch.type = PUSH_TYPE_DINGTALK;
    ch.url = "https://oapi.dingtalk.com/robot/send?access_token=t";
    ch.key1 = "SECxxx";
    CHECK(build_push_request(ch, ev, false, 1720584000000LL, req));
    CHECK(req.url.find("&timestamp=1720584000000&sign=") != std::string::npos);
    CHECK(req.body.find("短信通知") != std::string::npos);

    // 飞书：秒级时间戳 + sign 字段
    ch.type = PUSH_TYPE_FEISHU;
    ch.url = "https://open.feishu.cn/open-apis/bot/v2/hook/x";
    CHECK(build_push_request(ch, ev, false, 1720584000000LL, req));
    CHECK(req.body.find("\"timestamp\":\"1720584000\"") != std::string::npos);
    CHECK(req.body.find("\"sign\":\"") != std::string::npos);

    // Telegram：URL 由 key2 拼接
    ch.type = PUSH_TYPE_TELEGRAM;
    ch.url.clear();
    ch.key1 = "12345";
    ch.key2 = "BOT:TOKEN";
    CHECK(build_push_request(ch, ev, false, 0, req));
    CHECK(req.url == "https://api.telegram.org/botBOT:TOKEN/sendMessage");
    CHECK(req.body.find("\"chat_id\":\"12345\"") != std::string::npos);

    // 自定义模板：占位符展开(值已 JSON 转义)
    ch.type = PUSH_TYPE_CUSTOM;
    ch.url = "https://example.com/c";
    ch.customBody = "{\"from\":\"{sender}\",\"msg\":\"{message}\",\"to\":\"{local_number}\"}";
    CHECK(build_push_request(ch, ev, false, 0, req));
    CHECK(req.body == "{\"from\":\"+8610086\",\"msg\":\"余额 10 元\",\"to\":\"+8613800138000\"}");

    // 无效通道
    PushChannel bad;
    bad.enabled = true;
    bad.type = PUSH_TYPE_TELEGRAM;  // 缺 key1/key2
    CHECK(!channel_valid(bad));
    CHECK(!build_push_request(bad, ev, false, 0, req));

    // notify 模式：标题即任务名，无"短信来自"模板、无本机号码
    ch.type = PUSH_TYPE_POST_JSON;
    ch.url = "https://example.com/hook";
    SmsEvent task{"每日提醒", "内容", "2026-07-10 08:00:00", "+8613800138000"};
    CHECK(build_push_request(ch, task, true, 0, req));
    CHECK(req.body.find("\"receiver\":\"\"") != std::string::npos);
}

static void test_backoff()
{
    CHECK(backoff_seconds(1, 0) == 20);
    CHECK(backoff_seconds(2, 0) == 40);
    uint32_t v = backoff_seconds(6, 123);
    CHECK(v >= 600 && v <= 750);  // 封顶 600 + 1/4 抖动
}

int main()
{
    test_crypto();
    test_push_payloads();
    test_backoff();
    if (g_failures == 0) {
        printf("all push tests passed\n");
        return 0;
    }
    fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
}
