// OscManager.h
#pragma once

#include <string>
#include <memory>
#include <chrono>
#include <osc/OscOutboundPacketStream.h>
#include <ip/UdpSocket.h>

class OscManager {
private:
    std::string ipAddress;
    int port;
    int oscRate;
    std::string rParameter;
    std::string gParameter;
    std::string bParameter;

    std::unique_ptr<UdpTransmitSocket> socket;
    std::chrono::steady_clock::time_point lastMessageTime;

    void Initialize();

public:
    OscManager(const std::string& ipAddress = "127.0.0.1", int port = 9000);
    ~OscManager();

    void SetOscRate(int rate);
    void SetOscPort(int port);
    void SetParameters(const std::string& r, const std::string& g, const std::string& b);
    void SendColorValues(float r, float g, float b);
};