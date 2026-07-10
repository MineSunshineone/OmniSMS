#include "omnisms/email.h"

#include "omnisms/text.h"

namespace omnisms {

bool email_configured(const EmailConfig& config)
{
    return config.enabled && !trim(config.server).empty() &&
           !trim(config.username).empty() && !trim(config.password).empty();
}

EmailContent build_sms_email(const SmsEvent& event)
{
    EmailContent out;
    out.subject = "短信" + event.sender + "," + utf8_truncate(event.text, 48);
    out.body = "来自：" + event.sender;
    if (!event.receiver.empty()) out.body += "，本机号码：" + event.receiver;
    if (!event.timestamp.empty()) out.body += "，时间：" + event.timestamp;
    out.body += "，内容：" + event.text;
    return out;
}

EmailContent build_notification_email(const std::string& title, const std::string& body)
{
    return EmailContent{title, body};
}

}  // namespace omnisms
