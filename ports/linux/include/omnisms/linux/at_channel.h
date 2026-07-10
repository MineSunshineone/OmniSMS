#pragma once

#include <string>

namespace omnisms::linux_port {

// AT 物理通道。ModemDriver 只依赖该接口，不直接操作 termios/UART。
class AtChannel {
public:
    virtual ~AtChannel() = default;
    virtual std::string command(const std::string& command, int timeout_ms) = 0;
    virtual std::string command_with_payload(const std::string& command,
                                             const std::string& payload,
                                             const std::string& prompt,
                                             int timeout_ms) = 0;
    virtual std::string read_available(int wait_ms) = 0;

    bool ok(const std::string& command, int timeout_ms = 2000);
};

class SerialAtChannel final : public AtChannel {
public:
    ~SerialAtChannel() override;

    bool open_port(const std::string& endpoint, int baud, std::string* error = nullptr);
    std::string command(const std::string& command, int timeout_ms) override;
    std::string command_with_payload(const std::string& command, const std::string& payload,
                                     const std::string& prompt, int timeout_ms) override;
    std::string read_available(int wait_ms) override;

private:
    int fd_ = -1;
};

}  // namespace omnisms::linux_port
