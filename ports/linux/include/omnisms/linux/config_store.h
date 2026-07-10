#pragma once

#include <map>
#include <string>

#include "omnisms/config.h"

namespace omnisms::linux_port {

struct ConfigUpdateResult {
    bool success = false;
    AppConfig config;
    int applied = 0;
    bool restartRequired = false;
    std::string message;
};

// Apply one of the scoped forms emitted by the unchanged ESP Web UI.
ConfigUpdateResult apply_web_config_form(
    const AppConfig& current, const std::map<std::string, std::string>& fields);

// Import schema-v1 JSON or the legacy key=value export used by the ESP baseline.
// JSON is merged by top-level presence so platform-specific sections are preserved.
ConfigUpdateResult import_portable_config(const AppConfig& current, const std::string& raw);

// Write a complete schema-v1 JSON file beside a temporary file, fsync it, then rename it.
// Existing Unix permission bits are retained when the destination already exists.
bool atomic_write_file(const std::string& path, const std::string& data,
                       std::string* error = nullptr);
bool atomic_write_config(const std::string& path, const AppConfig& config,
                         std::string* error = nullptr);

}  // namespace omnisms::linux_port
