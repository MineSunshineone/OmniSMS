#include "omnisms/linux/web_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>

#include "omnisms/crypto.h"
#include "omnisms/text.h"

namespace omnisms::linux_port {
namespace {

constexpr size_t MAX_REQUEST_BYTES = 64 * 1024;

std::string lower(std::string value)
{
    for (char& ch : value) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return value;
}

std::string read_file(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream stream;
    stream << in.rdbuf();
    return stream.str();
}

void replace_all(std::string& value, const std::string& needle, const std::string& replacement)
{
    size_t pos = 0;
    while ((pos = value.find(needle, pos)) != std::string::npos) {
        value.replace(pos, needle.size(), replacement);
        pos += replacement.size();
    }
}

const char* status_text(int status)
{
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Content Too Large";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        default: return "Error";
    }
}

bool send_all(int fd, const std::string& data)
{
    size_t offset = 0;
    while (offset < data.size()) {
        ssize_t sent = ::send(fd, data.data() + offset, data.size() - offset, MSG_NOSIGNAL);
        if (sent < 0 && errno == EINTR) continue;
        if (sent <= 0) return false;
        offset += static_cast<size_t>(sent);
    }
    return true;
}

std::string query_value(const std::string& query, const std::string& wanted)
{
    size_t pos = 0;
    while (pos <= query.size()) {
        size_t end = query.find('&', pos);
        if (end == std::string::npos) end = query.size();
        std::string item = query.substr(pos, end - pos);
        size_t equal = item.find('=');
        std::string key = url_decode_component(item.substr(0, equal));
        if (key == wanted) {
            return equal == std::string::npos ? std::string() :
                   url_decode_component(item.substr(equal + 1));
        }
        if (end == query.size()) break;
        pos = end + 1;
    }
    return {};
}

bool safe_panel_name(const std::string& name)
{
    if (name.empty() || name.size() > 32) return false;
    for (char ch : name) {
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-')) return false;
    }
    return true;
}

WebResponse json_error(int status, const std::string& message)
{
    WebResponse response;
    response.status = status;
    response.body = "{\"success\":false,\"message\":\"" + json_escape(message) + "\"}";
    return response;
}

}  // namespace

struct LinuxWebServer::Impl {
    explicit Impl(WebServerOptions opts, WebCallbacks cb)
        : options(std::move(opts)), callbacks(std::move(cb)) {}

    WebServerOptions options;
    mutable std::mutex optionsMutex;
    WebCallbacks callbacks;
    std::atomic<bool> stopping{false};
    std::atomic<int> port{0};
    int listenFd = -1;
    std::thread thread;

    bool authorized(const WebRequest& request) const
    {
        std::string username;
        std::string password;
        {
            std::lock_guard<std::mutex> lock(optionsMutex);
            username = options.username;
            password = options.password;
        }
        if (username.empty()) return true;
        auto found = request.headers.find("authorization");
        if (found == request.headers.end()) return false;
        const std::string credentials = username + ":" + password;
        const std::string expected = "Basic " + base64_encode(
            reinterpret_cast<const uint8_t*>(credentials.data()), credentials.size());
        return found->second == expected;
    }

    WebResponse asset(const std::string& relative, const std::string& content_type) const
    {
        std::string asset_root;
        std::string asset_hash;
        {
            std::lock_guard<std::mutex> lock(optionsMutex);
            asset_root = options.assetRoot;
            asset_hash = options.assetHash;
        }
        if (asset_root.empty()) {
            return json_error(500, "web.assetRoot is not configured");
        }
        WebResponse response;
        response.contentType = content_type;
        response.body = read_file(asset_root + "/" + relative);
        if (response.body.empty()) return json_error(404, "Web asset not found: " + relative);
        replace_all(response.body, "{{ASSET_HASH}}", asset_hash);
        response.headers["Cache-Control"] = "no-cache";
        return response;
    }

    WebResponse route(const WebRequest& request) const
    {
        if (!authorized(request)) {
            WebResponse response = json_error(401, "Authentication required");
            response.headers["WWW-Authenticate"] = "Basic realm=\"OmniSMS\"";
            return response;
        }
        if (request.method == "POST") {
            auto csrf = request.headers.find("x-sms-csrf");
            if (csrf == request.headers.end() || csrf->second != "1") {
                return json_error(403, "CSRF header missing");
            }
        } else if (request.method != "GET") {
            return json_error(405, "Method not allowed");
        }

        if (request.method == "GET") {
            if (request.path == "/" || request.path == "/index.html") {
                return asset("index.html", "text/html; charset=utf-8");
            }
            if (request.path == "/app.css") return asset("app.css", "text/css; charset=utf-8");
            if (request.path == "/app.js") return asset("app.js", "application/javascript; charset=utf-8");
            if (request.path == "/ap.html") return asset("ap.html", "text/html; charset=utf-8");
            if (request.path == "/ui") {
                std::string panel = query_value(request.query, "panel");
                if (!safe_panel_name(panel)) return json_error(400, "Invalid panel");
                return asset("panels/" + panel + ".html", "text/html; charset=utf-8");
            }
            if (request.path == "/config.json") {
                WebResponse response;
                response.body = callbacks.configJson ? callbacks.configJson() : "{}";
                return response;
            }
            if (request.path == "/status") {
                WebResponse response;
                response.body = callbacks.statusJson ? callbacks.statusJson() : "{}";
                return response;
            }
            if (request.path == "/messages") {
                WebResponse response;
                response.body = callbacks.messagesJson ? callbacks.messagesJson(request) : "[]";
                return response;
            }
            if (request.path == "/export") {
                WebResponse response;
                response.body = callbacks.exportJson ? callbacks.exportJson(request) : "{}";
                response.headers["Content-Disposition"] = "attachment; filename=omnisms_config.json";
                return response;
            }
        }
        if (callbacks.dynamic) return callbacks.dynamic(request);
        return json_error(501, "Endpoint is not implemented on this platform");
    }

