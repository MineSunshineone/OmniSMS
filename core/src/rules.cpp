#include "omnisms/rules.h"

#include <regex.h>

#include <cctype>
#include <cstring>

#include "omnisms/text.h"

namespace omnisms {

std::string translate_perl_classes(const std::string& pattern)
{
    std::string out;
    out.reserve(pattern.size() + 16);
    bool in_bracket = false;
    for (size_t i = 0; i < pattern.size(); ++i) {
        char ch = pattern[i];
        if (ch == '[' && !in_bracket) { in_bracket = true; out += ch; continue; }
        if (ch == ']' && in_bracket) { in_bracket = false; out += ch; continue; }
        if (ch != '\\' || i + 1 >= pattern.size()) { out += ch; continue; }
        char next = pattern[i + 1];
        const char* body = nullptr;   // 字符类内部内容
        const char* neg = nullptr;    // 取反形式(仅括号外可表达)
        switch (next) {
            case 'd': body = "0-9"; break;
            case 'D': neg = "0-9"; break;
            case 'w': body = "A-Za-z0-9_"; break;
            case 'W': neg = "A-Za-z0-9_"; break;
            case 's': body = " \t\r\n\f\v"; break;
            case 'S': neg = " \t\r\n\f\v"; break;
            default: out += ch; out += next; ++i; continue;
        }
        if (in_bracket) {
            // 括号内只能展开正类；取反形式无法表达，原样保留
            if (body) { out += body; ++i; }
            else { out += ch; out += next; ++i; }
        } else {
            out += '[';
            if (neg) { out += '^'; out += neg; }
            else out += body;
            out += ']';
            ++i;
        }
    }
    return out;
}

static bool regex_search_case_insensitive(const std::string& pattern, const std::string& text)
{
    std::string posix = translate_perl_classes(pattern);
    regex_t re = {};
    if (regcomp(&re, posix.c_str(), REG_EXTENDED | REG_ICASE | REG_NOSUB) != 0) return false;
    bool hit = regexec(&re, text.c_str(), 0, nullptr, 0) == 0;
    regfree(&re);
    return hit;
}

static bool parse_push_channel_token(const std::string& value, uint8_t& channel)
{
    if (value.empty()) return false;
    uint32_t parsed = 0;
    for (char ch : value) {
        if (!isdigit(static_cast<unsigned char>(ch))) return false;
        parsed = parsed * 10U + static_cast<uint32_t>(ch - '0');
        if (parsed > static_cast<uint32_t>(MAX_PUSH_CHANNELS)) return false;
    }
    if (parsed == 0) return false;
    channel = static_cast<uint8_t>(parsed);
    return true;
}

ForwardDecision eval_forward_rules(const std::string& rules, const std::string& sender,
                                   const std::string& body)
{
    ForwardDecision d;
    size_t pos = 0;
    while (pos < rules.size()) {
        size_t end = rules.find('\n', pos);
        if (end == std::string::npos) end = rules.size();
        std::string line = trim(rules.substr(pos, end - pos));
        pos = end + (end < rules.size() ? 1 : 0);
        if (line.empty()) continue;

        size_t t1 = line.find('\t');
        size_t t2 = t1 == std::string::npos ? std::string::npos : line.find('\t', t1 + 1);
        if (t1 == std::string::npos || t2 == std::string::npos) continue;
        size_t t3 = line.find('\t', t2 + 1);
        std::string type = line.substr(0, t1);
        std::string pat = line.substr(t1 + 1, t2 - t1 - 1);
        std::string action = t3 == std::string::npos ? line.substr(t2 + 1) : line.substr(t2 + 1, t3 - t2 - 1);
        std::string enabled = t3 == std::string::npos ? "1" : trim(line.substr(t3 + 1));
        if (enabled == "0" || pat.empty()) continue;

        bool hit = false;
        if (type == "kw") hit = body.find(pat) != std::string::npos;
        else if (type == "from") hit = regex_search_case_insensitive(pat, sender);
        else if (type == "re") hit = regex_search_case_insensitive(pat, body);
        if (!hit) continue;

        d.matched = true;
        size_t ap = 0;
        while (ap <= action.size()) {
            size_t comma = action.find(',', ap);
            if (comma == std::string::npos) comma = action.size();
            std::string tok = trim(action.substr(ap, comma - ap));
            if (tok == "drop") d.drop = true;
            else if (tok == "email") d.email = true;
            else {
                uint8_t ch = 0;
                if (parse_push_channel_token(tok, ch)) d.chMask |= 1u << (ch - 1);
            }
            if (comma == action.size()) break;
            ap = comma + 1;
        }
        return d;
    }
    return d;
}

bool validate_forward_rules(const std::string& rules, std::string* message)
{
    size_t pos = 0;
    int line_no = 0;
    while (pos < rules.size()) {
        size_t end = rules.find('\n', pos);
        if (end == std::string::npos) end = rules.size();
        std::string line = trim(rules.substr(pos, end - pos));
        pos = end + (end < rules.size() ? 1 : 0);
        ++line_no;
        if (line.empty()) continue;

        size_t t1 = line.find('\t');
        size_t t2 = t1 == std::string::npos ? std::string::npos : line.find('\t', t1 + 1);
        if (t1 == std::string::npos || t2 == std::string::npos) continue;
        size_t t3 = line.find('\t', t2 + 1);
        std::string type = line.substr(0, t1);
        std::string pat = line.substr(t1 + 1, t2 - t1 - 1);
        std::string enabled = t3 == std::string::npos ? "1" : trim(line.substr(t3 + 1));
        if (enabled == "0" || pat.empty() || type == "kw") continue;
        if (type != "from" && type != "re") continue;

        std::string posix = translate_perl_classes(pat);
        regex_t re = {};
        int rc = regcomp(&re, posix.c_str(), REG_EXTENDED | REG_ICASE | REG_NOSUB);
        if (rc != 0) {
            if (message) {
                char errbuf[96] = {};
                regerror(rc, &re, errbuf, sizeof(errbuf));
                *message = "第 " + std::to_string(line_no) + " 行正则无效: " + errbuf;
            }
            return false;
        }
        regfree(&re);
    }
    if (message) message->clear();
    return true;
}

}  // namespace omnisms
