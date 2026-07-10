#include <cstdio>
#include <algorithm>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "omnisms/linux/modem_driver.h"

using namespace omnisms::linux_port;

static int g_failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        ++g_failures; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

class FakeAtChannel final : public AtChannel {
public:
    std::string command(const std::string& command, int) override
    {
        commands.push_back(command);
        auto found = responses.find(command);
        return found == responses.end() ? "\r\nOK\r\n" : found->second;
    }

    std::string command_with_payload(const std::string& command, const std::string& payload,
                                     const std::string& prompt, int) override
    {
        commands.push_back(command);
        last_payload = payload;
        last_prompt = prompt;
        auto found = responses.find(command);
        return found == responses.end() ? "\r\n> \r\n+CMGS: 12\r\n\r\nOK\r\n" : found->second;
    }

    std::string read_available(int) override
    {
        if (read_chunks.empty()) return {};
        std::string chunk = read_chunks.front();
        read_chunks.pop_front();
        return chunk;
    }

    std::map<std::string, std::string> responses;
    std::vector<std::string> commands;
    std::deque<std::string> read_chunks;
    std::string last_payload;
    std::string last_prompt;
};

int main()
{
    std::vector<std::string> ids = registered_modem_drivers();
    CHECK(std::find(ids.begin(), ids.end(), "generic-3gpp") != ids.end());
    CHECK(!create_modem_driver("missing"));

    std::unique_ptr<ModemDriver> driver = create_modem_driver("generic-3gpp");
    CHECK(driver && std::string(driver->id()) == "generic-3gpp");

    FakeAtChannel channel;
    std::string error;
    CHECK(driver->initialize(channel, &error));
    CHECK(channel.commands.size() == 5);
    CHECK(channel.commands[0] == "AT" && channel.commands[4] == "AT+CLIP=1");

    channel.responses["AT+CMGL=4"] =
        "\r\nRING\r\n+CLIP: \"10086\",129\r\n"
        "+CMGL: 3,0,,23\r\n001122AABB\r\n"
        "+CMTI: \"SM\",4\r\n"
        "+CMGL: 4,0,,18\r\nCCDDEEFF\r\nOK\r\n";
    ModemPollResult result;
    CHECK(driver->poll(channel, result, &error));
    CHECK(result.messages.size() == 2);
    CHECK(result.messages[0].index == 3 && result.messages[0].pdu == "001122AABB");
    CHECK(result.messages[1].index == 4 && result.messages[1].pdu == "CCDDEEFF");
    CHECK(result.urcLines.size() == 3);

    CHECK(driver->delete_sms(channel, 3));
    CHECK(channel.commands.back() == "AT+CMGD=3");

    std::string response;
    CHECK(driver->send_pdu(channel, 23, "001122\x1A", &response));
    CHECK(channel.commands.back() == "AT+CMGS=23");
    CHECK(channel.last_payload == "001122\x1A" && channel.last_prompt == ">");

    ModemPollResult urc;
    driver->consume_urc("\r\nRING\r\n+CMT: ,23\r\n0011", urc);
    driver->consume_urc("22AABB\r\n+CLIP: \"10010\",129\r\n", urc);
    CHECK(urc.messages.size() == 1 && urc.messages[0].index == -1);
    CHECK(urc.messages[0].pdu == "001122AABB");
    CHECK(urc.urcLines.size() == 3);

    SimState state = SimState::Unknown;
    channel.responses["AT+CPIN?"] = "\r\n+CPIN: READY\r\n\r\nOK\r\n";
    CHECK(driver->query_sim_state(channel, state, &error) && state == SimState::Ready);
    channel.responses["AT+CPIN?"] = "\r\n+CME ERROR: 10\r\n";
    CHECK(driver->query_sim_state(channel, state, &error) && state == SimState::Missing);

    channel.responses["AT+CUSD=1,\"*100#\",15"] =
        "\r\n+CUSD: 0,\"balance: 10\",15\r\n\r\nOK\r\n";
    CHECK(driver->send_ussd(channel, "*100#", response, &error));
    CHECK(response == "balance: 10");
    channel.responses["AT+CUSD=1,\"*101#\",15"] = "\r\nOK\r\n";
    channel.read_chunks.push_back("\r\n+CUSD: 0,\"async result\",15\r\n");
    CHECK(driver->send_ussd(channel, "*101#", response, &error));
    CHECK(response == "async result");
    CHECK(!driver->send_ussd(channel, "bad ussd", response, &error));

    if (g_failures == 0) {
        printf("all modem driver tests passed\n");
        return 0;
    }
    fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
}
