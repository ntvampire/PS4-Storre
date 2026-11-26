#include "http_client.h"
#include <iostream>
#include <sstream>
#include <cstring>

#if defined(_WIN32)
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef int socklen_t;
#else
    #include <sys/time.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <fcntl.h>
    #define closesocket close
    typedef int SOCKET;
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

void HttpClient::init() {
#if defined(_WIN32)
    static bool initialized = false;
    if (!initialized) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        initialized = true;
    }
#endif
}

void HttpClient::cleanup() {
#if defined(_WIN32)
    WSACleanup();
#endif
}

static HttpResponse send_http(const std::string& method, const std::string& host, int port, const std::string& path, const std::string& body, int timeout_ms) {
    HttpClient::init();
    HttpResponse res;

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        return res;
    }

#if defined(_WIN32)
    DWORD tv = timeout_ms;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
#else
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        closesocket(sock);
        return res;
    }

    std::ostringstream req;
    req << method << " " << path << " HTTP/1.1\r\n";
    req << "Host: " << host << ":" << port << "\r\n";
    req << "Connection: close\r\n";
    req << "User-Agent: PS4Storre/1.0\r\n";
    if (!body.empty()) {
        req << "Content-Type: application/json\r\n";
        req << "Content-Length: " << body.length() << "\r\n\r\n";
        req << body;
    } else {
        req << "\r\n";
    }

    std::string req_str = req.str();
    if (send(sock, req_str.c_str(), (int)req_str.length(), 0) == SOCKET_ERROR) {
        closesocket(sock);
        return res;
    }

    std::string raw_resp;
    char buffer[4096];
    int bytes_read = 0;
    while ((bytes_read = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes_read] = '\0';
        raw_resp.append(buffer, bytes_read);
    }
    closesocket(sock);

    if (raw_resp.empty()) {
        return res;
    }

    // Parse status line
    size_t line_end = raw_resp.find("\r\n");
    if (line_end != std::string::npos) {
        std::string status_line = raw_resp.substr(0, line_end);
        size_t code_start = status_line.find(" ");
        if (code_start != std::string::npos) {
            res.status_code = std::atoi(status_line.substr(code_start + 1, 3).c_str());
        }
    }

    // Parse body
    size_t header_end = raw_resp.find("\r\n\r\n");
    if (header_end != std::string::npos) {
        res.body = raw_resp.substr(header_end + 4);
    }

    res.ok = (res.status_code >= 200 && res.status_code < 400);
    return res;
}

HttpResponse HttpClient::get(const std::string& host, int port, const std::string& path, int timeout_ms) {
    return send_http("GET", host, port, path, "", timeout_ms);
}

HttpResponse HttpClient::post(const std::string& host, int port, const std::string& path, const std::string& body, int timeout_ms) {
    return send_http("POST", host, port, path, body, timeout_ms);
}
