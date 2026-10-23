#include "NetcatService.h"
#include <Arduino.h>
#include <lwip/sockets.h>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

struct NetcatTaskParams
{
    std::string host;
    int verbosity;
    bool buffered;
    uint16_t port;
    NetcatService *service;
};

void NetcatService::startTask(const std::string &host, int verbosity, uint16_t port, bool lineBuffer)
{
    auto *params = new NetcatTaskParams{host, verbosity, lineBuffer, port, this};
    xTaskCreatePinnedToCore(connectTask, "NetcatConnect", 20000, params, 1, nullptr, 1);
    delay(500); // start task delay
}

void NetcatService::connectTask(void *pvParams)
{
    auto *params = static_cast<NetcatTaskParams *>(pvParams);
    params->service->connect(params->host, params->verbosity, params->port, params->buffered);
    delete params;
    vTaskDelete(nullptr);
}

bool NetcatService::connect(const std::string &host, int verbosity, uint16_t port, bool lineBuffer)
{

    buffered = lineBuffer;
    if (!openSocket(host, port))
        return false;
    setNonBlocking();
    connected = true;
    return true;
}

bool NetcatService::openSocket(const std::string &host, uint16_t port)
{
    sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock < 0)
        return false;

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &dest.sin_addr);

    if (::connect(sock, (sockaddr *)&dest, sizeof(dest)) != 0)
    {
        ::close(sock);
        sock = -1;
        return false;
    }
    return true;
}

void NetcatService::setNonBlocking()
{
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
}

bool NetcatService::isConnected() const
{
    return sock >= 0 && connected;
}

void NetcatService::writeChar(char c)
{
    if (!isConnected())
        return;

    // Write to the socket in a line-by-line or char-by-char
    if (buffered)
    {
        txBuf.push_back(c);
        if (c == '\n' || c == '\r')
        {
            if (c == '\r') 
                txBuf.push_back('\n');
            ::send(sock, txBuf.data(), txBuf.size(), 0);
            txBuf.clear();
        }
    }
    else
    {
        ::send(sock, &c, 1, 0);
    }
}

std::string NetcatService::readOutputNonBlocking()
{
    if (!isConnected())
        return "";

    char buf[256];
    int n = ::recv(sock, buf, sizeof(buf), 0);
    if (n <= 0)
        return ""; // EWOULDBLOCK or closed
    return std::string(buf, n);
}

void NetcatService::close()
{
    if (sock >= 0)
    {
        ::shutdown(sock, SHUT_RDWR);
        ::close(sock);
        sock = -1;
    }
    connected = false;
}