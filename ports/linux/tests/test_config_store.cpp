#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include "omnisms/linux/config_store.h"

using namespace omnisms;
using namespace omnisms::linux_port;

static int g_failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        ++g_failures; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

static std::string read_all(const std::string& path)
{
    std::ifstream in(path);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

static void test_scoped_forms()
{
    AppConfig current;
    current.web.username = "admin";
    current.web.password = "old-secret";
    current.pushChannels[0].type = PUSH_TYPE_BARK;
    current.pushChannels[0].key1 = "saved-key";
    current.scheduledTasks[0].lastRun = 1700000000U;

    ConfigUpdateResult result = apply_web_config_form(current, {
        {"accountForm", "1"}, {"webUser", "owner"}, {"webPass", ""}
    });
    CHECK(result.success);
    CHECK(result.config.web.username == "owner");
    CHECK(result.config.web.password == "old-secret");
    CHECK(!result.restartRequired);

    result = apply_web_config_form(current, {
        {"pushForm", "1"}, {"pushEnabled", "on"}, {"push0en", "on"},
        {"push0type", "2"}, {"push0name", "phone"}, {"push0url", ""},
        {"push0key1", ""}, {"push0key1Keep", "1"}, {"push0key2", ""},
        {"push0key2Keep", "0"}, {"push0body", ""}
    });
    CHECK(result.success);
    CHECK(result.config.pushChannels[0].key1 == "saved-key");
    CHECK(result.config.pushChannels[0].enabled);

    result = apply_web_config_form(current, {
        {"stForm", "1"}, {"st0En", "on"}, {"st0Name", "monthly"},
        {"st0Days", "30"}, {"st0Act", "3"}, {"st0Tgt", "*100#"}
    });
    CHECK(result.success);
    CHECK(result.config.scheduledTasks[0].lastRun == 1700000000U);
    CHECK(result.config.scheduledTasks[0].target == "*100#");

    result = apply_web_config_form(current, {
        {"rulesForm", "1"}, {"forwardRules", "re\t[unclosed\t1"}
    });
    CHECK(!result.success);
    CHECK(result.message.find("转发规则") != std::string::npos);

    result = apply_web_config_form(current, {
        {"tzForm", "1"}, {"tzOffsetMin", "9999"}
    });
    CHECK(!result.success);
}

static void test_import_and_atomic_write()
{
    AppConfig current;
    current.modem.endpoint = "/dev/ttyUSB2";
    current.web.password = "keep-web";
    current.email.password = "keep-smtp";
    current.pushChannels[0].key1 = "keep-push";

    ConfigUpdateResult result = import_portable_config(
        current,
        "{\"schemaVersion\":1,\"receiver\":\"+8613800138000\","
        "\"email\":{\"enabled\":true,\"server\":\"smtp.example.com\","
        "\"port\":465,\"username\":\"bot@example.com\","
        "\"password\":\"__REDACTED__\",\"recipient\":\"me@example.com\"}}"
    );
    CHECK(result.success);
    CHECK(result.applied == 2);
    CHECK(result.config.receiver == "+8613800138000");
    CHECK(result.config.email.password == "keep-smtp");
    CHECK(result.config.modem.endpoint == "/dev/ttyUSB2");

    result = import_portable_config(current,
        "webPass=__REDACTED__\n"
        "smtpPass=next-smtp\n"
        "push0k1=next-push\n"
        "kaEnabled=1\n"
        "kaIntervalDays=90\n");
    CHECK(result.success);
    CHECK(result.config.web.password == "keep-web");
    CHECK(result.config.email.password == "next-smtp");
    CHECK(result.config.pushChannels[0].key1 == "next-push");
    CHECK(result.config.keepalive.enabled);

    char directory_template[] = "/tmp/omnisms-config-test-XXXXXX";
    char* directory = mkdtemp(directory_template);
    CHECK(directory != nullptr);
    if (!directory) return;
    const std::string path = std::string(directory) + "/omnisms.json";
    std::string error;
    CHECK(atomic_write_config(path, result.config, &error));
    CHECK(error.empty());
    AppConfig parsed;
    CHECK(parse_config_json(read_all(path), parsed, &error));
    CHECK(parsed.keepalive.intervalDays == 90);
    struct stat info{};
    CHECK(stat(path.c_str(), &info) == 0);
    CHECK((info.st_mode & 0777) == 0600);
    CHECK(access((path + ".tmp." + std::to_string(static_cast<long long>(getpid()))).c_str(), F_OK) != 0);
    const std::string state_path = std::string(directory) + "/messages.state";
    CHECK(atomic_write_file(state_path, "state-v1\nline-2\n", &error));
    CHECK(read_all(state_path) == "state-v1\nline-2\n");
    unlink(path.c_str());
    unlink(state_path.c_str());
    rmdir(directory);
}

int main()
{
    test_scoped_forms();
    test_import_and_atomic_write();
    if (g_failures == 0) {
        printf("all Linux config store tests passed\n");
        return 0;
    }
    fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
}
