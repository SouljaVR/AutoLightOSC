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

    std::unique_ptr<UdpTransmitSocket> socket;
    std::chrono::steady_clock::time_point lastMessageTime;

    void Initialize();

public:
    OscManager(const std::string& ipAddress = "127.0.0.1", int port = 9000);
    ~OscManager();

    void SetOscRate(int rate);
    void SendColorValues(float r, float g, float b);
};