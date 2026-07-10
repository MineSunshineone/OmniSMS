// 转发规则引擎(自 sms_forwarding idf_push/idf_config 抽取)
// 规则格式：每行 "type\tpattern\taction[\tenabled]"
//   type: kw(正文包含) / from(发送者正则) / re(正文正则)
//   action: 逗号分隔的 drop / email / 通道序号(1..MAX_PUSH_CHANNELS)
// 正则为 POSIX ERE + Perl 风格 \d \w \s 翻译，保存校验与运行时匹配共用同一翻译。
#pragma once

#include <cstdint>
#include <string>

namespace omnisms {

constexpr int MAX_PUSH_CHANNELS = 5;

struct ForwardDecision {
    bool matched = false;
    bool drop = false;
    uint32_t chMask = 0;  // bit i = 通道 i+1
    bool email = false;
};

// Perl 风格 \d \w \s(及取反)转 POSIX 字符类
std::string translate_perl_classes(const std::string& pattern);

// 首条命中规则即返回；无命中时 matched=false(调用方按"全部通道+邮件"处理)
ForwardDecision eval_forward_rules(const std::string& rules, const std::string& sender,
                                   const std::string& body);

// 保存前校验所有 from/re 规则可编译；失败时 message 给出行号与原因
// 返回 true=全部有效
bool validate_forward_rules(const std::string& rules, std::string* message);

}  // namespace omnisms
