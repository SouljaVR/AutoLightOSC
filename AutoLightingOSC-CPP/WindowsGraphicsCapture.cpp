// WindowsGraphicsCapture.cpp
// Uses screen DC so that captureArea (in screen coords) is always up-to-date.

#define NOMINMAX
#include "WindowsGraphicsCapture.h"
#include <windows.h>
#include <d3d11.h>
#include <iostream>

// Helper: capture a region of the *screen* into our Bitmap struct
static Bitmap CaptureScreenRegion(const RECT& captureArea) {
    int width = captureArea.right - captureArea.left;
    int height = captureArea.bottom - captureArea.top;
    if (width <= 0 || height <= 0) {
        return Bitmap();
    }

    // Get the desktop DC (origin at 0,0 = top-left of primary monitor)
    HDC hdcScreen = GetDC(nullptr);
    if (!hdcScreen) {
        return Bitmap();
    }

    HDC hdcMemDC = CreateCompatibleDC(hdcScreen);
    if (!hdcMemDC) {
        ReleaseDC(nullptr, hdcScreen);
        return Bitmap();
    }

    // Create a bitmap compatible with the screen DC
    HBITMAP hbmCapture = CreateCompatibleBitmap(hdcScreen, width, height);
    if (!hbmCapture) {
        DeleteDC(hdcMemDC);
        ReleaseDC(nullptr, hdcScreen);
        return Bitmap();
    }

    HGDIOBJ oldBmp = SelectObject(hdcMemDC, hbmCapture);

    // Blit from the *screen* at (captureArea.left, captureArea.top)
    BitBlt(hdcMemDC, 0, 0, width, height,
        hdcScreen, captureArea.left, captureArea.top, SRCCOPY);

    // Fill our Bitmap
    Bitmap result(width, height);
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;   // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    GetDIBits(hdcMemDC, hbmCapture, 0, height,
        result.data.get(), &bmi, DIB_RGB_COLORS);

    // Clean up GDI objects
    SelectObject(hdcMemDC, oldBmp);
    DeleteObject(hbmCapture);
    DeleteDC(hdcMemDC);
    ReleaseDC(nullptr, hdcScreen);

    return result;
}

WindowsGraphicsCapture::WindowsGraphicsCapture()
    : device(nullptr)
    , context(nullptr)
    , stagingTexture(nullptr)
    , captureItem(nullptr)
    , captureSession(nullptr)
    , isInitialized(false)
    , captureWindow(nullptr)
    , lastFrameTexture(nullptr)
    , lastFrameTime(0)
{
}

WindowsGraphicsCapture::~WindowsGraphicsCapture() {
    Cleanup();
}

void WindowsGraphicsCapture::Cleanup() {
    StopCapture();
    if (stagingTexture) {
        stagingTexture->Release();
        stagingTexture = nullptr;
    }
    if (lastFrameTexture) {
        lastFrameTexture->Release();
        lastFrameTexture = nullptr;
    }
    isInitialized = false;
}

bool WindowsGraphicsCapture::SetDevice(ID3D11Device* d3dDevice, ID3D11DeviceContext* d3dContext) {
    device = d3dDevice;
    context = d3dContext;
    isInitialized = (device != nullptr && context != nullptr);
    return isInitialized;
}

bool WindowsGraphicsCapture::StartCaptureWindow(HWND hwnd) {
    if (!isInitialized) {
        return false;
    }
    // We no longer rely on window DC—just remember the HWND so we can
    // re-query the captureArea each frame.
    captureWindow = hwnd;
    return (captureWindow != nullptr);
}

void WindowsGraphicsCapture::StopCapture() {
    captureWindow = nullptr;
    if (lastFrameTexture) {
        lastFrameTexture->Release();
        lastFrameTexture = nullptr;
    }
}

Bitmap WindowsGraphicsCapture::Capture(const RECT& captureArea) {
    if (!isInitialized || !captureWindow) {
        return Bitmap();
    }

    // Grab the screen region
    Bitmap result = CaptureScreenRegion(captureArea);
    if (!result.IsValid()) {
        return Bitmap();
    }

    // If you need a D3D11 staging texture for preview, update it here:
    if (device && context) {
        int width = result.width;
        int height = result.height;

        // Recreate staging texture if size changed
        if (stagingTexture) {
            D3D11_TEXTURE2D_DESC desc;
            stagingTexture->GetDesc(&desc);
            if (desc.Width != width || desc.Height != height) {
                stagingTexture->Release();
                stagingTexture = nullptr;
            }
        }
        if (!stagingTexture) {
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = width;
            desc.Height = height;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_STAGING;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            if (FAILED(device->CreateTexture2D(&desc, nullptr, &stagingTexture))) {
                stagingTexture = nullptr;
            }
        }

        // Copy the pixels into the staging texture
        if (stagingTexture) {
            D3D11_MAPPED_SUBRESOURCE mapped;
            if (SUCCEEDED(context->Map(stagingTexture, 0, D3D11_MAP_WRITE, 0, &mapped))) {
                for (int y = 0; y < height; ++y) {
                    memcpy(
                        reinterpret_cast<BYTE*>(mapped.pData) + y * mapped.RowPitch,
                        result.data.get() + y * result.stride,
                        width * 4
                    );
                }
                context->Unmap(stagingTexture, 0);
            }
        }
    }

    return result;
}
