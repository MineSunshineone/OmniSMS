#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "omnisms/linux/at_channel.h"

namespace omnisms::linux_port {

struct StoredSms {
    int index = -1;
    std::string pdu;
};

struct ModemPollResult {
    std::vector<StoredSms> messages;
    std::vector<std::string> urcLines;
};

enum class SimState {
    Unknown,
    Missing,
    Locked,
    Ready,
};

class ModemDriver {
public:
    virtual ~ModemDriver() = default;
    virtual const char* id() const = 0;
    virtual bool initialize(AtChannel& channel, std::string* error = nullptr) = 0;
    virtual bool poll(AtChannel& channel, ModemPollResult& out,
                      std::string* error = nullptr) = 0;
    virtual bool delete_sms(AtChannel& channel, int index) = 0;
    virtual bool send_pdu(AtChannel& channel, int tpdu_length, const std::string& payload,
                          std::string* response = nullptr) = 0;
    virtual void consume_urc(const std::string& chunk, ModemPollResult& out) = 0;
    virtual bool query_sim_state(AtChannel& channel, SimState& state,
                                 std::string* error = nullptr) = 0;
    virtual bool send_ussd(AtChannel& channel, const std::string& code,
                           std::string& response, std::string* error = nullptr) = 0;
};

using ModemDriverFactory = std::function<std::unique_ptr<ModemDriver>()>;

// driver 文件通过静态注册加入工厂；CMake 自动收集 src/drivers/*.cpp。
// 因此增加模组型号不需要修改 core、main 或构建清单。
bool register_modem_driver(const std::string& id, ModemDriverFactory factory);
std::unique_ptr<ModemDriver> create_modem_driver(const std::string& id);
std::vector<std::string> registered_modem_drivers();

}  // namespace omnisms::linux_port
