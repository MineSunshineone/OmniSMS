#include "omnisms/linux/modem_backend.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <map>
#include <regex>
#include <sstream>
#include <thread>

#include "omnisms/pdu.h"
#include "omnisms/phone.h"
#include "omnisms/text.h"

namespace omnisms::linux_port {
namespace {

using namespace std::chrono;

void set_error(std::string* error, const std::string& value)
{
    if (error) *error = value;
}

CommandResult run_command(const std::vector<std::string>& args, int timeout_ms)
{
    CommandResult result;
    if (args.empty()) {
        result.output = "empty command";
        return result;
    }
    int pipe_fds[2] = {-1, -1};
    if (pipe(pipe_fds) != 0) {
        result.output = std::strerror(errno);
        return result;
    }
    const pid_t pid = fork();
    if (pid < 0) {
        result.output = std::strerror(errno);
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return result;
    }
    if (pid == 0) {
        close(pipe_fds[0]);
        dup2(pipe_fds[1], STDOUT_FILENO);
        dup2(pipe_fds[1], STDERR_FILENO);
        close(pipe_fds[1]);
        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const std::string& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        const int exec_error = errno;
        dprintf(STDERR_FILENO, "execvp %s failed: %s\n",
                args[0].c_str(), std::strerror(exec_error));
        _exit(127);
    }

    close(pipe_fds[1]);
    const int flags = fcntl(pipe_fds[0], F_GETFL, 0);
    if (flags >= 0) fcntl(pipe_fds[0], F_SETFL, flags | O_NONBLOCK);
    const auto deadline = steady_clock::now() + milliseconds(std::max(100, timeout_ms));
    int status = 0;
    bool exited = false;
    bool wait_failed = false;
    char buffer[4096];
    while (!exited) {
        pollfd fd{pipe_fds[0], POLLIN | POLLHUP, 0};
        poll(&fd, 1, 50);
        for (;;) {
            const ssize_t count = read(pipe_fds[0], buffer, sizeof(buffer));
            if (count > 0) result.output.append(buffer, static_cast<size_t>(count));
            else break;
        }
        const pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) exited = true;
        else if (waited < 0) {
            exited = true;
            wait_failed = true;
            result.exitCode = -1;
            result.output += "\nwaitpid failed: " + std::string(std::strerror(errno));
        } else if (steady_clock::now() >= deadline) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            result.exitCode = 124;
            result.output += "\ncommand timed out";
            exited = true;
        }
    }
    for (;;) {
        const ssize_t count = read(pipe_fds[0], buffer, sizeof(buffer));
        if (count > 0) result.output.append(buffer, static_cast<size_t>(count));
        else break;
    }
    close(pipe_fds[0]);
    if (result.exitCode != 124 && !wait_failed) {
        result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128;
    }
    return result;
}

CommandRunner effective_runner(CommandRunner runner)
{
    return runner ? std::move(runner) : CommandRunner(run_command);
}

std::map<std::string, std::string> parse_key_values(const std::string& output)
{
    std::map<std::string, std::string> values;
    std::istringstream input(output);
    std::string line;
    while (std::getline(input, line)) {
        const size_t split = line.find(':');
        if (split == std::string::npos) continue;
        std::string key = omnisms::trim(line.substr(0, split));
        std::string value = omnisms::trim(line.substr(split + 1));
        if (value.size() >= 2 && value.front() == '\'' && value.back() == '\'') {
            value = value.substr(1, value.size() - 2);
        }
        values[std::move(key)] = std::move(value);
    }
    return values;
}

std::vector<std::string> prefixed_values(const std::map<std::string, std::string>& values,
                                         const std::string& prefix)
{
    std::vector<std::string> out;
    for (const auto& [key, value] : values) {
        if (key.rfind(prefix, 0) == 0 && !value.empty() && value != "--") out.push_back(value);
    }
    return out;
}

std::vector<std::string> object_paths(const std::map<std::string, std::string>& values,
                                      const std::string& prefix,
                                      const std::string& object_type)
{
    const std::regex pattern("(/org/freedesktop/ModemManager1/" + object_type + "/[0-9]+)");
    std::vector<std::string> out;
    for (const std::string& value : prefixed_values(values, prefix)) {
        std::smatch match;
        if (std::regex_search(value, match, pattern)) out.push_back(match[1].str());
    }
    return out;
}

std::string first_value(const std::map<std::string, std::string>& values,
                        std::initializer_list<const char*> keys)
{
    for (const char* key : keys) {
        const auto it = values.find(key);
        if (it != values.end() && it->second != "--") return it->second;
    }
    return {};
}

bool output_ok(const CommandResult& result, std::string* error, const std::string& action)
{
    if (result.exitCode == 0) return true;
    set_error(error, action + " failed (exit " + std::to_string(result.exitCode) + "): " +
                         omnisms::trim(result.output));
    return false;
}

