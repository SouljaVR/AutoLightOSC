// ScreenCapture.h
#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <memory>
#include "ColorProcessor.h" // For Bitmap struct

class ScreenCapture {
private:
    // DXGI components
    IDXGIFactory1* factory;
    IDXGIAdapter1* adapter;
    ID3D11Device* device;
    ID3D11DeviceContext* context;
    IDXGIOutput* output;
    IDXGIOutput1* output1;
    IDXGIOutputDuplication* duplication;

    // Textures and resources
    ID3D11Texture2D* stagingTexture;

    bool isInitialized;

    void Initialize();
    void Cleanup();

public:
    ScreenCapture();
    ~ScreenCapture();

    bool Reinitialize();
    Bitmap Capture(const RECT& captureArea);

    bool IsInitialized() const { return isInitialized; }
};