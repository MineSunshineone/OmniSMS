// 文本工具：转义/编码/占位符展开(自 sms_forwarding idf_push 抽取)
#pragma once

#include <string>

namespace omnisms {

std::string trim(std::string value);

// 在 UTF-8 字符边界截断，超长时追加 "..."
std::string utf8_truncate(const std::string& value, size_t max_bytes);

void json_escape_append(std::string& out, const std::string& value);
std::string json_escape(const std::string& value);
// out += "key":"<escaped value>"
void json_prop(std::string& out, const char* key, const std::string& value);

// 短信内容不可信，进入 HTML 模板的通道必须先转义标签字符
std::string html_escape(const std::string& value);

std::string url_encode(const std::string& value);
std::string url_decode_component(const std::string& value);

// 单次扫描替换 {sender} {message} {timestamp} {receiver} {local_number}，
// 避免正文里出现的占位符字面量被二次展开
std::string apply_push_placeholders(const std::string& value, const std::string& sender,
                                    const std::string& text, const std::string& timestamp,
                                    const std::string& receiver);

}  // namespace omnisms
