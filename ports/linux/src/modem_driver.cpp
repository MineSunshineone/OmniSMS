#include "omnisms/linux/modem_driver.h"

#include <algorithm>
#include <map>
#include <mutex>

namespace omnisms::linux_port {
namespace {

std::map<std::string, ModemDriverFactory>& factories()
{
    static std::map<std::string, ModemDriverFactory> value;
    return value;
}

std::mutex& factory_mutex()
{
    static std::mutex value;
    return value;
}

}  // namespace

bool register_modem_driver(const std::string& id, ModemDriverFactory factory)
{
    if (id.empty() || !factory) return false;
    std::lock_guard<std::mutex> lock(factory_mutex());
    return factories().emplace(id, std::move(factory)).second;
}

std::unique_ptr<ModemDriver> create_modem_driver(const std::string& id)
{
    std::lock_guard<std::mutex> lock(factory_mutex());
    auto found = factories().find(id);
    return found == factories().end() ? nullptr : found->second();
}

std::vector<std::string> registered_modem_drivers()
{
    std::lock_guard<std::mutex> lock(factory_mutex());
    std::vector<std::string> ids;
    ids.reserve(factories().size());
    for (const auto& item : factories()) ids.push_back(item.first);
    return ids;
}

}  // namespace omnisms::linux_port
