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


// ScreenCapture.cpp

#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>
#include "ScreenCapture.h"
#include <iostream>

ScreenCapture::ScreenCapture()
    : factory(nullptr), adapter(nullptr), device(nullptr), context(nullptr),
    output(nullptr), output1(nullptr), duplication(nullptr),
    stagingTexture(nullptr), isInitialized(false) {
    Initialize();
}

ScreenCapture::~ScreenCapture() {
    Cleanup();
}

void ScreenCapture::Initialize() {
    try {
        // Create DXGI Factory
        HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory);
        if (FAILED(hr)) throw std::runtime_error("Failed to create DXGI Factory");

        // Get primary adapter
        hr = factory->EnumAdapters1(0, &adapter);
        if (FAILED(hr)) throw std::runtime_error("Failed to get adapter");

        // Create D3D11 device
        D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
        hr = D3D11CreateDevice(
            adapter,
            D3D_DRIVER_TYPE_UNKNOWN,
            nullptr,
            0,
            featureLevels,
            ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION,
            &device,
            nullptr,
            &context
        );
        if (FAILED(hr)) throw std::runtime_error("Failed to create D3D11 device");

        // Get primary output (monitor)
        hr = adapter->EnumOutputs(0, &output);
        if (FAILED(hr)) throw std::runtime_error("Failed to get output");

        // Query Output1 interface
        hr = output->QueryInterface(__uuidof(IDXGIOutput1), (void**)&output1);
        if (FAILED(hr)) throw std::runtime_error("Failed to get Output1 interface");

        // Create output duplication
        hr = output1->DuplicateOutput(device, &duplication);
        if (FAILED(hr)) throw std::runtime_error("Failed to create output duplication");

        // Get desktop bounds
        DXGI_OUTPUT_DESC outputDesc;
        output->GetDesc(&outputDesc);
        RECT desktopBounds = outputDesc.DesktopCoordinates;

        // Create staging texture
        D3D11_TEXTURE2D_DESC texDesc = {};
        texDesc.Width = desktopBounds.right - desktopBounds.left;
        texDesc.Height = desktopBounds.bottom - desktopBounds.top;
        texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        texDesc.ArraySize = 1;
        texDesc.BindFlags = 0;
        texDesc.MiscFlags = 0;
        texDesc.SampleDesc.Count = 1;
        texDesc.SampleDesc.Quality = 0;
        texDesc.MipLevels = 1;
        texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        texDesc.Usage = D3D11_USAGE_STAGING;

        hr = device->CreateTexture2D(&texDesc, nullptr, &stagingTexture);
        if (FAILED(hr)) throw std::runtime_error("Failed to create staging texture");

        isInitialized = true;
    }
    catch (const std::exception& e) {
        std::cerr << "Error initializing screen capture: " << e.what() << std::endl;
        Cleanup();
        isInitialized = false;
    }
}

void ScreenCapture::Cleanup() {
    if (duplication) { duplication->Release(); duplication = nullptr; }
    if (stagingTexture) { stagingTexture->Release(); stagingTexture = nullptr; }
    if (output1) { output1->Release(); output1 = nullptr; }
    if (output) { output->Release(); output = nullptr; }
    if (context) { context->Release(); context = nullptr; }
    if (device) { device->Release(); device = nullptr; }
    if (adapter) { adapter->Release(); adapter = nullptr; }
    if (factory) { factory->Release(); factory = nullptr; }
}

bool ScreenCapture::Reinitialize() {
    Cleanup();
    Initialize();
    return isInitialized;
}

Bitmap ScreenCapture::Capture(const RECT& captureArea) {
    if (!isInitialized) {
        if (!Reinitialize()) {
            return Bitmap();
        }
    }

    try {
        IDXGIResource* desktopResource = nullptr;
        DXGI_OUTDUPL_FRAME_INFO frameInfo;

        // Try to get duplicated frame within given time
        HRESULT hr = duplication->AcquireNextFrame(1000, &frameInfo, &desktopResource);

        if (FAILED(hr)) {
            // If failed, try to reinitialize
            if (Reinitialize()) {
                // Try once more after reinitializing
                hr = duplication->AcquireNextFrame(1000, &frameInfo, &desktopResource);
                if (FAILED(hr)) {
                    return Bitmap();
                }
            }
            else {
                return Bitmap();
            }
        }

        // Get texture from resource
        ID3D11Texture2D* desktopTexture = nullptr;
        hr = desktopResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&desktopTexture);
        desktopResource->Release();

        if (FAILED(hr)) {
            duplication->ReleaseFrame();
            return Bitmap();
        }

        // Copy to staging texture
        context->CopyResource(stagingTexture, desktopTexture);
        desktopTexture->Release();

        // Map the staging texture
        D3D11_MAPPED_SUBRESOURCE mapped;
        hr = context->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mapped);

        if (FAILED(hr)) {
            duplication->ReleaseFrame();
            return Bitmap();
        }

        // Get texture dimensions
        D3D11_TEXTURE2D_DESC desc;
        stagingTexture->GetDesc(&desc);

        // Calculate capture area
        RECT effectiveCaptureArea = captureArea;

        // Ensure capture area is within bounds
        effectiveCaptureArea.left = std::max(0L, effectiveCaptureArea.left);
        effectiveCaptureArea.top = std::max(0L, effectiveCaptureArea.top);
        effectiveCaptureArea.right = std::min(static_cast<LONG>(desc.Width), effectiveCaptureArea.right);
        effectiveCaptureArea.bottom = std::min(static_cast<LONG>(desc.Height), effectiveCaptureArea.bottom);

        // Create bitmap for the captured area
        int width = effectiveCaptureArea.right - effectiveCaptureArea.left;
        int height = effectiveCaptureArea.bottom - effectiveCaptureArea.top;

        if (width <= 0 || height <= 0) {
            context->Unmap(stagingTexture, 0);
            duplication->ReleaseFrame();
            return Bitmap();
        }

        Bitmap result(width, height);

        // Copy pixel data
        for (int y = 0; y < height; y++) {
            BYTE* srcRow = static_cast<BYTE*>(mapped.pData) +
                (effectiveCaptureArea.top + y) * mapped.RowPitch +
                effectiveCaptureArea.left * 4;
            BYTE* dstRow = result.data.get() + y * result.stride;

            memcpy(dstRow, srcRow, width * 4);
        }

        // Unmap the texture
        context->Unmap(stagingTexture, 0);
        duplication->ReleaseFrame();

        return result;
    }
    catch (const std::exception& e) {
        std::cerr << "Error in Capture method: " << e.what() << std::endl;
        isInitialized = false;
        return Bitmap();
    }
}