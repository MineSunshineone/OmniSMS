#include "omnisms/phone.h"

#include <cctype>

#include "omnisms/text.h"

namespace omnisms {

std::string canonical_phone(const std::string& num)
{
    size_t start = 0;
    while (start < num.size() && isspace(static_cast<unsigned char>(num[start]))) ++start;
    bool explicit_cn_prefix = start < num.size() && num[start] == '+';

    std::string out;
    out.reserve(num.size());
    for (size_t i = 0; i < num.size(); ++i) {
        unsigned char ch = static_cast<unsigned char>(num[i]);
        if (isdigit(ch)) out += static_cast<char>(ch);
    }
    if (out.rfind("86", 0) == 0 && out.size() > 2 && (explicit_cn_prefix || out.size() == 13)) {
        return out.substr(2);
    }
    return out;
}

bool number_blacklisted(const std::string& list, const std::string& sender)
{
    if (list.empty()) return false;
    std::string target = canonical_phone(sender);
    if (target.empty()) return false;
    size_t pos = 0;
    while (pos <= list.size()) {
        size_t end = list.find('\n', pos);
        if (end == std::string::npos) end = list.size();
        std::string line = trim(list.substr(pos, end - pos));
        if (!line.empty() && canonical_phone(line) == target) {
            return true;
        }
        if (end == list.size()) break;
        pos = end + 1;
    }
    return false;
}

bool is_valid_phone_number(const std::string& phone)
{
    if (phone.size() < 3 || phone.size() > 20) return false;
    for (size_t i = 0; i < phone.size(); ++i) {
        char ch = phone[i];
        if (i == 0 && ch == '+') continue;
        if (!isdigit(static_cast<unsigned char>(ch))) return false;
    }
    return true;
}

}  // namespace omnisms