std::string quote_mmcli_value(const std::string& value)
{
    std::string out = "'";
    for (char c : value) {
        if (c == '\\' || c == '\'') out += '\\';
        out += c;
    }
    out += '\'';
    return out;
}

class AtModemBackend final : public ModemBackend {
public:
    AtModemBackend(std::string driver_id, std::string endpoint, int baud,
                   std::unique_ptr<ModemDriver> driver)
        : driverId_(std::move(driver_id)), endpoint_(std::move(endpoint)), baud_(baud),
          driver_(std::move(driver)) {}

    const char* id() const override { return driverId_.c_str(); }
    std::string endpoint() const override { return endpoint_; }
    ModemCapabilities capabilities() const override
    {
        return {true, false, true, true, true, true, true};
    }

    bool initialize(std::string* error) override
    {
        if (error) error->clear();
        if (!opened_) {
            if (!channel_.open_port(endpoint_, baud_, error)) return false;
            opened_ = true;
        }
        return driver_->initialize(channel_, error);
    }

    bool collect_events(ModemEventBatch& out, bool poll_stored, int wait_ms,
                        std::string* error) override
    {
        ModemPollResult result;
        const std::string chunk = channel_.read_available(wait_ms);
        if (!chunk.empty()) driver_->consume_urc(chunk, result);
        bool cmti = false;
        for (const std::string& line : result.urcLines) {
            if (line.rfind("+CMTI:", 0) == 0) cmti = true;
        }
        if (poll_stored || cmti) {
            ModemPollResult stored;
            if (!driver_->poll(channel_, stored, error)) return false;
            result.messages.insert(result.messages.end(), stored.messages.begin(), stored.messages.end());
            result.urcLines.insert(result.urcLines.end(), stored.urcLines.begin(), stored.urcLines.end());
        }
        out.urcLines.insert(out.urcLines.end(), result.urcLines.begin(), result.urcLines.end());
        for (const StoredSms& sms : result.messages) {
            ModemMessage message;
            message.id = std::to_string(sms.index);
            message.pduEncoded = true;
            message.pdu = sms.pdu;
            out.messages.push_back(std::move(message));
        }
        return true;
    }

    bool acknowledge_sms(const std::string& id, std::string* error) override
    {
        try {
            return driver_->delete_sms(channel_, std::stoi(id));
        } catch (...) {
            set_error(error, "invalid AT SMS index: " + id);
            return false;
        }
    }

    bool send_sms(const std::string& phone, const std::string& text, std::string* error) override
    {
        const int64_t now_ms = duration_cast<milliseconds>(
            system_clock::now().time_since_epoch()).count();
        uint16_t reference = static_cast<uint16_t>(now_ms & 0xFFFF);
        if (reference == 0) reference = 1;
        omnisms::PduEncoder encoder;
        std::vector<omnisms::EncodedPdu> parts;
        if (!encoder.encode_text(phone, text, reference, parts, error)) return false;
        for (size_t i = 0; i < parts.size(); ++i) {
            std::string response;
            if (!driver_->send_pdu(channel_, parts[i].tpduLength, parts[i].payload, &response)) {
                set_error(error, "SMS part " + std::to_string(i + 1) + "/" +
                                     std::to_string(parts.size()) + " failed: " +
                                     omnisms::trim(response));
                return false;
            }
            if (i + 1 < parts.size()) std::this_thread::sleep_for(milliseconds(300));
        }
        return true;
    }

    bool send_ussd(const std::string& code, std::string& response, std::string* error) override
    {
        return driver_->send_ussd(channel_, code, response, error);
    }

    bool query_sim_state(SimState& state, std::string* error) override
    {
        return driver_->query_sim_state(channel_, state, error);
    }

    bool raw_at(const std::string& command, std::string& response, std::string* error) override
    {
        response = channel_.command(command, 10000);
        if (!response.empty()) return true;
        set_error(error, "AT command returned no response");
        return false;
    }

    bool reset(std::string* error) override
    {
        const std::string response = channel_.command("AT+CFUN=1,1", 10000);
        if (!response.empty()) return true;
        set_error(error, "modem reset returned no response");
        return false;
    }

private:
    std::string driverId_;
    std::string endpoint_;
    int baud_ = 115200;
    bool opened_ = false;
    std::unique_ptr<ModemDriver> driver_;
    SerialAtChannel channel_;
};

class ModemManagerBackend final : public ModemBackend {
public:
    ModemManagerBackend(std::string selector, CommandRunner runner)
        : configuredSelector_(std::move(selector)), runner_(effective_runner(std::move(runner))) {}

    const char* id() const override { return "modemmanager"; }
    std::string endpoint() const override { return selector_; }
    ModemCapabilities capabilities() const override
    {
        return capabilities_;
    }

