// Copyright (c) 2025 BigSoulja/SouljaVR
// Developed and maintained by BigSoulja/SouljaVR and all direct or indirect contributors to the GitHub repository.
// See LICENSE.txt for full copyright and licensing details (GNU General Public License v3.0).
// 
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// This project is open source, but continued development and maintenance benefit from your support.
// Businesses and collaborators: support via funding, sponsoring, or integration opportunities is welcome.
// For inquiries or support, please reach out at: Discord: @bigsoulja

// SpoutReceiver.cpp

#define NOMINMAX
#include "SpoutReceiver.h"
#include <iostream>

SpoutReceiver::SpoutReceiver()
    : isInitialized(false), isConnected(false), pixelBuffer(nullptr),
    width(0), height(0), device(nullptr), context(nullptr),
    lastFrameCheck(0), lastFrameUpdate(GetTickCount64()), isActive(true) {
    memset(senderName, 0, 256);
}

SpoutReceiver::~SpoutReceiver() {
    Disconnect();
}

bool SpoutReceiver::Init(ID3D11Device* d3dDevice, ID3D11DeviceContext* d3dContext) {
    if (isInitialized) {
        return true;
    }

    try {
        // Store device and context references
        device = d3dDevice;
        context = d3dContext;

        // Initialize Spout receiver with DirectX instead of OpenGL
        if (!spout.OpenDirectX11(d3dDevice)) {
            throw std::runtime_error("Failed to initialize Spout DirectX");
        }

        isInitialized = true;
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Error initializing Spout receiver: " << e.what() << std::endl;
        isInitialized = false;
        return false;
    }
}

bool SpoutReceiver::Connect() {
    if (!isInitialized) {
        return false;
    }

    // If already connected, consider it a success
    if (isConnected) {
        return true;
    }

    // Try to find an active sender
    if (spout.GetActiveSender(senderName)) {
        // Get sender dimensions
        width = spout.GetSenderWidth();
        height = spout.GetSenderHeight();

        std::cerr << "Connected to sender: " << senderName << " ("
            << width << "x" << height << ")" << std::endl;

        // Activate the sender.
        if (!spout.SetActiveSender(senderName)) {
            std::cerr << "Failed to set active sender: " << senderName << std::endl;
            return false;
        }

        // Allocate pixel buffer
        if (pixelBuffer) {
            delete[] pixelBuffer;
        }
        unsigned int buffersize = width * height * 4;
        pixelBuffer = new unsigned char[buffersize];

        // Small delay to let the connection settle
        Sleep(100);

        isConnected = true;
        return true;
    }

    return false; // No sender found
}

Bitmap SpoutReceiver::Receive() {
    if (!isInitialized) {
        std::cerr << "Spout not initialized in Receive()" << std::endl;
        return Bitmap();
    }

    // Ensure connection
    if (!isConnected && !Connect()) {
        return Bitmap();
    }

    // Call ReceiveTexture first - this updates internal state even though it returns null
    ID3D11Texture2D* receivedTexture = nullptr;
    spout.ReceiveTexture(&receivedTexture);

    // Update sender dimensions if they have changed
    unsigned int newWidth = spout.GetSenderWidth();
    unsigned int newHeight = spout.GetSenderHeight();
    if (newWidth != width || newHeight != height) {
        width = newWidth;
        height = newHeight;
        if (pixelBuffer) {
            delete[] pixelBuffer;
        }
        unsigned int buffersize = width * height * 4;
        pixelBuffer = new unsigned char[buffersize];
    }

    // Get the shared handle
    HANDLE sharedHandle = spout.GetSenderHandle();
    if (!sharedHandle) {
        std::cerr << "Shared handle is null" << std::endl;
        return Bitmap();
    }

    // Open the shared resource
    if (receivedTexture) {
        receivedTexture->Release();
        receivedTexture = nullptr;
    }

    HRESULT hr = device->OpenSharedResource(sharedHandle, __uuidof(ID3D11Texture2D), (void**)&receivedTexture);
    if (FAILED(hr)) {
        std::cerr << "Failed to open shared resource, hr=" << hr << std::endl;
        return Bitmap();
    }

    // Create a staging texture for CPU access
    ID3D11Texture2D* stagingTexture = nullptr;
    D3D11_TEXTURE2D_DESC desc;
    receivedTexture->GetDesc(&desc);

    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = device->CreateTexture2D(&desc, nullptr, &stagingTexture);
    if (FAILED(hr)) {
        receivedTexture->Release();
        std::cerr << "Failed to create staging texture, hr=" << hr << std::endl;
        return Bitmap();
    }

    // Copy the received texture to the staging texture
    context->CopyResource(stagingTexture, receivedTexture);
    receivedTexture->Release();

    // Map the staging texture to get data
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    hr = context->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mappedResource);
    if (SUCCEEDED(hr)) {
        Bitmap result(width, height);

        // Copy row by row to account for potential stride differences
        for (unsigned int y = 0; y < height; y++) {
            memcpy(result.data.get() + y * result.stride,
                static_cast<unsigned char*>(mappedResource.pData) + y * mappedResource.RowPitch,
                width * 4);
        }

        context->Unmap(stagingTexture, 0);
        stagingTexture->Release();

        // Mark that we received a valid frame
        lastFrameUpdate = GetTickCount64();

        return result;
    }
    else {
        stagingTexture->Release();
        std::cerr << "Failed to map staging texture, hr=" << hr << std::endl;
    }

    return Bitmap();
}

bool SpoutReceiver::IsSenderActive() {
    if (!isConnected) {
        return false;
    }

    // Limit checks to reduce overhead (check every 500ms)
    uint64_t currentTime = GetTickCount64();
    if (currentTime - lastFrameCheck < 500) {
        // If we've gone too long without frame updates, consider inactive
        return isActive && (currentTime - lastFrameUpdate < 3000);
    }

    lastFrameCheck = currentTime;

    // Verify the sender still exists and can be connected to
    unsigned int w, h;
    HANDLE handle = NULL;
    DWORD format = 0;

    // Try to get sender info - this will fail if sender is gone
    if (!spout.GetSenderInfo(senderName, w, h, handle, format)) {
        isActive = false;
        return false;
    }

    // If significant change in size, consider it a new sender
    if (w > 0 && h > 0 && (w != width || h != height)) {
        width = w;
        height = h;
        lastFrameUpdate = currentTime;
        isActive = true;
        return true;
    }

    // Update timestamp on successful Receive in the Receive method
    // Consider inactive if no updates for 3 seconds
    isActive = (currentTime - lastFrameUpdate < 3000);
    return isActive;
}

void SpoutReceiver::Disconnect() {
    // Clean up pixel buffer
    if (pixelBuffer) {
        delete[] pixelBuffer;
        pixelBuffer = nullptr;
    }

    // Release the receiver
    if (isInitialized) {
        spout.ReleaseReceiver();
        spout.CloseDirectX11();
    }

    isConnected = false;
    width = 0;
    height = 0;
    memset(senderName, 0, 256);
}
