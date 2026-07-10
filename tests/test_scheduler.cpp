#include <cstdio>

#include "omnisms/scheduler.h"

using namespace omnisms;

static int failures = 0;
#define CHECK(value) do { if (!(value)) { \
    ++failures; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #value); \
} } while (0)

int main()
{
    constexpr uint32_t now = 1783684800u;
    CHECK(!epoch_valid(0));
    CHECK(epoch_valid(now));
    CHECK(!interval_due(now, now, 1));
    CHECK(interval_due(now - 86400u, now, 1));
    CHECK(!interval_due(now + 1, now, 1));
    CHECK(interval_due(0, now, 30));

    CHECK(evaluate_periodic(false, 0, now, 30) == PeriodicState::Disabled);
    CHECK(evaluate_periodic(true, 0, 100, 30) == PeriodicState::InvalidClock);
    CHECK(evaluate_periodic(true, 0, now, 30) == PeriodicState::EstablishBaseline);
    CHECK(evaluate_periodic(true, now - 10, now, 30) == PeriodicState::NotDue);
    CHECK(evaluate_periodic(true, now - 30u * 86400u, now, 30) == PeriodicState::Due);

    CHECK(failure_retry_baseline(now, 30) == now - 29u * 86400u);
    CHECK(failure_retry_baseline(now, 1) == now);
    CHECK(failure_retry_baseline(100, 30) == 0);

    const LocalDayHour utc = local_day_hour(now, 0);
    const LocalDayHour east = local_day_hour(now, 480);
    CHECK(utc.valid && east.valid);
    CHECK(east.day == utc.day || east.day == utc.day + 1);
    CHECK(east.hour == (utc.hour + 8) % 24);
    CHECK(!local_day_hour(100, 480).valid);

    CHECK(daily_due(true, east.hour, east.day - 1, now, 480));
    CHECK(!daily_due(true, east.hour, east.day, now, 480));
    CHECK(!daily_due(true, east.hour, east.day + 1, now, 480));
    CHECK(!daily_due(true, (east.hour + 1) % 24, east.day - 1, now, 480));
    CHECK(!daily_reboot_due(true, east.hour, east.day - 1, now, 480, 7199, true));
    CHECK(!daily_reboot_due(true, east.hour, east.day - 1, now, 480, 7200, false));
    CHECK(daily_reboot_due(true, east.hour, east.day - 1, now, 480, 7200, true));

    if (failures == 0) {
        printf("all scheduler tests passed\n");
        return 0;
    }
    return 1;
}
