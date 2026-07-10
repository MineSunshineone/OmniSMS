#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "omnisms/linux/modem_driver.h"

namespace omnisms::linux_port {

struct ModemCapabilities {
    bool pduSms = false;
    bool textSms = false;
    bool incomingCall = false;
    bool ussd = false;
    bool simHotplug = false;
    bool rawAt = false;
    bool reset = false;
};

struct ModemMessage {
    std::string id;
    bool pduEncoded = false;
    std::string pdu;
    std::string sender;
    std::string text;
    std::string timestamp;
};

struct ModemEventBatch {
    std::vector<ModemMessage> messages;
    std::vector<std::string> urcLines;
};

class ModemBackend {
public:
    virtual ~ModemBackend() = default;
    virtual const char* id() const = 0;
    virtual std::string endpoint() const = 0;
    virtual ModemCapabilities capabilities() const = 0;
    virtual bool initialize(std::string* error = nullptr) = 0;
    virtual bool collect_events(ModemEventBatch& out, bool poll_stored,
                                int wait_ms, std::string* error = nullptr) = 0;
    virtual bool acknowledge_sms(const std::string& id, std::string* error = nullptr) = 0;
    virtual bool send_sms(const std::string& phone, const std::string& text,
                          std::string* error = nullptr) = 0;
    virtual bool send_ussd(const std::string& code, std::string& response,
                           std::string* error = nullptr) = 0;
    virtual bool query_sim_state(SimState& state, std::string* error = nullptr) = 0;
    virtual bool raw_at(const std::string& command, std::string& response,
                        std::string* error = nullptr) = 0;
    virtual bool reset(std::string* error = nullptr) = 0;
};

struct CommandResult {
    int exitCode = -1;
    std::string output;
};

using CommandRunner = std::function<CommandResult(const std::vector<std::string>&, int)>;

// driver="modemmanager" selects the mmcli/ModemManager D-Bus backend. Other values
// continue to select a self-registered AT driver and open endpoint as a serial port.
std::unique_ptr<ModemBackend> create_modem_backend(const std::string& driver,
                                                   const std::string& endpoint,
                                                   int baud,
                                                   std::string* error = nullptr);

// Exposed for deterministic tests and device-selection tooling. An empty or "auto"
// selector is accepted only when ModemManager reports exactly one modem.
std::unique_ptr<ModemBackend> create_modemmanager_backend(
    const std::string& selector, CommandRunner runner = {});
std::vector<std::string> list_modemmanager_modems(CommandRunner runner = {},
                                                  std::string* error = nullptr);

}  // namespace omnisms::linux_port