    bool parse_request(int fd, WebRequest& request) const
    {
        std::string data;
        data.reserve(4096);
        size_t header_end = std::string::npos;
        while (header_end == std::string::npos && data.size() < MAX_REQUEST_BYTES) {
            char buffer[4096];
            ssize_t got = recv(fd, buffer, sizeof(buffer), 0);
            if (got < 0 && errno == EINTR) continue;
            if (got <= 0) return false;
            data.append(buffer, static_cast<size_t>(got));
            header_end = data.find("\r\n\r\n");
        }
        if (header_end == std::string::npos) return false;

        std::istringstream lines(data.substr(0, header_end));
        std::string first;
        if (!std::getline(lines, first)) return false;
        if (!first.empty() && first.back() == '\r') first.pop_back();
        std::istringstream first_line(first);
        std::string target;
        std::string version;
        if (!(first_line >> request.method >> target >> version)) return false;
        size_t question = target.find('?');
        request.path = target.substr(0, question);
        request.query = question == std::string::npos ? std::string() : target.substr(question + 1);

        std::string line;
        while (std::getline(lines, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            request.headers[lower(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1));
        }

        size_t content_length = 0;
        auto length = request.headers.find("content-length");
        if (length != request.headers.end()) {
            char* end = nullptr;
            unsigned long parsed = std::strtoul(length->second.c_str(), &end, 10);
            if (!end || *end || parsed > MAX_REQUEST_BYTES) return false;
            content_length = static_cast<size_t>(parsed);
        }
        request.body = data.substr(header_end + 4);
        while (request.body.size() < content_length) {
            char buffer[4096];
            size_t want = std::min(sizeof(buffer), content_length - request.body.size());
            ssize_t got = recv(fd, buffer, want, 0);
            if (got < 0 && errno == EINTR) continue;
            if (got <= 0) return false;
            request.body.append(buffer, static_cast<size_t>(got));
        }
        if (request.body.size() > content_length) request.body.resize(content_length);
        return true;
    }

    void handle_client(int fd) const
    {
        timeval timeout{10, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        WebRequest request;
        WebResponse response = parse_request(fd, request)
                                   ? route(request) : json_error(400, "Invalid HTTP request");
        std::string header = "HTTP/1.1 " + std::to_string(response.status) + " " +
                             status_text(response.status) + "\r\n";
        header += "Content-Type: " + response.contentType + "\r\n";
        header += "Content-Length: " + std::to_string(response.body.size()) + "\r\n";
        header += "Connection: close\r\n";
        header += "Cache-Control: no-store\r\n";
        for (const auto& item : response.headers) header += item.first + ": " + item.second + "\r\n";
        header += "\r\n";
        send_all(fd, header);
        send_all(fd, response.body);
    }

    void run()
    {
        while (!stopping.load()) {
            pollfd descriptor{listenFd, POLLIN, 0};
            int ready = poll(&descriptor, 1, 500);
            if (ready < 0 && errno == EINTR) continue;
            if (ready <= 0 || !(descriptor.revents & POLLIN)) continue;
            int client = accept(listenFd, nullptr, nullptr);
            if (client < 0) continue;
            handle_client(client);
            close(client);
        }
    }
};

LinuxWebServer::LinuxWebServer(WebServerOptions options, WebCallbacks callbacks)
    : impl_(new Impl(std::move(options), std::move(callbacks))) {}

LinuxWebServer::~LinuxWebServer()
{
    stop();
}

bool LinuxWebServer::start(std::string* error)
{
    if (error) error->clear();
    if (impl_->listenFd >= 0) return true;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        if (error) *error = std::strerror(errno);
        return false;
    }
    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(impl_->options.port));
    if (inet_pton(AF_INET, impl_->options.listenAddress.c_str(), &address.sin_addr) != 1) {
        if (error) *error = "web.listenAddress 必须是 IPv4 地址";
        close(fd);
        return false;
    }
    if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || listen(fd, 16) != 0) {
        if (error) *error = std::strerror(errno);
        close(fd);
        return false;
    }
    socklen_t size = sizeof(address);
    if (getsockname(fd, reinterpret_cast<sockaddr*>(&address), &size) == 0) {
        impl_->port.store(ntohs(address.sin_port));
    }
    impl_->listenFd = fd;
    impl_->stopping.store(false);
    impl_->thread = std::thread([this] { impl_->run(); });
    return true;
}

void LinuxWebServer::stop()
{
    if (!impl_ || impl_->listenFd < 0) return;
    impl_->stopping.store(true);
    shutdown(impl_->listenFd, SHUT_RDWR);
    close(impl_->listenFd);
    impl_->listenFd = -1;
    if (impl_->thread.joinable()) impl_->thread.join();
}

int LinuxWebServer::bound_port() const
{
    return impl_->port.load();
}

void LinuxWebServer::update_runtime_options(const std::string& asset_root,
                                            const std::string& username,
                                            const std::string& password)
{
    std::lock_guard<std::mutex> lock(impl_->optionsMutex);
    impl_->options.assetRoot = asset_root;
    impl_->options.username = username;
    impl_->options.password = password;
}

}  // namespace omnisms::linux_port
