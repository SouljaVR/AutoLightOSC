// SpoutReceiver.h
#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <memory>
#include <string>
#include "ColorProcessor.h" // For Bitmap struct
#include "SpoutDX.h"          // Spout2 SDK header

class SpoutReceiver {
private:
    bool isInitialized;
    bool isConnected;

    // Spout receiver object
    spoutDX spout;

    // Pixel buffer for receiving
    unsigned char* pixelBuffer;

    // Sender information
    unsigned int width;
    unsigned int height;
    char senderName[256];

    // D3D11 device (for reference only)
    ID3D11Device* device;
    ID3D11DeviceContext* context;

    uint64_t lastFrameCheck;
    uint64_t lastFrameUpdate;
    bool isActive;

public:
    SpoutReceiver();
    ~SpoutReceiver();

    // Initialize the Spout receiver
    bool Init(ID3D11Device* device, ID3D11DeviceContext* context);

    // Check if initialized
    bool IsInitialized() const { return isInitialized; }

    // Check if connected to a sender
    bool IsConnected() const { return isConnected; }

    // Try to connect to any available Spout sender
    bool Connect();

    // Receive frame from sender
    Bitmap Receive();

    // Get sender name
    std::string GetSenderName() const { return senderName; }

    bool IsSenderActive();

    // Disconnect from sender
    void Disconnect();
};