#include "omnisms/linux/at_channel.h"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <thread>

namespace omnisms::linux_port {
namespace {

bool baud_speed(int baud, speed_t& speed)
{
    switch (baud) {
        case 9600: speed = B9600; return true;
        case 19200: speed = B19200; return true;
        case 38400: speed = B38400; return true;
        case 57600: speed = B57600; return true;
        case 115200: speed = B115200; return true;
#ifdef B230400
        case 230400: speed = B230400; return true;
#endif
#ifdef B460800
        case 460800: speed = B460800; return true;
#endif
#ifdef B921600
        case 921600: speed = B921600; return true;
#endif
        default: return false;
    }
}

void set_error(std::string* error, const std::string& message)
{
    if (error) *error = message;
}

bool write_all(int fd, const std::string& data,
               const std::chrono::steady_clock::time_point& deadline)
{
    size_t offset = 0;
    while (offset < data.size() && std::chrono::steady_clock::now() < deadline) {
        ssize_t written = ::write(fd, data.data() + offset, data.size() - offset);
        if (written > 0) {
            offset += static_cast<size_t>(written);
        } else if (written < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            return false;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    return offset == data.size();
}

bool terminal_response(const std::string& response)
{
    return response.find("\r\nOK\r\n") != std::string::npos ||
           response.find("\r\nERROR") != std::string::npos ||
           response.find("+CMS ERROR") != std::string::npos ||
           response.find("+CME ERROR") != std::string::npos;
}

bool read_some(int fd, std::string& response)
{
    char buffer[512];
    ssize_t count = ::read(fd, buffer, sizeof(buffer));
    if (count > 0) {
        response.append(buffer, static_cast<size_t>(count));
        return true;
    }
    if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    return true;
}

}  // namespace

bool AtChannel::ok(const std::string& command_text, int timeout_ms)
{
    return command(command_text, timeout_ms).find("\r\nOK\r\n") != std::string::npos;
}

SerialAtChannel::~SerialAtChannel()
{
    if (fd_ >= 0) ::close(fd_);
}

bool SerialAtChannel::open_port(const std::string& endpoint, int baud, std::string* error)
{
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    speed_t speed = B115200;
    if (!baud_speed(baud, speed)) {
        set_error(error, "不支持的串口波特率: " + std::to_string(baud));
        return false;
    }
    fd_ = ::open(endpoint.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) {
        set_error(error, "打开串口失败 " + endpoint + ": " + std::strerror(errno));
        return false;
    }
    termios options = {};
    if (tcgetattr(fd_, &options) != 0) {
        set_error(error, "读取串口参数失败: " + std::string(std::strerror(errno)));
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    cfmakeraw(&options);
    cfsetispeed(&options, speed);
    cfsetospeed(&options, speed);
    options.c_cflag |= CLOCAL | CREAD;
    if (tcsetattr(fd_, TCSANOW, &options) != 0) {
        set_error(error, "设置串口参数失败: " + std::string(std::strerror(errno)));
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    return true;
}

std::string SerialAtChannel::command(const std::string& command_text, int timeout_ms)
{
    if (fd_ < 0) return {};
    const std::string wire = command_text + "\r";
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    if (!write_all(fd_, wire, deadline)) return {};

    std::string response;
    while (std::chrono::steady_clock::now() < deadline) {
        if (!read_some(fd_, response) || terminal_response(response)) break;
    }
    return response;
}

std::string SerialAtChannel::command_with_payload(const std::string& command_text,
                                                   const std::string& payload,
                                                   const std::string& prompt,
                                                   int timeout_ms)
{
    if (fd_ < 0 || prompt.empty()) return {};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    if (!write_all(fd_, command_text + "\r", deadline)) return {};

    std::string response;
    while (std::chrono::steady_clock::now() < deadline) {
        if (!read_some(fd_, response)) return response;
        if (terminal_response(response)) return response;
        if (response.find(prompt) != std::string::npos) break;
    }
    if (response.find(prompt) == std::string::npos) return response;
    if (!write_all(fd_, payload, deadline)) return response;
    while (std::chrono::steady_clock::now() < deadline) {
        if (!read_some(fd_, response) || terminal_response(response)) break;
    }
    return response;
}

std::string SerialAtChannel::read_available(int wait_ms)
{
    if (fd_ < 0 || wait_ms < 0) return {};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(wait_ms);
    std::string response;
    while (std::chrono::steady_clock::now() < deadline) {
        if (!read_some(fd_, response)) break;
    }
    return response;
}

}  // namespace omnisms::linux_port
