// 号码规范化与黑名单匹配(自 sms_forwarding idf_sms 抽取)
#pragma once

#include <string>

namespace omnisms {

// 去空白/分隔符，仅留数字；+86/裸 86(13 位)前缀剥除，用于号码等值比较
std::string canonical_phone(const std::string& num);

// list 为换行分隔的号码清单；按 canonical_phone 归一后精确匹配
bool number_blacklisted(const std::string& list, const std::string& sender);

// 可拨号码的宽校验：3-20 位数字，可带 + 前缀
bool is_valid_phone_number(const std::string& phone);

}  // namespace omnisms
