#include <cstdio>

#include "omnisms/email.h"

using namespace omnisms;

static int g_failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        ++g_failures; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

int main()
{
    EmailConfig config;
    config.enabled = true;
    config.server = "smtp.example.com";
    config.username = "bot@example.com";
    config.password = "secret";
    CHECK(email_configured(config));
    config.password.clear();
    CHECK(!email_configured(config));

    SmsEvent event{"10086", "验证码 123456", "2026-07-10 21:00:00", "+8613800138000"};
    const EmailContent sms = build_sms_email(event);
    CHECK(sms.subject == "短信10086,验证码 123456");
    CHECK(sms.body.find("来自：10086") != std::string::npos);
    CHECK(sms.body.find("本机号码：+8613800138000") != std::string::npos);
    CHECK(sms.body.find("内容：验证码 123456") != std::string::npos);

    const EmailContent notice = build_notification_email("每日心跳", "运行正常");
    CHECK(notice.subject == "每日心跳" && notice.body == "运行正常");

    if (g_failures == 0) {
        printf("all email tests passed\n");
        return 0;
    }
    fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
}
