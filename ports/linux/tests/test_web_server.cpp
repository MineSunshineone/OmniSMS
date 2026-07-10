#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <string>

#include "omnisms/crypto.h"
#include "omnisms/linux/web_server.h"

using namespace omnisms;
using namespace omnisms::linux_port;

static int g_failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        ++g_failures; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

static std::string request(int port, const std::string& wire)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return {};
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        close(fd);
        return {};
    }
    size_t offset = 0;
    while (offset < wire.size()) {
        ssize_t sent = send(fd, wire.data() + offset, wire.size() - offset, 0);
        if (sent <= 0) break;
        offset += static_cast<size_t>(sent);
    }
    shutdown(fd, SHUT_WR);
    std::string response;
    char buffer[4096];
    ssize_t got = 0;
    while ((got = recv(fd, buffer, sizeof(buffer), 0)) > 0) {
        response.append(buffer, static_cast<size_t>(got));
    }
    close(fd);
    return response;
}

static std::string auth_header(const std::string& username = "tester",
                               const std::string& password = "secret")
{
    const std::string credentials = username + ":" + password;
    return "Authorization: Basic " + base64_encode(
        reinterpret_cast<const uint8_t*>(credentials.data()), credentials.size()) + "\r\n";
}

int main()
{
    WebServerOptions options;
    options.listenAddress = "127.0.0.1";
    options.port = 0;
    options.assetRoot = OMNISMS_WEBUI_SOURCE_DIR;
    options.username = "tester";
    options.password = "secret";
    options.assetHash = "linux-test-hash";

    WebCallbacks callbacks;
    callbacks.configJson = [] { return std::string("{\"schemaVersion\":1}"); };
    callbacks.statusJson = [] { return std::string("{\"platform\":\"linux\"}"); };
    callbacks.messagesJson = [](const WebRequest&) { return std::string("[]"); };
    callbacks.exportJson = [](const WebRequest&) { return std::string("{\"schemaVersion\":1}"); };
    callbacks.dynamic = [](const WebRequest& req) {
        if (req.path == "/echo") {
            WebResponse response;
            response.body = "{\"body\":\"" + req.body + "\"}";
            return response;
        }
        WebResponse response;
        response.status = 501;
        response.body = "{\"success\":false}";
        return response;
    };

    LinuxWebServer server(options, callbacks);
    std::string error;
    CHECK(server.start(&error));
    CHECK(error.empty());
    CHECK(server.bound_port() > 0);
    const int port = server.bound_port();

    std::string response = request(port,
        "GET /config.json HTTP/1.1\r\nHost: localhost\r\n\r\n");
    CHECK(response.find("HTTP/1.1 401 Unauthorized") == 0);
    CHECK(response.find("WWW-Authenticate: Basic") != std::string::npos);

    response = request(port,
        "GET /config.json HTTP/1.1\r\nHost: localhost\r\n" + auth_header() + "\r\n");
    CHECK(response.find("HTTP/1.1 200 OK") == 0);
    CHECK(response.find("{\"schemaVersion\":1}") != std::string::npos);

    response = request(port,
        "GET /ui?panel=overview HTTP/1.1\r\nHost: localhost\r\n" + auth_header() + "\r\n");
    CHECK(response.find("HTTP/1.1 200 OK") == 0);
    CHECK(response.find("{{ASSET_HASH}}") == std::string::npos);

    response = request(port,
        "GET /index.html HTTP/1.1\r\nHost: localhost\r\n" + auth_header() + "\r\n");
    CHECK(response.find("linux-test-hash") != std::string::npos);

    response = request(port,
        "POST /echo HTTP/1.1\r\nHost: localhost\r\n" + auth_header() +
        "Content-Length: 3\r\n\r\nabc");
    CHECK(response.find("HTTP/1.1 403 Forbidden") == 0);

    response = request(port,
        "POST /echo HTTP/1.1\r\nHost: localhost\r\n" + auth_header() +
        "X-SMS-CSRF: 1\r\nContent-Length: 3\r\n\r\nabc");
    CHECK(response.find("HTTP/1.1 200 OK") == 0);
    CHECK(response.find("{\"body\":\"abc\"}") != std::string::npos);

    response = request(port,
        "GET /unsupported HTTP/1.1\r\nHost: localhost\r\n" + auth_header() + "\r\n");
    CHECK(response.find("HTTP/1.1 501 Not Implemented") == 0);

    server.update_runtime_options(OMNISMS_WEBUI_SOURCE_DIR, "next", "changed");
    response = request(port,
        "GET /config.json HTTP/1.1\r\nHost: localhost\r\n" + auth_header() + "\r\n");
    CHECK(response.find("HTTP/1.1 401 Unauthorized") == 0);
    response = request(port,
        "GET /config.json HTTP/1.1\r\nHost: localhost\r\n" +
        auth_header("next", "changed") + "\r\n");
    CHECK(response.find("HTTP/1.1 200 OK") == 0);

    server.stop();
    if (g_failures == 0) {
        printf("all Linux web server tests passed\n");
        return 0;
    }
    fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
}
