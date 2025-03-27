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

// WindowManager.cpp

#include "WindowManager.h"
#include <string>
#include <iostream>

WindowManager::WindowManager() {
}

// Static callback function for EnumWindows
BOOL CALLBACK WindowManager::EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    auto windowList = reinterpret_cast<std::vector<WindowInfo>*>(lParam);

    if (!IsWindowVisible(hwnd)) return TRUE;

    // Get window title
    char title[256];
    int length = GetWindowTextA(hwnd, title, sizeof(title));
    if (length == 0) return TRUE;

    // Get process ID
    DWORD processId;
    GetWindowThreadProcessId(hwnd, &processId);

    // Get process name
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!hProcess) return TRUE;

    char processPath[MAX_PATH];
    DWORD size = sizeof(processPath);
    if (QueryFullProcessImageNameA(hProcess, 0, processPath, &size)) {
        std::string fullPath(processPath);

        // Extract the filename
        size_t pos = fullPath.find_last_of('\\');
        std::string processName = (pos != std::string::npos) ? fullPath.substr(pos + 1) : fullPath;

        // Remove the file extension
        pos = processName.find_last_of('.');
        if (pos != std::string::npos) {
            processName = processName.substr(0, pos);
        }

        // Check if window is minimized
        if (!IsIconic(hwnd)) {
            WindowInfo info;
            info.handle = hwnd;
            info.title = title;
            info.processName = processName;
            info.processId = processId;

            windowList->push_back(info);
        }
    }

    CloseHandle(hProcess);
    return TRUE;
}

std::vector<WindowInfo> WindowManager::GetOpenWindows() {
    std::vector<WindowInfo> windowList;
    DWORD currentProcessId = GetCurrentProcessId();

    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&windowList));

    // Filter out our own process after enumeration
    windowList.erase(
        std::remove_if(windowList.begin(), windowList.end(),
            [currentProcessId](const WindowInfo& info) {
                return info.processId == currentProcessId;
            }),
        windowList.end()
    );

    return windowList;
}

HWND WindowManager::FindVRChatWindow() {
    auto windows = GetOpenWindows();

    for (const auto& window : windows) {
        if (window.processName == "VRChat") {
            return window.handle;
        }
    }

    return nullptr;
}

RECT WindowManager::GetOptimalCaptureArea(HWND windowHandle) {
    if (!windowHandle) {
        return { 0, 0, 0, 0 };
    }

    RECT windowRect, clientRect;
    GetWindowRect(windowHandle, &windowRect);
    GetClientRect(windowHandle, &clientRect);

    // Calculate client area in screen coordinates
    POINT clientOrigin = { 0, 0 };
    ClientToScreen(windowHandle, &clientOrigin);

    // Calculate window decoration sizes
    int titleBarHeight = clientOrigin.y - windowRect.top;
    int leftBorderWidth = clientOrigin.x - windowRect.left;
    int rightBorderWidth = windowRect.right - (clientOrigin.x + clientRect.right);
    int bottomBorderHeight = windowRect.bottom - (clientOrigin.y + clientRect.bottom);

    const int PADDING = 5;

    RECT captureArea;
    captureArea.left = windowRect.left + leftBorderWidth + PADDING;
    captureArea.top = windowRect.top + titleBarHeight + PADDING;
    captureArea.right = windowRect.right - rightBorderWidth - PADDING;
    captureArea.bottom = windowRect.bottom - bottomBorderHeight - PADDING;

    return captureArea;
}

bool WindowManager::IsWindowValid(HWND hWnd) {
    if (!hWnd) return false;
    if (IsIconic(hWnd)) return false;

    RECT rect;
    return GetWindowRect(hWnd, &rect) &&
        (rect.right - rect.left > 0) &&
        (rect.bottom - rect.top > 0);
}

bool WindowManager::SetWindowOnTop(HWND hWnd) {
    return SetWindowPos(
        hWnd,
        HWND_TOPMOST,
        0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE
    );
}

bool WindowManager::SetWindowNotTopMost(HWND hWnd) {
    BOOL result = SetWindowPos(
        hWnd,
        HWND_NOTOPMOST,
        0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
    );

    if (result) {
        result = SetWindowPos(
            hWnd,
            HWND_TOP,
            0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
        );
    }

    return result;
}