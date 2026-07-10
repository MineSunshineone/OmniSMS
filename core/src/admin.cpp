#include "omnisms/admin.h"

#include "omnisms/phone.h"
#include "omnisms/text.h"

namespace omnisms {

bool admin_sender_matches(const std::string& admin_phone, const std::string& sender)
{
    if (admin_phone.empty()) return false;
    const std::string admin = canonical_phone(admin_phone);
    return !admin.empty() && canonical_phone(sender) == admin;
}

AdminCommandDecision evaluate_admin_command(const std::string& admin_phone,
                                             const std::string& sender,
                                             const std::string& text,
                                             int64_t uptime_seconds,
                                             bool sms_busy,
                                             bool reset_pending)
{
    AdminCommandDecision decision;
    if (!admin_sender_matches(admin_phone, sender)) return decision;

    decision.command = trim(text);
    const bool sms_command = decision.command.rfind("SMS:", 0) == 0;
    const bool reset_command = decision.command == "RESET";
    if (!sms_command && !reset_command) return decision;

    if (sms_command) {
        const size_t first = decision.command.find(':');
        const size_t second = first == std::string::npos ? std::string::npos :
                              decision.command.find(':', first + 1);
        if (second == std::string::npos || second <= first + 1) {
            decision.status = AdminCommandStatus::SmsFormatError;
            return decision;
        }
        decision.target = trim(decision.command.substr(first + 1, second - first - 1));
        decision.content = trim(decision.command.substr(second + 1));
        if (!is_valid_phone_number(decision.target)) {
            decision.status = AdminCommandStatus::InvalidTarget;
        } else if (decision.content.empty() || decision.content.size() > 300) {
            decision.status = AdminCommandStatus::InvalidContent;
        } else if (sms_busy) {
            decision.status = AdminCommandStatus::SmsBusy;
        } else {
            decision.status = AdminCommandStatus::SendSms;
        }
        return decision;
    }

    if (uptime_seconds < 60) {
        decision.status = AdminCommandStatus::ResetTooSoon;
    } else if (reset_pending) {
        decision.status = AdminCommandStatus::ResetPending;
    } else {
        decision.status = AdminCommandStatus::Reset;
    }
    return decision;
}

const char* admin_command_message(AdminCommandStatus status)
{
    switch (status) {
        case AdminCommandStatus::SendSms: return "发送短信";
        case AdminCommandStatus::Reset: return "执行重启";
        case AdminCommandStatus::SmsFormatError: return "SMS命令格式错误，正确格式: SMS:号码:内容";
        case AdminCommandStatus::InvalidTarget: return "SMS命令目标号码非法（应为 3-20 位数字，可带 + 前缀）";
        case AdminCommandStatus::InvalidContent: return "SMS命令内容为空或超过 300 字符";
        case AdminCommandStatus::SmsBusy: return "已有管理员短信任务在执行，请稍后重试";
        case AdminCommandStatus::ResetTooSoon: return "设备启动不足60秒，已忽略RESET命令以防重启风暴。请稍后重试。";
        case AdminCommandStatus::ResetPending: return "RESET任务已在执行，已忽略重复命令。";
        default: return "";
    }
}

}  // namespace omnisms
