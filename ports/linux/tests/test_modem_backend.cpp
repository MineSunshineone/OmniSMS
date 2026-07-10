#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "omnisms/linux/modem_backend.h"

using namespace omnisms::linux_port;

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); ++failures; } } while (0)

static std::string joined(const std::vector<std::string>& args)
{
    std::string out;
    for (const std::string& arg : args) {
        if (!out.empty()) out += '|';
        out += arg;
    }
    return out;
}

int main()
{
    std::vector<std::string> calls;
    CommandRunner runner = [&](const std::vector<std::string>& args, int) {
        calls.push_back(joined(args));
        const std::string command = calls.back();
        if (command == "mmcli|-L|-K") {
            return CommandResult{0, "modem-list.value[1] : /org/freedesktop/ModemManager1/Modem/0 [test modem]\n"};
        }
        if (command == "mmcli|-m|/org/freedesktop/ModemManager1/Modem/0|-K") {
            return CommandResult{0, "modem.generic.sim : /org/freedesktop/ModemManager1/SIM/0\n"
                                    "modem.generic.unlock-required : none\n"};
        }
        if (command.find("--messaging-list-sms") != std::string::npos) {
            return CommandResult{0, "modem.messaging.sms.value[1] : /org/freedesktop/ModemManager1/SMS/7 (received)\n"};
        }
        if (command == "mmcli|-s|/org/freedesktop/ModemManager1/SMS/7|-K") {
            return CommandResult{0, "sms.properties.state : received\n"
                                    "sms.content.number : +8613800138000\n"
                                    "sms.content.text : hello, 世界\n"
                                    "sms.properties.timestamp : 2026-07-10T12:34:56+08:00\n"};
        }
        if (command.find("--voice-list-calls") != std::string::npos) {
            return CommandResult{0, "modem.voice.call.value[1] : /org/freedesktop/ModemManager1/Call/4 (ringing-in)\n"
                                    "modem.voice.call.value[2] : /org/freedesktop/ModemManager1/Call/5 (active)\n"};
        }
        if (command == "mmcli|-o|/org/freedesktop/ModemManager1/Call/4|-K") {
            return CommandResult{0, "call.properties.direction : incoming\n"
                                    "call.properties.state : ringing-in\n"
                                    "call.properties.number : +86 (138) 0013-8000\n"};
        }
        if (command == "mmcli|-o|/org/freedesktop/ModemManager1/Call/5|-K") {
            return CommandResult{0, "call.properties.direction : outgoing\n"
                                    "call.properties.state : active\n"
                                    "call.properties.number : +8613900139000\n"};
        }
        if (command.find("--messaging-create-sms=") != std::string::npos) {
            return CommandResult{0, "Successfully created new SMS: /org/freedesktop/ModemManager1/SMS/8\n"};
        }
        if (command.find("--3gpp-ussd-initiate=") != std::string::npos) {
            return CommandResult{0, "modem.3gpp.ussd.network-request : balance 10.00\n"};
        }
        return CommandResult{0, "success\n"};
    };

    std::string error;
    std::unique_ptr<ModemBackend> backend = create_modemmanager_backend("auto", runner);
    CHECK(backend->initialize(&error));
    CHECK(backend->endpoint() == "/org/freedesktop/ModemManager1/Modem/0");
    CHECK(backend->capabilities().textSms);
    CHECK(backend->capabilities().incomingCall);
    CHECK(backend->capabilities().ussd);
    CHECK(!backend->capabilities().rawAt);

    SimState sim = SimState::Unknown;
    CHECK(backend->query_sim_state(sim, &error));
    CHECK(sim == SimState::Ready);

    ModemEventBatch events;
    CHECK(backend->collect_events(events, true, 0, &error));
    CHECK(events.messages.size() == 1);
    CHECK(events.urcLines.size() == 2);
    if (events.urcLines.size() == 2) {
        CHECK(events.urcLines[0] == "RING");
        CHECK(events.urcLines[1] == "+CLIP: \"+8613800138000\",129");
    }
    if (!events.messages.empty()) {
        CHECK(events.messages[0].id == "/org/freedesktop/ModemManager1/SMS/7");
        CHECK(events.messages[0].sender == "+8613800138000");
        CHECK(events.messages[0].text == "hello, 世界");
        CHECK(events.messages[0].timestamp == "2026-07-10T12:34:56+08:00");
        CHECK(!events.messages[0].pduEncoded);
        CHECK(backend->acknowledge_sms(events.messages[0].id, &error));
    }

    CHECK(backend->send_sms("+8613900139000", "comma, unicode 世界", &error));
    CHECK(!backend->send_sms("not-a-phone", "text", &error));
    std::string response;
    CHECK(backend->send_ussd("*100#", response, &error));
    CHECK(response == "balance 10.00");
    CHECK(backend->reset(&error));
    CHECK(!backend->raw_at("AT", response, &error));

    CommandRunner multiple = [](const std::vector<std::string>& args, int) {
        if (args.size() >= 2 && args[1] == "-L") {
            return CommandResult{0, "modem-list.value[1] : /org/freedesktop/ModemManager1/Modem/0\n"
                                    "modem-list.value[2] : /org/freedesktop/ModemManager1/Modem/1\n"};
        }
        return CommandResult{0, {}};
    };
    backend = create_modemmanager_backend("auto", multiple);
    error.clear();
    CHECK(!backend->initialize(&error));
    CHECK(error.find("multiple") != std::string::npos);

    CommandRunner limited = [](const std::vector<std::string>& args, int) {
        const std::string command = joined(args);
        if (command == "mmcli|-m|0|--messaging-status|-K") return CommandResult{0, "supported\n"};
        if (command.find("--3gpp-ussd-status") != std::string::npos ||
            command.find("--voice-status") != std::string::npos) {
            return CommandResult{1, "unsupported\n"};
        }
        return CommandResult{0, "modem.generic.sim : /org/freedesktop/ModemManager1/SIM/0\n"};
    };
    backend = create_modemmanager_backend("0", limited);
    error.clear();
    CHECK(backend->initialize(&error));
    CHECK(backend->capabilities().textSms);
    CHECK(!backend->capabilities().ussd);
    CHECK(!backend->capabilities().incomingCall);
    response.clear();
    CHECK(!backend->send_ussd("*100#", response, &error));
    CHECK(error.find("unsupported") != std::string::npos);

    CommandRunner no_messaging = [](const std::vector<std::string>& args, int) {
        const std::string command = joined(args);
        if (command.find("--messaging-status") != std::string::npos) {
            return CommandResult{1, "error: modem has no messaging capabilities\n"};
        }
        return CommandResult{0, "modem.generic.sim : /org/freedesktop/ModemManager1/SIM/0\n"};
    };
    backend = create_modemmanager_backend("0", no_messaging);
    error.clear();
    CHECK(!backend->initialize(&error));
    CHECK(error.find("messaging capability") != std::string::npos);

    if (failures == 0) std::printf("all modem backend tests passed\n");
    return failures == 0 ? 0 : 1;
}
