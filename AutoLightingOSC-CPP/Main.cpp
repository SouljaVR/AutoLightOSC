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

// Main.cpp

#define NOMINMAX

#include <windows.h>
#include <d3d11.h>
#include <tchar.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <string>
#include <memory>
#include <chrono>
#include <algorithm>
#include <shellapi.h>

#include "Resource.h"
#include "UserSettings.h"
#include "WindowManager.h"
#include "ScreenCapture.h"
#include "ColorProcessor.h"
#include "OscManager.h"
#include "SpoutReceiver.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// Data
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

// Forward declarations
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
bool LoadTextureFromFile(const char* filename, ID3D11ShaderResourceView** out_srv, int& out_width, int& out_height);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Application state
struct AppState {
    UserSettings settings;
    std::unique_ptr<WindowManager> windowManager;
    std::unique_ptr<ScreenCapture> screenCapture;
    std::unique_ptr<ColorProcessor> colorProcessor;
    std::unique_ptr<OscManager> oscManager;
    std::unique_ptr<SpoutReceiver> spoutReceiver;

    HWND targetWindowHandle = nullptr;
    RECT captureArea = { 0, 0, 0, 0 };
    RECT userCropArea = { 0, 0, 0, 0 };
    bool isCapturing = false;
    bool isDebugViewExpanded = false;
    bool isSelecting = false;
    bool isActivelySelecting = false;
    ImVec2 startPoint = { 0, 0 };

    ColorRGB currentColor = { 0, 0, 0 };
    ColorRGB targetColor = { 0, 0, 0 };
    std::chrono::steady_clock::time_point lastCaptureTime;
    std::chrono::steady_clock::time_point lastSmoothingTime;

    // Window list for the combobox
    std::vector<WindowInfo> windowList;
    int selectedWindowIdx = -1;

    // Capture and OSC timer variables
    std::chrono::steady_clock::time_point lastFrameTime;
    std::chrono::milliseconds captureInterval{ 200 }; // Default 5 FPS (1000/5)
    std::chrono::milliseconds oscInterval{ 200 };     // Default 5 Hz
    const std::chrono::microseconds smoothingInterval{ 16667 }; // ~60fps (1000000/60)

    // Texture for preview
    ID3D11ShaderResourceView* previewTexture = nullptr;
    Bitmap lastCapturedImage;

    // About window
    bool showAboutWindow = false;
    ID3D11ShaderResourceView* logoTexture = nullptr;

    // Add version information
    const char* appVersion = "1.0.5";
    const char* gitHubUrl = "https://github.com/SouljaVR/AutoLightingOSC";
    const char* websiteUrl = "https://www.soulja.io";

    AppState() {
        windowManager = std::make_unique<WindowManager>();
        screenCapture = std::make_unique<ScreenCapture>();
        colorProcessor = std::make_unique<ColorProcessor>(settings);
        oscManager = std::make_unique<OscManager>("127.0.0.1", 9000);
        spoutReceiver = std::make_unique<SpoutReceiver>();

        settings = UserSettings::Load();

        // Set intervals based on settings
        captureInterval = std::chrono::milliseconds(1000 / settings.captureFps);
        oscInterval = std::chrono::milliseconds(1000 / settings.oscRate);

        lastFrameTime = std::chrono::steady_clock::now();
        lastCaptureTime = std::chrono::steady_clock::now();
        lastSmoothingTime = std::chrono::steady_clock::now();
    }

    ~AppState() {
        if (previewTexture) {
            previewTexture->Release();
            previewTexture = nullptr;
        }

        if (spoutReceiver && spoutReceiver->IsConnected()) {
            spoutReceiver->Disconnect();
        }

        if (logoTexture) {
            logoTexture->Release();
            logoTexture = nullptr;
        }
    }

    void FindTargetWindow() {
        targetWindowHandle = windowManager->FindVRChatWindow();
        if (targetWindowHandle) {
            captureArea = windowManager->GetOptimalCaptureArea(targetWindowHandle);
            // Update selected window in the dropdown when refreshed
            RefreshApplicationList();
            for (size_t i = 0; i < windowList.size(); i++) {
                if (windowList[i].handle == targetWindowHandle) {
                    selectedWindowIdx = static_cast<int>(i);
                    break;
                }
            }
        }
    }

    void RefreshApplicationList() {
        windowList = windowManager->GetOpenWindows();
        // Try to keep the same selection if possible
        if (selectedWindowIdx >= 0 && selectedWindowIdx < windowList.size()) {
            HWND oldHandle = windowList[selectedWindowIdx].handle;
            selectedWindowIdx = -1;
            for (size_t i = 0; i < windowList.size(); i++) {
                if (windowList[i].handle == oldHandle) {
                    selectedWindowIdx = static_cast<int>(i);
                    break;
                }
            }
        }

        // Auto-select VRChat if available
        if (selectedWindowIdx == -1) {
            for (size_t i = 0; i < windowList.size(); i++) {
                if (windowList[i].processName == "VRChat") {
                    selectedWindowIdx = static_cast<int>(i);
                    targetWindowHandle = windowList[i].handle;
                    captureArea = windowManager->GetOptimalCaptureArea(targetWindowHandle);
                    break;
                }
            }
        }
    }

    void StartCapture() {
        // Set up capture interval based on FPS
        captureInterval = std::chrono::milliseconds(1000 / settings.captureFps);

        // Set up OSC rate
        oscManager->SetOscRate(settings.oscRate);
        oscInterval = std::chrono::milliseconds(1000 / settings.oscRate);

        // If using Spout, try to connect to a sender
        if (settings.enableSpout) {
            if (spoutReceiver->Connect()) {
                // Successfully connected to a Spout sender
                isCapturing = true;
            }
            else {
                // Failed to connect to any Spout sender
                MessageBoxA(nullptr, "Could not connect to any Spout sender. Please ensure a Spout sender is running.",
                    "Error", MB_OK | MB_ICONERROR);
                return;
            }
        }
        else {
            // Using normal screen capture
            if (!targetWindowHandle) {
                FindTargetWindow();
                if (!targetWindowHandle) {
                    // Show error message
                    MessageBoxA(nullptr, "Could not find VRChat window. Please make sure VRChat is running.",
                        "Error", MB_OK | MB_ICONERROR);
                    return;
                }
            }

            // Start capture
            if (settings.keepTargetWindowOnTop) {
                windowManager->SetWindowOnTop(targetWindowHandle);
            }
            isCapturing = true;
        }

        lastFrameTime = std::chrono::steady_clock::now();
        lastSmoothingTime = std::chrono::steady_clock::now();
    }

