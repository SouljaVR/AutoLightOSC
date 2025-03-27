// UserSettings.h
#pragma once

#include <string>
#include <filesystem>
#include <nlohmann/json.hpp>

class UserSettings {
public:
    // Default settings
    int captureFps = 5;
    int whiteMixValue = 0;
    int saturationValue = 0;
    bool forceMaxBrightness = true;
    bool enableSmoothing = true;
    float smoothingRateValue = 0.5f;
    bool showDebugView = false;
    int oscRate = 5;
    bool keepTargetWindowOnTop = false;
    bool enableSpout = false;

    UserSettings();

    static UserSettings Load();
    void Save() const;

private:
    static std::filesystem::path GetSettingsFilePath();
};