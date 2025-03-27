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

// UserSettings.cpp

#include "UserSettings.h"
#include <fstream>
#include <iostream>
#include <ShlObj.h>

UserSettings::UserSettings() {
}

std::filesystem::path UserSettings::GetSettingsFilePath() {
    char appDataPath[MAX_PATH];
    SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appDataPath);

    std::filesystem::path settingsDir = std::filesystem::path(appDataPath) / "AutoLightOSC";
    std::filesystem::path settingsFile = settingsDir / "settings.json";

    return settingsFile;
}

UserSettings UserSettings::Load() {
    UserSettings settings;

    try {
        std::filesystem::path settingsFile = GetSettingsFilePath();
        std::filesystem::path settingsDir = settingsFile.parent_path();

        // Create directory if it doesn't exist
        if (!std::filesystem::exists(settingsDir)) {
            std::filesystem::create_directories(settingsDir);
        }

        // If file exists, load settings
        if (std::filesystem::exists(settingsFile)) {
            std::ifstream file(settingsFile);
            if (file.is_open()) {
                nlohmann::json j;
                file >> j;

                // Load values from JSON
                if (j.contains("captureFps")) settings.captureFps = j["captureFps"];
                if (j.contains("whiteMixValue")) settings.whiteMixValue = j["whiteMixValue"];
                if (j.contains("saturationValue")) settings.saturationValue = j["saturationValue"];
                if (j.contains("forceMaxBrightness")) settings.forceMaxBrightness = j["forceMaxBrightness"];
                if (j.contains("enableSmoothing")) settings.enableSmoothing = j["enableSmoothing"];
                if (j.contains("smoothingRateValue")) settings.smoothingRateValue = j["smoothingRateValue"];
                if (j.contains("oscRate")) settings.oscRate = j["oscRate"];
                if (j.contains("keepTargetWindowOnTop")) settings.keepTargetWindowOnTop = j["keepTargetWindowOnTop"];
                if (j.contains("enableSpout")) settings.enableSpout = j["enableSpout"];

                file.close();
            }
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error loading settings: " << e.what() << std::endl;
    }

    return settings;
}

void UserSettings::Save() const {
    try {
        std::filesystem::path settingsFile = GetSettingsFilePath();
        std::filesystem::path settingsDir = settingsFile.parent_path();

        // Create directory if it doesn't exist
        if (!std::filesystem::exists(settingsDir)) {
            std::filesystem::create_directories(settingsDir);
        }

        // Create JSON object
        nlohmann::json j;
        j["captureFps"] = captureFps;
        j["whiteMixValue"] = whiteMixValue;
        j["saturationValue"] = saturationValue;
        j["forceMaxBrightness"] = forceMaxBrightness;
        j["enableSmoothing"] = enableSmoothing;
        j["smoothingRateValue"] = smoothingRateValue;
        j["oscRate"] = oscRate;
        j["keepTargetWindowOnTop"] = keepTargetWindowOnTop;
        j["enableSpout"] = enableSpout;

        // Write to file
        std::ofstream file(settingsFile);
        if (file.is_open()) {
            file << j.dump(4);
            file.close();
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error saving settings: " << e.what() << std::endl;
    }
}