    bool initialize(std::string* error) override
    {
        if (error) error->clear();
        capabilities_ = {false, false, false, false, true, false, true};
        selector_ = configuredSelector_;
        if (selector_.empty() || selector_ == "auto") {
            const std::vector<std::string> modems = list_modemmanager_modems(runner_, error);
            if (modems.empty()) {
                if (error && error->empty()) *error = "ModemManager reported no modems";
                return false;
            }
            if (modems.size() != 1) {
                set_error(error, "multiple ModemManager devices found; set modem.endpoint to an "
                                 "object path or numeric modem id");
                return false;
            }
            selector_ = modems.front();
        }
        const CommandResult status = run({"mmcli", "-m", selector_, "-K"}, 10000);
        if (!output_ok(status, error, "ModemManager modem lookup")) return false;

        const CommandResult messaging = run(
            {"mmcli", "-m", selector_, "--messaging-status", "-K"}, 10000);
        if (!output_ok(messaging, error, "ModemManager messaging capability")) return false;
        capabilities_.textSms = true;

        capabilities_.ussd = run(
            {"mmcli", "-m", selector_, "--3gpp-ussd-status", "-K"}, 10000).exitCode == 0;
        capabilities_.incomingCall = run(
            {"mmcli", "-m", selector_, "--voice-status", "-K"}, 10000).exitCode == 0;
        return true;
    }

    bool collect_events(ModemEventBatch& out, bool poll_stored, int wait_ms,
                        std::string* error) override
    {
        if (error) error->clear();
        (void)wait_ms;
        if (!poll_stored) return true;
        if (!capabilities_.textSms) {
            set_error(error, "ModemManager messaging backend is not initialized");
            return false;
        }
        const CommandResult listed = run(
            {"mmcli", "-m", selector_, "--messaging-list-sms", "-K"}, 15000);
        if (!output_ok(listed, error, "ModemManager SMS list")) return false;
        const auto values = parse_key_values(listed.output);
        const std::vector<std::string> paths = object_paths(
            values, "modem.messaging.sms.value", "SMS");
        for (const std::string& path : paths) {
            const CommandResult inspected = run({"mmcli", "-s", path, "-K"}, 10000);
            if (inspected.exitCode != 0) continue;
            const auto sms = parse_key_values(inspected.output);
            const std::string state = first_value(sms, {"sms.properties.state", "sms.general.state"});
            if (!state.empty() && state != "received" && state != "receiving") continue;
            ModemMessage message;
            message.id = path;
            message.sender = first_value(sms, {"sms.content.number", "sms.general.number"});
            message.text = first_value(sms, {"sms.content.text", "sms.general.text"});
            message.timestamp = first_value(sms, {"sms.properties.timestamp",
                                                  "sms.content.timestamp",
                                                  "sms.general.timestamp"});
            if (!message.sender.empty() || !message.text.empty()) out.messages.push_back(std::move(message));
        }

        if (capabilities_.incomingCall) collect_calls(out);
        return true;
    }

    bool acknowledge_sms(const std::string& id, std::string* error) override
    {
        const CommandResult result = run(
            {"mmcli", "-m", selector_, "--messaging-delete-sms=" + id}, 10000);
        return output_ok(result, error, "ModemManager SMS delete");
    }

    bool send_sms(const std::string& phone, const std::string& text, std::string* error) override
    {
        if (error) error->clear();
        if (!capabilities_.textSms) {
            set_error(error, "ModemManager messaging backend is not initialized");
            return false;
        }
        if (!omnisms::is_valid_phone_number(phone)) {
            set_error(error, "invalid destination phone number");
            return false;
        }
        if (omnisms::trim(text).empty()) {
            set_error(error, "SMS text is empty");
            return false;
        }
        const CommandResult created = run(
            {"mmcli", "-m", selector_,
             "--messaging-create-sms=text=" + quote_mmcli_value(text) +
                 ",number=" + quote_mmcli_value(phone)},
            15000);
        if (!output_ok(created, error, "ModemManager SMS create")) return false;
        static const std::regex path_pattern("(/org/freedesktop/ModemManager1/SMS/[0-9]+)");
        std::smatch match;
        if (!std::regex_search(created.output, match, path_pattern)) {
            set_error(error, "ModemManager did not return the created SMS object path: " +
                             omnisms::trim(created.output));
            return false;
        }
        const std::string path = match[1].str();
        const CommandResult sent = run({"mmcli", "-s", path, "--send"}, 30000);
        if (!output_ok(sent, error, "ModemManager SMS send")) return false;
        // Sent objects are transient application state. Failure to clean them up is logged by
        // ModemManager but must not turn a successful network submission into a false failure.
        run({"mmcli", "-m", selector_, "--messaging-delete-sms=" + path}, 10000);
        return true;
    }

