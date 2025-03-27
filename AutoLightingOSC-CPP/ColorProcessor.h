#pragma once

#include <Windows.h>
#include <vector>
#include <memory>
#include "UserSettings.h"

struct Bitmap {
    std::shared_ptr<BYTE[]> data;
    int width;
    int height;
    int stride;

    Bitmap() : data(nullptr), width(0), height(0), stride(0) {}

    Bitmap(int w, int h) : width(w), height(h), stride(w * 4) {
        data = std::shared_ptr<BYTE[]>(new BYTE[stride * height]());
    }

    bool IsValid() const {
        return data != nullptr && width > 0 && height > 0;
    }
};

struct ColorRGB {
    float r, g, b;

    ColorRGB() : r(0), g(0), b(0) {}
    ColorRGB(float red, float green, float blue) : r(red), g(green), b(blue) {}

    bool operator==(const ColorRGB& other) const {
        return r == other.r && g == other.g && b == other.b;
    }

    bool operator!=(const ColorRGB& other) const {
        return !(*this == other);
    }
};

class ColorProcessor {
private:
    static const int MaxProcessingSize = 100; // Maximum width or height for processing
    ColorRGB lastNonBlackColor;
    ColorRGB currentSmoothedColor;

    UserSettings& settings;

    ColorRGB ForceMaxBrightness(float r, float g, float b);
    ColorRGB ApplyWhiteMix(float r, float g, float b);

    void RGBtoHSV(float r, float g, float b, float& h, float& s, float& v);
    void HSVtoRGB(float h, float s, float v, float& r, float& g, float& b);
    ColorRGB ApplySaturation(float r, float g, float b);

public:
    ColorProcessor(UserSettings& settings);

    Bitmap DownscaleForProcessing(const Bitmap& image);
    ColorRGB GetAverageColor(const Bitmap& bitmap);
    ColorRGB ProcessColor(const ColorRGB& avgColor);
    ColorRGB GetSmoothedColor(float deltaTime, const ColorRGB& targetColor);
};