    void StopCapture() {
        // If using Spout, disconnect from sender
        if (settings.enableSpout) {
            spoutReceiver->Disconnect();
        }
        else if (targetWindowHandle) {
            // Normal screen capture - restore window state
            windowManager->SetWindowNotTopMost(targetWindowHandle);
        }

        isCapturing = false;
    }

    void PerformCapture() {
        Bitmap capturedBitmap;

        if (settings.enableSpout) {
            // Check if the sender is still actively sending frames
            if (!spoutReceiver->IsSenderActive()) {
                // std::cerr << "Spout sender inactive, reconnecting..." << std::endl;
                spoutReceiver->Disconnect();
                Sleep(1000); // Brief delay

                if (!spoutReceiver->Connect()) {
                    // std::cerr << "Failed to reconnect to Spout sender, retrying..." << std::endl;
                    return;
                }
            }

            // Get frame from Spout
            capturedBitmap = spoutReceiver->Receive();
            if (!capturedBitmap.IsValid()) {
                static int failCount = 0;
                failCount++;

                // Only disconnect after multiple consecutive failures
                if (failCount > 10 && spoutReceiver->IsConnected()) {
                    spoutReceiver->Disconnect();
                    MessageBoxA(nullptr, "Spout sender disconnected after multiple failures.",
                        "Warning", MB_OK | MB_ICONWARNING);
                    StopCapture();
                    failCount = 0;
                }
                return;
            }
        }
        else {
            // Use normal screen capture
            if (!windowManager->IsWindowValid(targetWindowHandle)) {
                return;
            }

            // Update the capture area each time to follow the window
            captureArea = windowManager->GetOptimalCaptureArea(targetWindowHandle);

            // Get the full capture area
            RECT fullCaptureArea = captureArea;

            // Capture the screen area
            capturedBitmap = screenCapture->Capture(fullCaptureArea);
            if (!capturedBitmap.IsValid()) {
                return;
            }
        }

        // Save the last captured image for preview
        lastCapturedImage = capturedBitmap;

        // Create or update preview texture
        if (isDebugViewExpanded) {
            UpdatePreviewTexture(capturedBitmap);
        }

        // Determine if we should use a crop for color processing
        bool useCrop = !isActivelySelecting &&
            userCropArea.right > userCropArea.left &&
            userCropArea.bottom > userCropArea.top &&
            isDebugViewExpanded;

        // Process image to get average color
        Bitmap processingBitmap;

        if (useCrop) {
            // Only extract the cropped portion for color processing
            // instead of capturing again
            RECT validCrop = userCropArea;
            validCrop.left = std::max(0L, validCrop.left);
            validCrop.top = std::max(0L, validCrop.top);
            validCrop.right = std::min((LONG)lastCapturedImage.width, validCrop.right);
            validCrop.bottom = std::min((LONG)lastCapturedImage.height, validCrop.bottom);

            int cropWidth = validCrop.right - validCrop.left;
            int cropHeight = validCrop.bottom - validCrop.top;

            if (cropWidth > 0 && cropHeight > 0) {
                // Create a new bitmap with just the cropped area
                processingBitmap = Bitmap(cropWidth, cropHeight);

                // Copy just the cropped region from the full image
                for (int y = 0; y < cropHeight; y++) {
                    for (int x = 0; x < cropWidth; x++) {
                        int srcOffset = (validCrop.top + y) * lastCapturedImage.stride + (validCrop.left + x) * 4;
                        int dstOffset = y * processingBitmap.stride + x * 4;

                        // Copy the pixel data (BGRA format)
                        processingBitmap.data.get()[dstOffset + 0] = lastCapturedImage.data.get()[srcOffset + 0]; // B
                        processingBitmap.data.get()[dstOffset + 1] = lastCapturedImage.data.get()[srcOffset + 1]; // G
                        processingBitmap.data.get()[dstOffset + 2] = lastCapturedImage.data.get()[srcOffset + 2]; // R
                        processingBitmap.data.get()[dstOffset + 3] = lastCapturedImage.data.get()[srcOffset + 3]; // A
                    }
                }
            }
        }

        // If we didn't create a valid crop bitmap, use the full image
        if (!processingBitmap.IsValid()) {
            processingBitmap = lastCapturedImage;
        }

        auto downscaledBitmap = colorProcessor->DownscaleForProcessing(processingBitmap);
        auto avgColor = colorProcessor->GetAverageColor(downscaledBitmap);

        // Store the target color (before smoothing)
        targetColor = colorProcessor->ProcessColor(avgColor);
    }

    RECT ScaleUserCropToActualWindow() {
        if (!lastCapturedImage.IsValid()) {
            return captureArea; // Return full area if no image available
        }

        int captureWidth = captureArea.right - captureArea.left;
        int captureHeight = captureArea.bottom - captureArea.top;

        // Make sure crop area doesn't exceed the bounds of the preview image
        RECT validCrop = userCropArea;
        validCrop.left = std::max(0L, validCrop.left);
        validCrop.top = std::max(0L, validCrop.top);
        validCrop.right = std::min((LONG)lastCapturedImage.width, validCrop.right);
        validCrop.bottom = std::min((LONG)lastCapturedImage.height, validCrop.bottom);

        // Calculate scale factors between preview and actual capture
        float scaleX = (float)captureWidth / lastCapturedImage.width;
        float scaleY = (float)captureHeight / lastCapturedImage.height;

        // Calculate the crop area in actual window coordinates
        RECT scaledRect;
        scaledRect.left = captureArea.left + (LONG)(validCrop.left * scaleX);
        scaledRect.top = captureArea.top + (LONG)(validCrop.top * scaleY);
        scaledRect.right = captureArea.left + (LONG)(validCrop.right * scaleX);
        scaledRect.bottom = captureArea.top + (LONG)(validCrop.bottom * scaleY);

        return scaledRect;
    }

