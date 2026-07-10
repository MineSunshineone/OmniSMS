#include "omnisms/text.h"

#include <cctype>
#include <cstdio>

namespace omnisms {

std::string trim(std::string value)
{
    size_t start = 0;
    while (start < value.size() && isspace(static_cast<unsigned char>(value[start]))) ++start;
    size_t end = value.size();
    while (end > start && isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return value.substr(start, end - start);
}

std::string utf8_truncate(const std::string& value, size_t max_bytes)
{
    if (value.size() <= max_bytes) return value;
    size_t end = max_bytes;
    while (end > 0 && (static_cast<unsigned char>(value[end]) & 0xC0) == 0x80) --end;
    return value.substr(0, end) + "...";
}

void json_escape_append(std::string& out, const std::string& value)
{
    for (char ch : value) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    char buf[7];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(ch));
                    out += buf;
                } else {
                    out += ch;
                }
        }
    }
}

std::string json_escape(const std::string& value)
{
    std::string out;
    out.reserve(value.size() + 8);
    json_escape_append(out, value);
    return out;
}

void json_prop(std::string& out, const char* key, const std::string& value)
{
    out += "\"";
    out += key;
    out += "\":\"";
    json_escape_append(out, value);
    out += "\"";
}

std::string html_escape(const std::string& value)
{
    std::string out;
    out.reserve(value.size() + 16);
    for (char ch : value) {
        switch (ch) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += ch; break;
        }
    }
    return out;
}

std::string url_encode(const std::string& value)
{
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size() * 3);
    for (unsigned char ch : value) {
        if (isalnum(ch)) out += static_cast<char>(ch);
        else if (ch == ' ') out += '+';
        else {
            out += '%';
            out += hex[ch >> 4];
            out += hex[ch & 0x0F];
        }
    }
    return out;
}

static int hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

std::string url_decode_component(const std::string& value)
{
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        char ch = value[i];
        if (ch == '+') {
            out += ' ';
        } else if (ch == '%' && i + 2 < value.size()) {
            int hi = hex_value(value[i + 1]);
            int lo = hex_value(value[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>((hi << 4) | lo);
                i += 2;
            } else {
                out += ch;
            }
        } else {
            out += ch;
        }
    }
    return out;
}

std::string apply_push_placeholders(const std::string& value, const std::string& sender,
                                    const std::string& text, const std::string& timestamp,
                                    const std::string& receiver)
{
    std::string out;
    out.reserve(value.size() + text.size() + receiver.size());
    size_t pos = 0;
    while (pos < value.size()) {
        size_t brace = value.find('{', pos);
        if (brace == std::string::npos) {
            out.append(value, pos, std::string::npos);
            break;
        }
        out.append(value, pos, brace - pos);
        if (value.compare(brace, 8, "{sender}") == 0) { out += sender; pos = brace + 8; }
        else if (value.compare(brace, 9, "{message}") == 0) { out += text; pos = brace + 9; }
        else if (value.compare(brace, 11, "{timestamp}") == 0) { out += timestamp; pos = brace + 11; }
        else if (value.compare(brace, 10, "{receiver}") == 0) { out += receiver; pos = brace + 10; }
        else if (value.compare(brace, 14, "{local_number}") == 0) { out += receiver; pos = brace + 14; }
        else { out += '{'; pos = brace + 1; }
    }
    return out;
}

}  // namespace omnisms
