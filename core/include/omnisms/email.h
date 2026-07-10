#pragma once

#include <string>

#include "omnisms/config.h"
#include "omnisms/push.h"

namespace omnisms {

struct EmailContent {
    std::string subject;
    std::string body;
};

// Transport-independent email selection and content semantics shared by all ports.
bool email_configured(const EmailConfig& config);
EmailContent build_sms_email(const SmsEvent& event);
EmailContent build_notification_email(const std::string& title, const std::string& body);

}  // namespace omnisms
