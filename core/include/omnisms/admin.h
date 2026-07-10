#pragma once

#include <cstdint>
#include <string>

namespace omnisms {

// 原 ESP 固件支持的管理员短信命令。解析与策略属于业务 core，实际发短信/重启由平台执行。
enum class AdminCommandStatus : uint8_t {
    Ignored = 0,
    SendSms,
    Reset,
    SmsFormatError,
    InvalidTarget,
    InvalidContent,
    SmsBusy,
    ResetTooSoon,
    ResetPending,
};

struct AdminCommandDecision {
    AdminCommandStatus status = AdminCommandStatus::Ignored;
    std::string command;
    std::string target;
    std::string content;

    bool handled() const { return status != AdminCommandStatus::Ignored; }
};

bool admin_sender_matches(const std::string& admin_phone, const std::string& sender);

// 保持基准行为：命令区分大小写；SMS:号码:内容；内容按 UTF-8 字节最多 300；
// RESET 在启动 60 秒内拒绝。busy/pending 由平台当前状态注入，core 不持有线程状态。
AdminCommandDecision evaluate_admin_command(const std::string& admin_phone,
                                             const std::string& sender,
                                             const std::string& text,
                                             int64_t uptime_seconds,
                                             bool sms_busy,
                                             bool reset_pending);

const char* admin_command_message(AdminCommandStatus status);

}  // namespace omnisms
