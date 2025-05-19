// UserSettings.h
#pragma once

#include <string>
#include <filesystem>
#include <nlohmann/json.hpp>

class UserSettings {
public:
    // Default settings
    bool useDXGI = false;
    bool autoCapture = false;
    int captureFps = 5;
    int whiteMixValue = 0;
    int saturationValue = 0;
    bool forceMaxBrightness = true;
    bool enableSmoothing = true;
    float smoothingRateValue = 0.5f;
    bool showDebugView = false;
    int oscRate = 3;
    bool keepTargetWindowOnTop = false;
    bool enableSpout = false;
    int oscPort = 9000;
    std::string oscRParameter = "AL_Red";
    std::string oscGParameter = "AL_Green";
    std::string oscBParameter = "AL_Blue";

    UserSettings();

    static UserSettings Load();
    void Save() const;

private:
    static std::filesystem::path GetSettingsFilePath();
};