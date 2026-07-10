#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>

namespace omnisms::linux_port {

struct WebResponse {
    int status = 200;
    std::string contentType = "application/json; charset=utf-8";
    std::string body;
    std::map<std::string, std::string> headers;
};

struct WebRequest {
    std::string method;
    std::string path;
    std::string query;
    std::map<std::string, std::string> headers;
    std::string body;
};

struct WebServerOptions {
    std::string listenAddress = "127.0.0.1";
    int port = 8080;  // 0 = 让系统分配，仅用于测试
    std::string assetRoot;
    std::string username;
    std::string password;
    std::string assetHash = "linux";
};

struct WebCallbacks {
    std::function<std::string()> configJson;
    std::function<std::string()> statusJson;
    std::function<std::string(const WebRequest&)> messagesJson;
    std::function<std::string(const WebRequest&)> exportJson;
    std::function<WebResponse(const WebRequest&)> dynamic;
};

// 无第三方服务器依赖的小型 HTTP/1.1 服务：托管原 webui/ 并将动态请求交给端口回调。
class LinuxWebServer {
public:
    LinuxWebServer(WebServerOptions options, WebCallbacks callbacks);
    ~LinuxWebServer();
    LinuxWebServer(const LinuxWebServer&) = delete;
    LinuxWebServer& operator=(const LinuxWebServer&) = delete;

    bool start(std::string* error = nullptr);
    void stop();
    int bound_port() const;
    // These options can be replaced without rebinding the listener. Address/port changes
    // remain restart-only and are intentionally not accepted here.
    void update_runtime_options(const std::string& asset_root,
                                const std::string& username,
                                const std::string& password);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace omnisms::linux_port
