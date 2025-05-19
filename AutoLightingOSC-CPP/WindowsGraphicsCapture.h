// WindowsGraphicsCapture.h
#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <memory>
#include "ColorProcessor.h" // For Bitmap struct

// Forward declarations
struct IGraphicsCaptureItem;
struct IGraphicsCaptureSession;
struct ID3D11Texture2D;

class WindowsGraphicsCapture {
private:
    // D3D11 components
    ID3D11Device* device;
    ID3D11DeviceContext* context;
    ID3D11Texture2D* stagingTexture;

    // Capture components
    IGraphicsCaptureItem* captureItem;
    IGraphicsCaptureSession* captureSession;

    bool isInitialized;
    HWND captureWindow;

    // Last frame info
    ID3D11Texture2D* lastFrameTexture;
    DWORD lastFrameTime;

    void Initialize();
    void Cleanup();

public:
    WindowsGraphicsCapture();
    ~WindowsGraphicsCapture();

    bool SetDevice(ID3D11Device* d3dDevice, ID3D11DeviceContext* d3dContext);
    bool StartCaptureWindow(HWND hwnd);
    void StopCapture();
    Bitmap Capture(const RECT& captureArea);

    bool IsInitialized() const { return isInitialized; }
    HWND GetCaptureWindow() const { return captureWindow; }
};