#include <cstdio>
#include <string>

#include "omnisms/admin.h"

using namespace omnisms;

static int failures = 0;
#define CHECK(value) do { if (!(value)) { \
    ++failures; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #value); \
} } while (0)

int main()
{
    CHECK(admin_sender_matches("+86 138-0013-8000", "13800138000"));
    CHECK(!admin_sender_matches("+8613800138000", "13800138001"));

    AdminCommandDecision d = evaluate_admin_command(
        "+8613800138000", "13800138000", " SMS:+8613900139000: hello:world ", 100, false, false);
    CHECK(d.status == AdminCommandStatus::SendSms);
    CHECK(d.target == "+8613900139000");
    CHECK(d.content == "hello:world");

    d = evaluate_admin_command("13800138000", "13800138000", "SMS:bad:value", 100, false, false);
    CHECK(d.status == AdminCommandStatus::InvalidTarget);
    d = evaluate_admin_command("13800138000", "13800138000", "SMS:10086:", 100, false, false);
    CHECK(d.status == AdminCommandStatus::InvalidContent);
    d = evaluate_admin_command("13800138000", "13800138000", "SMS:10086:test", 100, true, false);
    CHECK(d.status == AdminCommandStatus::SmsBusy);
    d = evaluate_admin_command("13800138000", "13800138000", "SMS:10086", 100, false, false);
    CHECK(d.status == AdminCommandStatus::SmsFormatError);

    d = evaluate_admin_command("13800138000", "13800138000", "RESET", 59, false, false);
    CHECK(d.status == AdminCommandStatus::ResetTooSoon);
    d = evaluate_admin_command("13800138000", "13800138000", "RESET", 60, false, true);
    CHECK(d.status == AdminCommandStatus::ResetPending);
    d = evaluate_admin_command("13800138000", "13800138000", "RESET", 60, false, false);
    CHECK(d.status == AdminCommandStatus::Reset);

    d = evaluate_admin_command("13800138000", "13800138000", "reset", 100, false, false);
    CHECK(d.status == AdminCommandStatus::Ignored);
    d = evaluate_admin_command("13800138000", "13800138001", "RESET", 100, false, false);
    CHECK(d.status == AdminCommandStatus::Ignored);

    if (failures == 0) {
        printf("all admin command tests passed\n");
        return 0;
    }
    return 1;
}