    void UpdatePreviewTexture(const Bitmap& bitmap) {
        // If texture exists but size is wrong, recreate it
        if (previewTexture) {
            D3D11_TEXTURE2D_DESC desc;
            ID3D11Texture2D* texture = nullptr;
            previewTexture->GetResource(reinterpret_cast<ID3D11Resource**>(&texture));
            if (texture) {
                texture->GetDesc(&desc);
                texture->Release();

                if (desc.Width != bitmap.width || desc.Height != bitmap.height) {
                    previewTexture->Release();
                    previewTexture = nullptr;
                }
            }
        }

        // Create new texture if needed
        if (!previewTexture) {
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = bitmap.width;
            desc.Height = bitmap.height;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

            ID3D11Texture2D* texture = nullptr;
            g_pd3dDevice->CreateTexture2D(&desc, nullptr, &texture);

            if (texture) {
                D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                srvDesc.Format = desc.Format;
                srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Texture2D.MipLevels = 1;
                g_pd3dDevice->CreateShaderResourceView(texture, &srvDesc, &previewTexture);
                texture->Release();
            }
        }

        // Update texture data
        if (previewTexture) {
            ID3D11Texture2D* texture = nullptr;
            previewTexture->GetResource(reinterpret_cast<ID3D11Resource**>(&texture));
            if (texture) {
                D3D11_MAPPED_SUBRESOURCE mapped;
                if (SUCCEEDED(g_pd3dDeviceContext->Map(texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                    // Copy pixel data
                    for (UINT y = 0; y < bitmap.height; y++) {
                        for (UINT x = 0; x < bitmap.width; x++) {
                            int srcOffset = y * bitmap.stride + x * 4;
                            int destOffset = y * mapped.RowPitch + x * 4;

                            if (settings.enableSpout) {
                                // Direct copy without channel swap
                                ((BYTE*)mapped.pData)[destOffset + 0] = bitmap.data.get()[srcOffset + 0]; // B
                                ((BYTE*)mapped.pData)[destOffset + 1] = bitmap.data.get()[srcOffset + 1]; // G
                                ((BYTE*)mapped.pData)[destOffset + 2] = bitmap.data.get()[srcOffset + 2]; // R
                                ((BYTE*)mapped.pData)[destOffset + 3] = bitmap.data.get()[srcOffset + 3]; // A
                            } else {
                                // Swap R and B channels (0=B, 1=G, 2=R, 3=A)
                                ((BYTE*)mapped.pData)[destOffset + 0] = bitmap.data.get()[srcOffset + 2]; // B <- R
                                ((BYTE*)mapped.pData)[destOffset + 1] = bitmap.data.get()[srcOffset + 1]; // G <- G
                                ((BYTE*)mapped.pData)[destOffset + 2] = bitmap.data.get()[srcOffset + 0]; // R <- B
                                ((BYTE*)mapped.pData)[destOffset + 3] = bitmap.data.get()[srcOffset + 3]; // A <- A
                            }
                        }
                    }

                    g_pd3dDeviceContext->Unmap(texture, 0);
                }
                texture->Release();
            }
        }
    }

    void UpdateSmoothing(float deltaTime) {
        if (!isCapturing) return;

        if (settings.enableSmoothing) {
            currentColor = colorProcessor->GetSmoothedColor(deltaTime, targetColor);
        }
        else {
            currentColor = targetColor;
        }
    }

    void ProcessOscOutput() {
        if (!isCapturing) return;

        // Send OSC message with current color (smoothed or direct)
        oscManager->SendColorValues(currentColor.r, currentColor.g, currentColor.b);
    }

    void SaveSettings() {
        settings.Save();
    }

    bool IsVRChatSelected() const {
        // Check if the selected window in the dropdown is VRChat
        if (selectedWindowIdx >= 0 && selectedWindowIdx < windowList.size()) {
            return windowList[selectedWindowIdx].processName == "VRChat";
        }

        // Or check if the actual target window is VRChat, even if not selected in the list
        if (targetWindowHandle) {
            for (const auto& window : windowList) {
                if (window.handle == targetWindowHandle && window.processName == "VRChat") {
                    return true;
                }
            }
        }

        return false;
    }
};

// Main code
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
{
    // Enable console window for debug logs
    AllocConsole();
    FILE* fp;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONOUT$", "w", stderr);
    freopen_s(&fp, "CONIN$", "r", stdin);
    std::cout.clear();
    std::cerr.clear();
    std::cin.clear();
    std::wcout.clear();
    std::wcerr.clear();
    std::wcin.clear();

    // Create application window
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr),
        LoadIcon(wc.hInstance, MAKEINTRESOURCE(IDI_AUTOLIGHTINGOSCCPP)),
        LoadCursor(nullptr, IDC_ARROW),
        nullptr, nullptr,
        _T("AutoLightOSC"),
        LoadIcon(wc.hInstance, MAKEINTRESOURCE(IDI_SMALL)) };
    ::RegisterClassEx(&wc);
    HWND hwnd = ::CreateWindow(wc.lpszClassName, _T("AutoLightOSC"), WS_OVERLAPPEDWINDOW & ~WS_SIZEBOX, 100, 100, 483, 327, nullptr, nullptr, wc.hInstance, nullptr);

    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClass(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Enable dark mode for title bar (Windows 11)
    typedef BOOL(WINAPI* DwmSetWindowAttribute_t)(HWND, DWORD, LPCVOID, DWORD);
    HMODULE hDwmApi = LoadLibraryA("dwmapi.dll");
    if (hDwmApi) {
        DwmSetWindowAttribute_t dwmSetWindowAttribute = (DwmSetWindowAttribute_t)GetProcAddress(hDwmApi, "DwmSetWindowAttribute");

        if (dwmSetWindowAttribute) {
            // Use the DWMWA_USE_IMMERSIVE_DARK_MODE attribute (value 20)
            const DWORD DWMWA_USE_IMMERSIVE_DARK_MODE = 20;
            BOOL darkMode = TRUE;
            dwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));
        }

