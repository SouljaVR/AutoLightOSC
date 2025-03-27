// WindowManager.h
#pragma once

#include <Windows.h>
#include <vector>
#include <string>

struct WindowInfo {
    HWND handle;
    std::string title;
    std::string processName;
    DWORD processId;

    std::string ToString() const {
        return title + " (" + processName + ")";
    }
};

class WindowManager {
private:
    static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam);

public:
    WindowManager();

    std::vector<WindowInfo> GetOpenWindows();
    HWND FindVRChatWindow();
    RECT GetOptimalCaptureArea(HWND windowHandle);
    bool IsWindowValid(HWND hWnd);
    bool SetWindowOnTop(HWND hWnd);
    bool SetWindowNotTopMost(HWND hWnd);
};