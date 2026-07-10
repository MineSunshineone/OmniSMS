#pragma once

#include <cstdint>

namespace omnisms {

constexpr uint32_t VALID_EPOCH_MIN = 1700000000u;
constexpr int64_t DAILY_REBOOT_MIN_UPTIME_SECONDS = 2 * 60 * 60;

bool epoch_valid(uint32_t epoch);
bool interval_due(uint32_t last_epoch, uint32_t now_epoch, uint32_t interval_days);

enum class PeriodicState : uint8_t {
    Disabled = 0,
    InvalidClock,
    EstablishBaseline,
    NotDue,
    Due,
};

// 首次启用/无 lastRun 时只建立基准，不立即执行；与 ESP 保号和六个定时任务一致。
PeriodicState evaluate_periodic(bool enabled, uint32_t last_epoch, uint32_t now_epoch,
                                int interval_days);

// 周期任务失败后把 lastRun 调整为“下一天再次到期”，避免每小时重复执行昂贵动作。
uint32_t failure_retry_baseline(uint32_t now_epoch, int interval_days);

struct LocalDayHour {
    bool valid = false;
    int64_t day = 0;  // 指定时区下自 Unix epoch 起的本地日编号
    int hour = 0;
};

LocalDayHour local_day_hour(uint32_t now_epoch, int timezone_offset_min);

// day 必须严格大于 last_day；NTP 回拨跨午夜不会在同一自然日重复触发。
bool daily_due(bool enabled, int target_hour, int64_t last_day,
               uint32_t now_epoch, int timezone_offset_min);

bool daily_reboot_due(bool enabled, int target_hour, int64_t last_day,
                      uint32_t now_epoch, int timezone_offset_min,
                      int64_t uptime_seconds, bool system_idle);

}  // namespace omnisms