        FreeLibrary(hDwmApi);
    }

    // Set application icon
    SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)LoadIcon(wc.hInstance, MAKEINTRESOURCE(IDI_AUTOLIGHTINGOSCCPP)));
    SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)LoadIcon(wc.hInstance, MAKEINTRESOURCE(IDI_SMALL)));

    // Show the window
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();  // Dark theme as requested

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // Create application state
    auto appState = std::make_unique<AppState>();

    if (!appState->spoutReceiver->Init(g_pd3dDevice, g_pd3dDeviceContext)) {
        std::cerr << "Warning: Failed to initialize Spout receiver" << std::endl;
    }

    // Find VRChat window initially
    appState->FindTargetWindow();

    int logoWidth, logoHeight;
    if (LoadTextureFromFile("resources/logo.png", &appState->logoTexture, logoWidth, logoHeight)) {
        std::cout << "Logo loaded successfully" << std::endl;
    }
    else {
        std::cerr << "Failed to load logo" << std::endl;
    }

    // Set window size based on debug view
    RECT windowRect;
    GetWindowRect(hwnd, &windowRect);
    int width = appState->settings.showDebugView ? 570 : 570;
    int height = appState->settings.showDebugView ? 900 : 400;

    // Update the window size
    MoveWindow(hwnd, windowRect.left, windowRect.top, width, height, TRUE);

    // Set the appState to the userData of the window
    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(appState.get()));

    // Application color variables
    ImVec4 clear_color = ImVec4(0.1f, 0.1f, 0.1f, 1.00f);

    // Refresh application list at initialization
    appState->RefreshApplicationList();

    // Main loop
    bool done = false;
    while (!done)
    {
        // Poll and handle messages (inputs, window resize, etc.)
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        // Window check interval (check every 2 seconds)
        static auto lastWindowCheckTime = std::chrono::steady_clock::now();
        auto currentTime = std::chrono::steady_clock::now();
        auto windowCheckDuration = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastWindowCheckTime);

        if (windowCheckDuration.count() >= 2000) { // 2 seconds
            if (appState->isCapturing) {
                // Check if the window is still valid
                if (!appState->windowManager->IsWindowValid(appState->targetWindowHandle)) {
                    // Try to find VRChat again
                    HWND newHandle = appState->windowManager->FindVRChatWindow();
                    if (newHandle) {
                        // Found VRChat again
                        appState->targetWindowHandle = newHandle;
                        appState->captureArea = appState->windowManager->GetOptimalCaptureArea(newHandle);
                        if (appState->settings.keepTargetWindowOnTop) {
                            appState->windowManager->SetWindowOnTop(newHandle);
                        }
                        appState->RefreshApplicationList();

                        // Find VRChat in the window list and update the selected index
                        for (size_t i = 0; i < appState->windowList.size(); i++) {
                            if (appState->windowList[i].handle == newHandle) {
                                appState->selectedWindowIdx = static_cast<int>(i);
                                break;
                            }
                        }
                    }
                    else {
                        // Still can't find VRChat, stop capture
                        appState->StopCapture();
                        appState->targetWindowHandle = nullptr;
                    }
                }
                else {
                    // Refresh the topmost state periodically
                    if (appState->settings.keepTargetWindowOnTop) {
                        appState->windowManager->SetWindowOnTop(appState->targetWindowHandle);
                    }
                }
            }
            else {
                // Not capturing, check if our existing handle is still valid
                if (appState->targetWindowHandle && !appState->windowManager->IsWindowValid(appState->targetWindowHandle)) {
                    appState->targetWindowHandle = nullptr;
                }
                else if (!appState->targetWindowHandle) {
                    // No window detected, check for VRChat
                    HWND newHandle = appState->windowManager->FindVRChatWindow();
                    if (newHandle) {
                        appState->targetWindowHandle = newHandle;
                        appState->captureArea = appState->windowManager->GetOptimalCaptureArea(newHandle);
                        // Refresh the window list to make sure VRChat is included
                        appState->RefreshApplicationList();

                        // Find VRChat in the window list and update the selected index
                        for (size_t i = 0; i < appState->windowList.size(); i++) {
                            if (appState->windowList[i].handle == newHandle) {
                                appState->selectedWindowIdx = static_cast<int>(i);
                                break;
                            }
                        }
                    }
                }
            }

            lastWindowCheckTime = currentTime;
        }

        // Capture timer
        if (appState->isCapturing) {
            auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - appState->lastFrameTime);

            if (elapsedTime >= appState->captureInterval) {
                appState->PerformCapture();
                appState->lastFrameTime = currentTime;
            }

            // Smoothing timer - runs at 60Hz independently
            auto smoothingElapsed = std::chrono::duration_cast<std::chrono::microseconds>(currentTime - appState->lastSmoothingTime);
            if (smoothingElapsed >= appState->smoothingInterval) {
                float smoothingDeltaTime = std::chrono::duration<float>(smoothingElapsed).count();
                appState->UpdateSmoothing(smoothingDeltaTime);
                appState->lastSmoothingTime = currentTime;
            }

            // OSC Output timer
            static auto lastOscTime = std::chrono::steady_clock::now();
            auto oscElapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastOscTime);

            if (oscElapsedTime >= appState->oscInterval) {
                appState->ProcessOscOutput();
                lastOscTime = currentTime;
            }
        }

        // Start the Dear ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Main application window
        {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(static_cast<float>(width), static_cast<float>(height)));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 20));

            ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoTitleBar;

            ImGui::Begin("AutoLightOSC", nullptr, window_flags);

            // Start/Stop Capture Button
            if (ImGui::Button(appState->isCapturing ? "Stop Capture" : "Start Capture", ImVec2(175, 35))) {
                if (appState->isCapturing) {
                    appState->StopCapture();
                }
                else {
                    appState->StartCapture();
                }
            }

            // OSC Rate and FPS settings with fixed widths
            ImGui::SameLine();
            ImGui::SetCursorPosX(210);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("OSC Rate:");
            ImGui::SameLine();

            ImGui::PushItemWidth(100);
            int oscRate = appState->settings.oscRate;
            if (ImGui::InputInt("##oscrate", &oscRate, 1, 5)) {
                if (oscRate < 1) oscRate = 1;
                if (oscRate > 240) oscRate = 240;
                appState->settings.oscRate = oscRate;
                appState->oscInterval = std::chrono::milliseconds(1000 / oscRate);
                if (appState->isCapturing) {
                    appState->oscManager->SetOscRate(oscRate);
                    appState->SaveSettings();
                }
            }
            ImGui::PopItemWidth();

            ImGui::SameLine();
            ImGui::SetCursorPosX(400);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("FPS:");
            ImGui::SameLine();

            ImGui::PushItemWidth(100);
            int fps = appState->settings.captureFps;
            if (ImGui::InputInt("##fps", &fps, 1, 5)) {
                if (fps < 1) fps = 1;
                if (fps > 60) fps = 60;
                appState->settings.captureFps = fps;
                appState->captureInterval = std::chrono::milliseconds(1000 / fps);
                appState->SaveSettings();
            }
            ImGui::PopItemWidth();

            ImGui::Spacing();
            ImGui::Spacing();

            // Enable Spout2 Toggle
            if (appState->isCapturing) {
                // When capturing, show a disabled checkbox
                bool enableSpout = appState->settings.enableSpout;
                ImGui::BeginDisabled();
                ImGui::Checkbox("Use Spout2", &enableSpout);
                ImGui::EndDisabled();

                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Cannot change input mode during capture");
                }
            }
            else {
                // When not capturing, show an enabled checkbox
                bool enableSpout = appState->settings.enableSpout;
                if (ImGui::Checkbox("Use Spout2", &enableSpout)) {
                    appState->settings.enableSpout = enableSpout;
                    appState->SaveSettings();
                }
            }

            // White Mix slider
            ImGui::Spacing();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("White Mix: %d%%", appState->settings.whiteMixValue);

            ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.2f, 0.6f, 1.0f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.1f, 0.4f, 0.9f, 1.0f));
            ImGui::PushItemWidth(145);

            int whiteMix = appState->settings.whiteMixValue;
            if (ImGui::SliderInt("##whitemix", &whiteMix, 0, 100, "")) {
                appState->settings.whiteMixValue = whiteMix;
                appState->SaveSettings();
            }

            ImGui::SameLine();
            if (ImGui::Button("R##whitemix", ImVec2(25, 20))) {  // "R" for Reset
                appState->settings.whiteMixValue = 0; // Default value
                appState->SaveSettings();
            }

            ImGui::PopItemWidth();
            ImGui::PopStyleColor(2);

            // Saturation slider
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Saturation Boost: %d%%", appState->settings.saturationValue);

            ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.2f, 0.6f, 1.0f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.1f, 0.4f, 0.9f, 1.0f));
            ImGui::PushItemWidth(145);

            int saturation = appState->settings.saturationValue;
            if (ImGui::SliderInt("##saturation", &saturation, -100, 100, "")) {
                appState->settings.saturationValue = saturation;
                appState->SaveSettings();
            }

            ImGui::SameLine();
            if (ImGui::Button("R##saturation", ImVec2(25, 20))) {  // "R" for Reset
                appState->settings.saturationValue = 0; // Default value
                appState->SaveSettings();
            }

            ImGui::PopItemWidth();
            ImGui::PopStyleColor(2);

            ImGui::Spacing();
            ImGui::Spacing();

            // Force Max Brightness Toggle
            bool forceMaxBrightness = appState->settings.forceMaxBrightness;
            if (ImGui::Checkbox("Force Max Brightness", &forceMaxBrightness)) {
                appState->settings.forceMaxBrightness = forceMaxBrightness;
                appState->SaveSettings();
            }

            // Enable Smoothing Toggle
            bool enableSmoothing = appState->settings.enableSmoothing;
            if (ImGui::Checkbox("Enable Smoothing", &enableSmoothing)) {
                appState->settings.enableSmoothing = enableSmoothing;
                appState->SaveSettings();
            }


            // Smoothing Slider
            ImGui::Spacing();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Smoothing Rate: %d%%", static_cast<int>(appState->settings.smoothingRateValue * 100.0f));

            ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.2f, 0.6f, 1.0f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.1f, 0.4f, 0.9f, 1.0f));
            ImGui::PushItemWidth(145);

            int smoothingRatePercent = static_cast<int>(appState->settings.smoothingRateValue * 100.0f);
            if (ImGui::SliderInt("##smoothingrate", &smoothingRatePercent, 5, 100, "")) {
                appState->settings.smoothingRateValue = smoothingRatePercent / 100.0f;
                appState->SaveSettings();
            }

            ImGui::SameLine();
            if (ImGui::Button("R##smoothingrate", ImVec2(25, 20))) {  // "R" for Reset
                appState->settings.smoothingRateValue = 0.5f; // Default value
                appState->SaveSettings();
            }

            ImGui::PopItemWidth();
            ImGui::PopStyleColor(2);

            ImGui::Spacing();
            ImGui::Spacing();

            // Enable KeepWindowOnTop Toggle
            bool keepTargetWindowOnTop = appState->settings.keepTargetWindowOnTop;
            if (ImGui::Checkbox("Keep Target On Top", &keepTargetWindowOnTop)) {
                // If the setting was just disabled and we have a valid target window
                if (appState->settings.keepTargetWindowOnTop && !keepTargetWindowOnTop &&
                    appState->targetWindowHandle && appState->windowManager->IsWindowValid(appState->targetWindowHandle)) {
                    // Immediately remove the topmost flag
                    appState->windowManager->SetWindowNotTopMost(appState->targetWindowHandle);
                    appState->SaveSettings();
                }

                // Update the setting
                appState->settings.keepTargetWindowOnTop = keepTargetWindowOnTop;
            }

            ImGui::Spacing();
            ImGui::Spacing();

            // Color preview panel
            ImGui::SameLine();
            ImGui::SetCursorPosX(210);
            ImGui::SetCursorPosY(90);

            // Create a colored rectangle
            ImVec4 currentColor = ImVec4(
                appState->currentColor.r,
                appState->currentColor.g,
                appState->currentColor.b,
                1.0f
            );

            ImVec2 colorPanelPos = ImGui::GetCursorScreenPos();
            ImVec2 colorPanelSize(330, 200);
            ImGui::GetWindowDrawList()->AddRectFilled(
                colorPanelPos,
                ImVec2(colorPanelPos.x + colorPanelSize.x, colorPanelPos.y + colorPanelSize.y),
                ImGui::ColorConvertFloat4ToU32(currentColor),
                0.0f
            );

            ImGui::Dummy(colorPanelSize);

            ImGui::SetCursorPosX(350);
            ImGui::SetCursorPosY(300);
            if (ImGui::Button("About", ImVec2(90, 50))) {
                appState->showAboutWindow = true;
            }

            // Debug View toggle button
            ImGui::SetCursorPosX(450);
            ImGui::SetCursorPosY(300);
            if (ImGui::Button(appState->isDebugViewExpanded ? "Hide Debug" : "Show Debug", ImVec2(90, 50))) {
                appState->isDebugViewExpanded = !appState->isDebugViewExpanded;
                appState->settings.showDebugView = appState->isDebugViewExpanded;

                // Save settings
                appState->SaveSettings();

                // Update window size
                RECT windowRect;
                GetWindowRect(hwnd, &windowRect);
                int newWidth = appState->isDebugViewExpanded ? 570 : 570;
                int newHeight = appState->isDebugViewExpanded ? 900 : 400;
                MoveWindow(hwnd, windowRect.left, windowRect.top, newWidth, newHeight, TRUE);

                // Update width and height for ImGui
                width = newWidth;
                height = newHeight;
            }

            // Status label - show green only for VRChat detected
            ImGui::SetCursorPosX(20);
            ImGui::SetCursorPosY(330);
            bool isVRChat = appState->IsVRChatSelected();
            ImVec4 statusColor = (appState->targetWindowHandle && isVRChat) ?
                ImVec4(0.0f, 0.8f, 0.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f);

            std::string spoutStatusText;
            const char* statusText = nullptr;

            if (appState->settings.enableSpout) {
                if (appState->spoutReceiver->IsConnected()) {
                    spoutStatusText = "Spout: Connected to " + appState->spoutReceiver->GetSenderName();
                    statusText = spoutStatusText.c_str();
                    statusColor = ImVec4(0.0f, 0.8f, 0.0f, 1.0f); // Green for connected
                }
                else {
                    statusText = "Spout: No sender connected";
                    statusColor = ImVec4(1.0f, 0.6f, 0.0f, 1.0f); // Orange for Spout enabled but not connected
                }
            }
            else if (!appState->targetWindowHandle) {
                statusText = "VRChat not found";
                statusColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f); // Red
            }
            else if (isVRChat) {
                statusText = "VRChat detected";
                statusColor = ImVec4(0.0f, 0.8f, 0.0f, 1.0f); // Green
            }
            else {
                statusText = "Other application selected";
                statusColor = ImVec4(1.0f, 0.6f, 0.0f, 1.0f); // Orange
            }

            ImGui::TextColored(statusColor, "Status: %s", statusText);

            // Debug panel
            if (appState->isDebugViewExpanded) {
                ImGui::Spacing(); ImGui::Spacing(); ImGui::Spacing(); ImGui::Spacing(); ImGui::Spacing();

                float totalWidth = ImGui::GetContentRegionAvail().x;
                float leftPadding = 20.0f;
                float rightPadding = 0.0f;
                float separatorWidth = totalWidth - leftPadding - rightPadding;

                ImGui::SetCursorPosX(leftPadding);
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
                ImGui::PushStyleColor(ImGuiCol_Separator, ImGui::GetStyleColorVec4(ImGuiCol_Separator));
                ImGui::InvisibleButton("##separator", ImVec2(separatorWidth, 1));
                ImGui::GetWindowDrawList()->AddLine(
                    ImGui::GetItemRectMin(),
                    ImVec2(ImGui::GetItemRectMin().x + separatorWidth, ImGui::GetItemRectMin().y),
                    ImGui::GetColorU32(ImGuiCol_Separator),
                    1.0f
                );
                ImGui::PopStyleColor();
                ImGui::PopStyleVar();

                ImGui::Spacing(); ImGui::Spacing(); ImGui::Spacing(); ImGui::Spacing(); ImGui::Spacing();

                float remainingHeight = ImGui::GetContentRegionAvail().y - 40;
                ImGui::SetCursorPosX(leftPadding);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(leftPadding, 20));
                ImGui::BeginChild("DebugPanel", ImVec2(totalWidth - leftPadding, remainingHeight), true, ImGuiWindowFlags_NoScrollbar);

                ImGui::BeginGroup();

                // RGB + OSC Values display
                float oscR = appState->currentColor.r * 2 - 1;
                float oscG = appState->currentColor.g * 2 - 1;
                float oscB = appState->currentColor.b * 2 - 1;

                ImGui::Text("RGB: (%d, %d, %d)",
                    static_cast<int>(appState->currentColor.r * 255),
                    static_cast<int>(appState->currentColor.g * 255),
                    static_cast<int>(appState->currentColor.b * 255));

                ImGui::SameLine();
                ImGui::Text("|");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "OSC R:");
                ImGui::SameLine();
                ImGui::Text("%.2f", oscR);
                ImGui::SameLine();
                ImGui::Text("|");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "OSC G:");
                ImGui::SameLine();
                ImGui::Text("%.2f", oscG);
                ImGui::SameLine();
                ImGui::Text("|");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.0f, 0.0f, 1.0f, 1.0f), "OSC B:");
                ImGui::SameLine();
                ImGui::Text("%.2f", oscB);

                ImGui::Spacing();
                ImGui::Spacing();

                // Target Application dropdown with limited width
                ImGui::Text("Target Application:");
                ImGui::Spacing();

                ImGui::PushItemWidth(180.0f); // Limit width
                if (ImGui::BeginCombo("##targetapp",
                    appState->selectedWindowIdx >= 0 && appState->selectedWindowIdx < appState->windowList.size()
                    ? appState->windowList[appState->selectedWindowIdx].title.c_str()
                    : "Select Application")) {

                    for (int i = 0; i < appState->windowList.size(); i++) {
                        bool isSelected = (appState->selectedWindowIdx == i);
                        std::string comboLabel = appState->windowList[i].title + " (" + appState->windowList[i].processName + ")";

                        if (ImGui::Selectable(comboLabel.c_str(), isSelected)) {
                            // Store the previous window handle before changing it
                            HWND previousWindowHandle = appState->targetWindowHandle;

                            // Update to the new window
                            appState->selectedWindowIdx = i;
                            appState->targetWindowHandle = appState->windowList[i].handle;
                            appState->captureArea = appState->windowManager->GetOptimalCaptureArea(appState->targetWindowHandle);

                            // Clear user crop when changing applications
                            appState->userCropArea = { 0, 0, 0, 0 };

                            // If capturing, release the previous window before setting the new one
                            if (appState->isCapturing && previousWindowHandle && previousWindowHandle != appState->targetWindowHandle) {
                                appState->windowManager->SetWindowNotTopMost(previousWindowHandle);
                                // Then start capture with the new window
                                appState->StopCapture();
                                appState->StartCapture();
                            }
                        }

                        if (isSelected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopItemWidth();

                ImGui::Spacing();

                // Refresh and Clear Crop buttons
                if (ImGui::Button("Refresh", ImVec2(82, 27))) {
                    appState->RefreshApplicationList();
                }

                ImGui::SameLine();

                if (ImGui::Button("Clear Crop", ImVec2(90, 27))) {
                    appState->userCropArea = { 0, 0, 0, 0 };
                }

                ImGui::Spacing();

                // Auto-Detect VRChat button
                if (ImGui::Button("Auto-Detect VRChat", ImVec2(180, 27))) {
                    // Store the previous window handle before changing it
                    HWND previousWindowHandle = appState->targetWindowHandle;

                    appState->selectedWindowIdx = -1;
                    appState->targetWindowHandle = appState->windowManager->FindVRChatWindow();

                    if (appState->targetWindowHandle) {
                        appState->captureArea = appState->windowManager->GetOptimalCaptureArea(appState->targetWindowHandle);

                        // Update the selected index if VRChat is found
                        appState->RefreshApplicationList();
                        for (size_t i = 0; i < appState->windowList.size(); i++) {
                            if (appState->windowList[i].handle == appState->targetWindowHandle) {
                                appState->selectedWindowIdx = static_cast<int>(i);
                                break;
                            }
                        }

                        // If capturing, release the previous window if it's different than the new one
                        if (appState->isCapturing && previousWindowHandle &&
                            previousWindowHandle != appState->targetWindowHandle) {
                            appState->windowManager->SetWindowNotTopMost(previousWindowHandle);

                            // Restart the capture with the new window
                            appState->StopCapture();
                            appState->StartCapture();
                        }
                    }
                }

                ImGui::EndGroup();

                // Preview image with proper aspect ratio
                ImGui::BeginGroup();

                ImGui::Spacing();

                // Calculate available space and aspect ratio
                ImVec2 availRegion = ImGui::GetContentRegionAvail();

                if (appState->previewTexture && appState->lastCapturedImage.IsValid()) {
                    // Calculate aspect ratio-correct size
                    float aspectRatio = (float)appState->lastCapturedImage.width / appState->lastCapturedImage.height;
                    ImVec2 imageSize;

                    if (aspectRatio > availRegion.x / availRegion.y) {
                        // Width is the limiting factor
                        imageSize.x = availRegion.x;
                        imageSize.y = imageSize.x / aspectRatio;
                    }
                    else {
                        // Height is the limiting factor
                        imageSize.y = availRegion.y;
                        imageSize.x = imageSize.y * aspectRatio;
                    }

                    // Left Padding of image preview
                    float offsetX = (availRegion.x - imageSize.x) * 0.0f;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);

                    // Store cursor pos for selection handling
                    ImVec2 imagePos = ImGui::GetCursorScreenPos();

                    // Display the preview image
                    ImGui::Image((ImTextureID)appState->previewTexture, imageSize);

                    // Handle mouse interactions for crop selection
                    if (ImGui::IsItemHovered()) {
                        ImVec2 mousePos = ImGui::GetMousePos();

                        // Calculate relative position within the image bounds
                        float relX = mousePos.x - imagePos.x;
                        float relY = mousePos.y - imagePos.y;

                        // Normalize to original image coordinates
                        int imgX = static_cast<int>((relX / imageSize.x) * appState->lastCapturedImage.width);
                        int imgY = static_cast<int>((relY / imageSize.y) * appState->lastCapturedImage.height);

                        // Clamp to valid image bounds
                        imgX = std::max(0, std::min(appState->lastCapturedImage.width - 1, imgX));
                        imgY = std::max(0, std::min(appState->lastCapturedImage.height - 1, imgY));

                        // Handle mouse down - start selection
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                            appState->isSelecting = true;
                            appState->isActivelySelecting = true;
                            appState->startPoint = ImVec2(static_cast<float>(imgX), static_cast<float>(imgY));
                        }

                        // Handle mouse up - finalize selection
                        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && appState->isSelecting) {
                            appState->isSelecting = false;
                            appState->isActivelySelecting = false;

                            // Calculate the selection rectangle in image coordinates
                            int left = static_cast<int>(std::min(appState->startPoint.x, static_cast<float>(imgX)));
                            int top = static_cast<int>(std::min(appState->startPoint.y, static_cast<float>(imgY)));
                            int right = static_cast<int>(std::max(appState->startPoint.x, static_cast<float>(imgX)));
                            int bottom = static_cast<int>(std::max(appState->startPoint.y, static_cast<float>(imgY)));

                            // Only set if it meets minimum size
                            if ((right - left) >= 10 && (bottom - top) >= 10) {
                                appState->userCropArea.left = left;
                                appState->userCropArea.top = top;
                                appState->userCropArea.right = right;
                                appState->userCropArea.bottom = bottom;
                            }
                            else {
                                appState->userCropArea = { 0, 0, 0, 0 };
                            }
                        }
                    }

                    // Draw the selection rectangle
                    if (appState->isSelecting && appState->isActivelySelecting) {
                        // Get current mouse position for drawing
                        ImVec2 mousePos = ImGui::GetMousePos();
                        float relX = mousePos.x - imagePos.x;
                        float relY = mousePos.y - imagePos.y;

                        // Normalize to image coordinates
                        int currentImgX = static_cast<int>((relX / imageSize.x) * appState->lastCapturedImage.width);
                        int currentImgY = static_cast<int>((relY / imageSize.y) * appState->lastCapturedImage.height);

                        // Clamp to image bounds
                        currentImgX = std::max(0, std::min(appState->lastCapturedImage.width - 1, currentImgX));
                        currentImgY = std::max(0, std::min(appState->lastCapturedImage.height - 1, currentImgY));

                        // Convert image coordinates back to screen coordinates
                        float startScreenX = imagePos.x + (appState->startPoint.x / appState->lastCapturedImage.width) * imageSize.x;
                        float startScreenY = imagePos.y + (appState->startPoint.y / appState->lastCapturedImage.height) * imageSize.y;
                        float endScreenX = imagePos.x + (static_cast<float>(currentImgX) / appState->lastCapturedImage.width) * imageSize.x;
                        float endScreenY = imagePos.y + (static_cast<float>(currentImgY) / appState->lastCapturedImage.height) * imageSize.y;

                        // Draw selection rectangle (yellow)
                        ImGui::GetWindowDrawList()->AddRect(
                            ImVec2(std::min(startScreenX, endScreenX), std::min(startScreenY, endScreenY)),
                            ImVec2(std::max(startScreenX, endScreenX), std::max(startScreenY, endScreenY)),
                            IM_COL32(255, 255, 0, 255),  // Yellow
                            0.0f,
                            0,
                            2.0f  // Line thickness
                        );
                    }
                    else if (appState->userCropArea.right > appState->userCropArea.left &&
                        appState->userCropArea.bottom > appState->userCropArea.top) {
                        // Convert saved crop area from image to screen coordinates
                        float cropLeftScreen = imagePos.x + ((float)appState->userCropArea.left / appState->lastCapturedImage.width) * imageSize.x;
                        float cropTopScreen = imagePos.y + ((float)appState->userCropArea.top / appState->lastCapturedImage.height) * imageSize.y;
                        float cropRightScreen = imagePos.x + ((float)appState->userCropArea.right / appState->lastCapturedImage.width) * imageSize.x;
                        float cropBottomScreen = imagePos.y + ((float)appState->userCropArea.bottom / appState->lastCapturedImage.height) * imageSize.y;

                        // Draw the saved crop rectangle (red)
                        ImGui::GetWindowDrawList()->AddRect(
                            ImVec2(cropLeftScreen, cropTopScreen),
                            ImVec2(cropRightScreen, cropBottomScreen),
                            IM_COL32(255, 0, 0, 255),  // Red
                            0.0f,
                            0,
                            2.0f  // Line thickness
                        );
                    }
                }
                else {
                    // Calculate center position for the text
                    ImVec2 textSize = ImGui::CalcTextSize("No preview available");

                    // Get current cursor position as reference point
                    ImVec2 currentPos = ImGui::GetCursorPos();

                    // Calculate centered position
                    float textPosX = currentPos.x + (availRegion.x - textSize.x) * 0.5f;
                    float textPosY = currentPos.y + (availRegion.y - textSize.y) * 0.5f;

                    // Create space with a dummy
                    ImGui::Dummy(availRegion);

                    // Set cursor to centered position and draw text
                    ImGui::SetCursorPos(ImVec2(textPosX, textPosY));
                    ImGui::Text("No preview available");
                }

                ImGui::EndGroup();

                ImGui::EndChild(); // End DebugPanel
                ImGui::PopStyleVar();
            }

            ImGui::End();
            ImGui::PopStyleVar(3);
        }

        // Render About window if open
        if (appState->showAboutWindow) {
            ImGui::SetNextWindowSize(ImVec2(400, 350), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f),
                ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

            if (ImGui::Begin("About AutoLightOSC", &appState->showAboutWindow, ImGuiWindowFlags_NoCollapse)) {
                ImGui::PushTextWrapPos(ImGui::GetWindowWidth() - 20);

                // Title
                ImGui::Text("AutoLightOSC v%s", appState->appVersion);
                ImGui::Separator();

                // Logo
                if (appState->logoTexture) {
                    // Center the logo
                    float logoWidth = 64;
                    float logoHeight = 64;

                    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - logoWidth) * 0.5f);
                    ImGui::Image((ImTextureID)appState->logoTexture, ImVec2(logoWidth, logoHeight));
                    ImGui::Spacing();
                    ImGui::Spacing();
                }

                // Description
                ImGui::Text("AutoLightOSC captures screen colors and sends them to VRChat avatars via OSC.");
                ImGui::Spacing();
                ImGui::Spacing();

                // Credits
                ImGui::Text("Created by: BigSoulja");
                ImGui::Text("Contributors: @ocornut, Dear ImGui, @leadedge, Spout2");
                ImGui::Spacing();
                ImGui::Spacing();

                // Links - make them clickable
                ImGui::Text("Links:");

                ImGui::SameLine();
                if (ImGui::SmallButton("GitHub")) {
                    ShellExecuteA(NULL, "open", appState->gitHubUrl, NULL, NULL, SW_SHOWNORMAL);
                }

                ImGui::SameLine();
                if (ImGui::SmallButton("Website")) {
                    ShellExecuteA(NULL, "open", appState->websiteUrl, NULL, NULL, SW_SHOWNORMAL);
                }

                ImGui::Spacing();
                ImGui::Spacing();

                // License or additional information
                ImGui::Text("This software is licensed under the MIT License.");

                // Close button at the bottom
                ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 40);
                if (ImGui::Button("Close", ImVec2(80, 30))) {
                    appState->showAboutWindow = false;
                }

                ImGui::PopTextWrapPos();
                ImGui::End();
            }
        }

        // Rendering
        ImGui::Render();
        const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0); // Present with vsync
    }

    // Save settings before exit
    appState->SaveSettings();

    // Ensure any topmost window is released
    if (appState->isCapturing && appState->targetWindowHandle) {
        appState->windowManager->SetWindowNotTopMost(appState->targetWindowHandle);
    }

    // Cleanup
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClass(wc.lpszClassName, wc.hInstance);

    return 0;
}

