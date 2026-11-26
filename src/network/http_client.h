#pragma once

#include <string>

struct HttpResponse {
    int status_code = 0;
    std::string body;
    bool ok = false;
};

class HttpClient {
public:
    static void init();
    static void cleanup();

    static HttpResponse get(const std::string& host, int port, const std::string& path, int timeout_ms = 1000);
    static HttpResponse post(const std::string& host, int port, const std::string& path, const std::string& body, int timeout_ms = 1000);
};
