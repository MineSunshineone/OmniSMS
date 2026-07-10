#include <iostream>
#include <string>
#include <vector>

static bool has_exact(const std::vector<std::string>& args, const std::string& expected)
{
    for (const std::string& arg : args) {
        if (arg == expected) return true;
    }
    return false;
}

static const std::string* with_prefix(const std::vector<std::string>& args,
                                      const std::string& prefix)
{
    for (const std::string& arg : args) {
        if (arg.rfind(prefix, 0) == 0) return &arg;
    }
    return nullptr;
}

int main(int argc, char** argv)
{
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);

    if (has_exact(args, "-L")) {
        std::cout << "modem-list.value[1] : "
                     "/org/freedesktop/ModemManager1/Modem/0 [fake modem]\n";
        return 0;
    }
    if (has_exact(args, "--messaging-status")) {
        std::cout << "modem.messaging.supported-storages : sm, me\n";
        return 0;
    }
    if (has_exact(args, "--3gpp-ussd-status")) {
        std::cout << "modem.3gpp.ussd.status : idle\n";
        return 0;
    }
    if (has_exact(args, "--voice-status")) {
        std::cout << "modem.voice.emergency-only : no\n";
        return 0;
    }
    if (const std::string* create = with_prefix(args, "--messaging-create-sms=")) {
        if (create->find("text='comma, unicode 世界'") == std::string::npos ||
            create->find("number='+8613900139000'") == std::string::npos) {
            std::cerr << "unexpected create argument: " << *create << '\n';
            return 3;
        }
        std::cout << "Successfully created new SMS:\n"
                     "  /org/freedesktop/ModemManager1/SMS/8 (unknown)\n";
        return 0;
    }
    if (has_exact(args, "--send")) {
        std::cout << "successfully sent the SMS\n";
        return 0;
    }
    if (with_prefix(args, "--messaging-delete-sms=")) {
        std::cout << "successfully deleted SMS\n";
        return 0;
    }
    if (with_prefix(args, "--3gpp-ussd-initiate=")) {
        std::cout << "modem.3gpp.ussd.network-request : balance 10.00\n";
        return 0;
    }
    if (has_exact(args, "-m") && has_exact(args, "-K")) {
        std::cout << "modem.generic.sim : /org/freedesktop/ModemManager1/SIM/0\n"
                     "modem.generic.unlock-required : none\n";
        return 0;
    }

    std::cerr << "unsupported fake mmcli invocation";
    for (const std::string& arg : args) std::cerr << ' ' << arg;
    std::cerr << '\n';
    return 2;
}
