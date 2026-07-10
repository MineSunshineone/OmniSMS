#include "omnisms/scheduler.h"

namespace omnisms {

bool epoch_valid(uint32_t epoch)
{
    return epoch >= VALID_EPOCH_MIN;
}

bool interval_due(uint32_t last_epoch, uint32_t now_epoch, uint32_t interval_days)
{
    if (!epoch_valid(now_epoch) || interval_days == 0) return false;
    if (!epoch_valid(last_epoch)) return true;
    if (last_epoch > now_epoch) return false;
    return static_cast<uint64_t>(now_epoch - last_epoch) >=
           static_cast<uint64_t>(interval_days) * 86400ULL;
}

PeriodicState evaluate_periodic(bool enabled, uint32_t last_epoch, uint32_t now_epoch,
                                int interval_days)
{
    if (!enabled || interval_days <= 0) return PeriodicState::Disabled;
    if (!epoch_valid(now_epoch)) return PeriodicState::InvalidClock;
    if (!epoch_valid(last_epoch)) return PeriodicState::EstablishBaseline;
    return interval_due(last_epoch, now_epoch, static_cast<uint32_t>(interval_days))
        ? PeriodicState::Due : PeriodicState::NotDue;
}

uint32_t failure_retry_baseline(uint32_t now_epoch, int interval_days)
{
    if (!epoch_valid(now_epoch)) return 0;
    const uint32_t days = interval_days > 0 ? static_cast<uint32_t>(interval_days) : 1u;
    const uint64_t back = static_cast<uint64_t>(days - 1) * 86400ULL;
    return back < now_epoch ? now_epoch - static_cast<uint32_t>(back) : now_epoch;
}

LocalDayHour local_day_hour(uint32_t now_epoch, int timezone_offset_min)
{
    LocalDayHour result;
    if (!epoch_valid(now_epoch)) return result;
    const int64_t local = static_cast<int64_t>(now_epoch) +
                          static_cast<int64_t>(timezone_offset_min) * 60LL;
    result.hour = static_cast<int>((local / 3600LL) % 24LL);
    if (result.hour < 0) result.hour += 24;
    result.day = local / 86400LL;
    if (local < 0 && (local % 86400LL) != 0) --result.day;
    result.valid = true;
    return result;
}

bool daily_due(bool enabled, int target_hour, int64_t last_day,
               uint32_t now_epoch, int timezone_offset_min)
{
    if (!enabled || target_hour < 0 || target_hour > 23) return false;
    const LocalDayHour local = local_day_hour(now_epoch, timezone_offset_min);
    return local.valid && local.hour == target_hour && local.day > last_day;
}

bool daily_reboot_due(bool enabled, int target_hour, int64_t last_day,
                      uint32_t now_epoch, int timezone_offset_min,
                      int64_t uptime_seconds, bool system_idle)
{
    return system_idle && uptime_seconds >= DAILY_REBOOT_MIN_UPTIME_SECONDS &&
           daily_due(enabled, target_hour, last_day, now_epoch, timezone_offset_min);
}

}  // namespace omnisms