    bool send_ussd(const std::string& code, std::string& response, std::string* error) override
    {
        if (error) error->clear();
        if (!capabilities_.ussd) {
            set_error(error, "USSD is unsupported by the selected ModemManager device");
            return false;
        }
        if (code.empty() || code.size() > 64 || code.find_first_of("\r\n") != std::string::npos) {
            set_error(error, "invalid USSD code");
            return false;
        }
        const CommandResult result = run(
            {"mmcli", "-m", selector_, "--3gpp-ussd-initiate=" + code, "-K"}, 30000);
        if (!output_ok(result, error, "ModemManager USSD")) return false;
        const auto values = parse_key_values(result.output);
        response = first_value(values, {"modem.3gpp.ussd.network-request",
                                        "modem.3gpp.ussd.network-notification",
                                        "modem.3gpp-ussd.network-request",
                                        "modem.3gpp-ussd.network-notification"});
        if (response.empty()) response = omnisms::trim(result.output);
        return true;
    }

    bool query_sim_state(SimState& state, std::string* error) override
    {
        if (error) error->clear();
        const CommandResult result = run({"mmcli", "-m", selector_, "-K"}, 10000);
        if (!output_ok(result, error, "ModemManager SIM state")) return false;
        const auto values = parse_key_values(result.output);
        const std::string sim = first_value(values, {"modem.generic.sim"});
        const std::string unlock = first_value(values, {"modem.generic.unlock-required"});
        if (sim.empty() || sim == "/") state = SimState::Missing;
        else if (!unlock.empty() && unlock != "none" && unlock != "unknown") state = SimState::Locked;
        else state = SimState::Ready;
        return true;
    }

    bool raw_at(const std::string&, std::string&, std::string* error) override
    {
        set_error(error, "raw AT is unsupported while ModemManager owns the device");
        return false;
    }

    bool reset(std::string* error) override
    {
        const CommandResult result = run({"mmcli", "-m", selector_, "--reset"}, 30000);
        return output_ok(result, error, "ModemManager reset");
    }

private:
    void collect_calls(ModemEventBatch& out)
    {
        const CommandResult listed = run(
            {"mmcli", "-m", selector_, "--voice-list-calls", "-K"}, 10000);
        if (listed.exitCode != 0) return;
        const std::vector<std::string> paths = object_paths(
            parse_key_values(listed.output), "modem.voice.call.value", "Call");
        for (const std::string& path : paths) {
            const CommandResult inspected = run({"mmcli", "-o", path, "-K"}, 10000);
            if (inspected.exitCode != 0) continue;
            const auto call = parse_key_values(inspected.output);
            const std::string direction = first_value(call, {"call.properties.direction"});
            const std::string state = first_value(call, {"call.properties.state"});
            if (direction != "incoming" || (state != "ringing-in" && state != "waiting")) {
                continue;
            }
            std::string number = first_value(call, {"call.properties.number"});
            number.erase(std::remove_if(number.begin(), number.end(), [](unsigned char c) {
                return !(std::isdigit(c) || c == '+' || c == '*' || c == '#');
            }), number.end());
            out.urcLines.push_back("RING");
            out.urcLines.push_back("+CLIP: \"" + number + "\",129");
        }
    }

    CommandResult run(const std::vector<std::string>& args, int timeout_ms)
    {
        return runner_(args, timeout_ms);
    }

    std::string configuredSelector_;
    std::string selector_;
    CommandRunner runner_;
    ModemCapabilities capabilities_{false, false, false, false, true, false, true};
};

}  // namespace

std::unique_ptr<ModemBackend> create_modem_backend(const std::string& driver,
                                                   const std::string& endpoint,
                                                   int baud,
                                                   std::string* error)
{
    if (driver == "modemmanager" || driver == "mmcli") {
        return create_modemmanager_backend(endpoint);
    }
    std::unique_ptr<ModemDriver> modem_driver = create_modem_driver(driver);
    if (!modem_driver) {
        set_error(error, "unknown modem driver: " + driver);
        return {};
    }
    return std::make_unique<AtModemBackend>(driver, endpoint, baud, std::move(modem_driver));
}

std::unique_ptr<ModemBackend> create_modemmanager_backend(const std::string& selector,
                                                          CommandRunner runner)
{
    return std::make_unique<ModemManagerBackend>(selector, std::move(runner));
}

std::vector<std::string> list_modemmanager_modems(CommandRunner runner, std::string* error)
{
    CommandRunner command = effective_runner(std::move(runner));
    const CommandResult result = command({"mmcli", "-L", "-K"}, 10000);
    if (!output_ok(result, error, "ModemManager device list")) return {};
    return object_paths(parse_key_values(result.output), "modem-list.value", "Modem");
}

}  // namespace omnisms::linux_port
