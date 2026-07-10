#include "omnisms/linux/modem_driver.h"

#include <cctype>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace omnisms::linux_port {
namespace {

std::string trim_line(const std::string& value)
{
    size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
    size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) --last;
    return value.substr(first, last - first);
}

bool set_error(std::string* error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

bool is_hex_pdu(const std::string& value)
{
    if (value.empty() || (value.size() & 1U) != 0 || value.size() > 4096) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isxdigit(ch) != 0;
    });
}

bool extract_cusd(const std::string& raw, std::string& response)
{
    const size_t start = raw.find("+CUSD:");
    if (start == std::string::npos) return false;
    const size_t quote1 = raw.find('"', start);
    if (quote1 != std::string::npos) {
        const size_t quote2 = raw.find('"', quote1 + 1);
        if (quote2 != std::string::npos) {
            response = raw.substr(quote1 + 1, quote2 - quote1 - 1);
            return true;
        }
    }
    const size_t end = raw.find_first_of("\r\n", start);
    response = trim_line(raw.substr(start, end == std::string::npos ? std::string::npos : end - start));
    return true;
}

class Generic3gppDriver final : public ModemDriver {
public:
    const char* id() const override { return "generic-3gpp"; }

    bool initialize(AtChannel& channel, std::string* error) override
    {
        if (!channel.ok("AT") || !channel.ok("ATE0") || !channel.ok("AT+CMGF=0")) {
            return set_error(error, "模组初始化失败（AT/ATE0/CMGF）");
        }
        channel.ok("AT+CNMI=2,1");
        channel.ok("AT+CLIP=1");
        return true;
    }

    bool poll(AtChannel& channel, ModemPollResult& out, std::string* error) override
    {
        out = {};
        const std::string response = channel.command("AT+CMGL=4", 8000);
        if (response.empty()) return set_error(error, "AT+CMGL=4 无响应");
        if (response.find("\r\nOK\r\n") == std::string::npos &&
            response.find("+CMGL:") == std::string::npos) {
            return set_error(error, "AT+CMGL=4 返回错误");
        }

        // 先前不完整的 +CMT 窗口由存储轮询兜底，避免误吞 CMGL 的首条 PDU。
        waiting_direct_pdu_ = false;
        std::istringstream lines(response);
        std::string line;
        int pending_index = -1;
        while (std::getline(lines, line)) {
            line = trim_line(line);
            if (line == "RING" || line.rfind("+CLIP:", 0) == 0 ||
                line.rfind("+CMTI:", 0) == 0) {
                out.urcLines.push_back(line);
                continue;
            }
            if (line.rfind("+CMT:", 0) == 0) {
                waiting_direct_pdu_ = true;
                out.urcLines.push_back(line);
                continue;
            }
            if (waiting_direct_pdu_ && is_hex_pdu(line)) {
                out.messages.push_back({-1, line});
                waiting_direct_pdu_ = false;
                continue;
            }
            if (line.rfind("+CMGL:", 0) == 0) {
                pending_index = std::atoi(line.c_str() + 6);
                continue;
            }
            if (pending_index >= 0 && !line.empty()) {
                if (line != "OK" && line != "ERROR" && line.rfind("AT+", 0) != 0) {
                    out.messages.push_back({pending_index, line});
                }
                pending_index = -1;
            }
        }
        return true;
    }

    bool delete_sms(AtChannel& channel, int index) override
    {
        char command[24];
        std::snprintf(command, sizeof(command), "AT+CMGD=%d", index);
        return channel.ok(command);
    }

    bool send_pdu(AtChannel& channel, int tpdu_length, const std::string& payload,
                  std::string* response) override
    {
        const std::string command = "AT+CMGS=" + std::to_string(tpdu_length);
        const std::string result = channel.command_with_payload(command, payload, ">", 20000);
        if (response) *response = result;
        return result.find("+CMGS:") != std::string::npos &&
               result.find("\r\nOK\r\n") != std::string::npos;
    }

    void consume_urc(const std::string& chunk, ModemPollResult& out) override
    {
        if (chunk.empty()) return;
        urc_carry_ += chunk;
        if (urc_carry_.size() > 8192) urc_carry_.erase(0, urc_carry_.size() - 4096);
        size_t newline = 0;
        while ((newline = urc_carry_.find('\n')) != std::string::npos) {
            std::string line = trim_line(urc_carry_.substr(0, newline));
            urc_carry_.erase(0, newline + 1);
            if (line.empty()) continue;
            if (waiting_direct_pdu_ && is_hex_pdu(line)) {
                out.messages.push_back({-1, line});
                waiting_direct_pdu_ = false;
            } else if (line.rfind("+CMT:", 0) == 0) {
                waiting_direct_pdu_ = true;
                out.urcLines.push_back(line);
            } else if (line == "RING" || line.rfind("+CLIP:", 0) == 0 ||
                       line.rfind("+CMTI:", 0) == 0) {
                out.urcLines.push_back(line);
            }
        }
    }

    bool query_sim_state(AtChannel& channel, SimState& state, std::string* error) override
    {
        const std::string result = channel.command("AT+CPIN?", 2000);
        if (result.empty()) return set_error(error, "AT+CPIN? 无响应");
        if (result.find("READY") != std::string::npos) state = SimState::Ready;
        else if (result.find("SIM PIN") != std::string::npos ||
                 result.find("SIM PUK") != std::string::npos) state = SimState::Locked;
        else if (result.find("NOT INSERTED") != std::string::npos ||
                 result.find("SIM FAILURE") != std::string::npos ||
                 result.find("+CME ERROR: 10") != std::string::npos) state = SimState::Missing;
        else state = SimState::Unknown;
        return true;
    }

    bool send_ussd(AtChannel& channel, const std::string& code,
                   std::string& response, std::string* error) override
    {
        response.clear();
        if (code.empty() || code.size() > 64 ||
            !std::all_of(code.begin(), code.end(), [](unsigned char ch) {
                return std::isdigit(ch) || ch == '*' || ch == '#';
            })) {
            return set_error(error, "USSD 代码无效");
        }
        std::string raw = channel.command("AT+CUSD=1,\"" + code + "\",15", 5000);
        if (extract_cusd(raw, response)) return true;
        for (int i = 0; i < 20; ++i) {
            raw += channel.read_available(500);
            if (extract_cusd(raw, response)) return true;
        }
        return set_error(error, "USSD 响应超时");
    }

private:
    std::string urc_carry_;
    bool waiting_direct_pdu_ = false;
};

const bool registered = register_modem_driver("generic-3gpp", [] {
    return std::unique_ptr<ModemDriver>(new Generic3gppDriver());
});

}  // namespace
}  // namespace omnisms::linux_port