// Texture loader

bool LoadTextureFromFile(const char* filename, ID3D11ShaderResourceView** out_srv, int& out_width, int& out_height) {
    // Load from disk into a raw RGBA buffer
    int image_width = 0;
    int image_height = 0;
    unsigned char* image_data = stbi_load(filename, &image_width, &image_height, NULL, 4);
    if (image_data == NULL)
        return false;

    // Create texture
    D3D11_TEXTURE2D_DESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.Width = image_width;
    desc.Height = image_height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;

    ID3D11Texture2D* pTexture = NULL;
    D3D11_SUBRESOURCE_DATA subResource;
    subResource.pSysMem = image_data;
    subResource.SysMemPitch = desc.Width * 4;
    subResource.SysMemSlicePitch = 0;
    g_pd3dDevice->CreateTexture2D(&desc, &subResource, &pTexture);

    // Create texture view
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
    ZeroMemory(&srvDesc, sizeof(srvDesc));
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = desc.MipLevels;
    srvDesc.Texture2D.MostDetailedMip = 0;
    g_pd3dDevice->CreateShaderResourceView(pTexture, &srvDesc, out_srv);
    pTexture->Release();

    stbi_image_free(image_data);

    out_width = image_width;
    out_height = image_height;
    return true;
}

// Helper functions for Direct3D

bool CreateDeviceD3D(HWND hWnd)
{
    // Setup swap chain
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext) != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProc(hWnd, msg, wParam, lParam);
}