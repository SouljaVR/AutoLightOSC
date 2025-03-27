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

// ColorProcessor.cpp

#define NOMINMAX
#include <windows.h>

#include "ColorProcessor.h"
#include <algorithm>
#include <cmath>

ColorProcessor::ColorProcessor(UserSettings& settings)
    : settings(settings), lastNonBlackColor(), currentSmoothedColor() {
}

Bitmap ColorProcessor::DownscaleForProcessing(const Bitmap& image) {
    if (!image.IsValid()) {
        return Bitmap();
    }

    if (image.width <= MaxProcessingSize && image.height <= MaxProcessingSize) {
        // Return a copy of the image
        Bitmap result(image.width, image.height);
        std::copy(image.data.get(), image.data.get() + (image.stride * image.height), result.data.get());
        return result;
    }

    float scale = std::min(
        static_cast<float>(MaxProcessingSize) / image.width,
        static_cast<float>(MaxProcessingSize) / image.height
    );

    int newWidth = static_cast<int>(image.width * scale);
    int newHeight = static_cast<int>(image.height * scale);

    Bitmap result(newWidth, newHeight);

    // Simple bilinear downscale
    for (int y = 0; y < newHeight; y++) {
        for (int x = 0; x < newWidth; x++) {
            float srcX = x / scale;
            float srcY = y / scale;

            int srcX1 = static_cast<int>(srcX);
            int srcY1 = static_cast<int>(srcY);
            int srcX2 = std::min(srcX1 + 1, image.width - 1);
            int srcY2 = std::min(srcY1 + 1, image.height - 1);

            float dx = srcX - srcX1;
            float dy = srcY - srcY1;

            // Get source pixels
            BYTE* p11 = image.data.get() + (srcY1 * image.stride + srcX1 * 4);
            BYTE* p12 = image.data.get() + (srcY1 * image.stride + srcX2 * 4);
            BYTE* p21 = image.data.get() + (srcY2 * image.stride + srcX1 * 4);
            BYTE* p22 = image.data.get() + (srcY2 * image.stride + srcX2 * 4);

            // Bilinear interpolation
            for (int c = 0; c < 4; c++) {
                float top = p11[c] * (1 - dx) + p12[c] * dx;
                float bottom = p21[c] * (1 - dx) + p22[c] * dx;
                float value = top * (1 - dy) + bottom * dy;

                result.data.get()[y * result.stride + x * 4 + c] = static_cast<BYTE>(value);
            }
        }
    }

    return result;
}

ColorRGB ColorProcessor::GetAverageColor(const Bitmap& bitmap) {
    if (!bitmap.IsValid()) {
        return ColorRGB(0, 0, 0);
    }

    long long r = 0, g = 0, b = 0;
    int totalPixels = bitmap.width * bitmap.height;

    if (totalPixels == 0) {
        return ColorRGB(0, 0, 0);
    }

    const BYTE* pixelData = bitmap.data.get();

    for (int y = 0; y < bitmap.height; y++) {
        for (int x = 0; x < bitmap.width; x++) {
            int offset = y * bitmap.stride + x * 4; // 4 bytes per pixel (BGRA format)
            b += pixelData[offset];
            g += pixelData[offset + 1];
            r += pixelData[offset + 2];
        }
    }

    float avgR = static_cast<float>(r) / (totalPixels * 255);
    float avgG = static_cast<float>(g) / (totalPixels * 255);
    float avgB = static_cast<float>(b) / (totalPixels * 255);

    // Swaps Red & Blue channels for Spout2 input
    if (settings.enableSpout) {
        std::swap(avgR, avgB);
    }

    return ColorRGB(avgR, avgG, avgB);
}

ColorRGB ColorProcessor::ProcessColor(const ColorRGB& avgColor) {
    float r = avgColor.r;
    float g = avgColor.g;
    float b = avgColor.b;

    // Keep track of last non-black color
    if (r != 0.0f || g != 0.0f || b != 0.0f) {
        lastNonBlackColor = ColorRGB(r, g, b);
    }
    else {
        r = lastNonBlackColor.r;
        g = lastNonBlackColor.g;
        b = lastNonBlackColor.b;
    }

    // Apply max brightness if enabled
    if (settings.forceMaxBrightness) {
        ColorRGB brightColor = ForceMaxBrightness(r, g, b);
        r = brightColor.r;
        g = brightColor.g;
        b = brightColor.b;
    }

    // Apply white mix
    ColorRGB mixedColor = ApplyWhiteMix(r, g, b);
    r = mixedColor.r;
    g = mixedColor.g;
    b = mixedColor.b;

    // Apply saturation adjustment
    ColorRGB saturatedColor = ApplySaturation(r, g, b);
    r = saturatedColor.r;
    g = saturatedColor.g;
    b = saturatedColor.b;

    return ColorRGB(r, g, b);
}

ColorRGB ColorProcessor::ForceMaxBrightness(float r, float g, float b) {
    float maxVal = std::max(r, std::max(g, b));
    if (maxVal <= 0.0f) {
        return ColorRGB(r, g, b);
    }

    float scale = 1.0f / maxVal;
    return ColorRGB(
        std::min(r * scale, 1.0f),
        std::min(g * scale, 1.0f),
        std::min(b * scale, 1.0f)
    );
}

ColorRGB ColorProcessor::ApplyWhiteMix(float r, float g, float b) {
    float whiteMix = settings.whiteMixValue / 100.0f;

    r = r + (1.0f - r) * whiteMix;
    g = g + (1.0f - g) * whiteMix;
    b = b + (1.0f - b) * whiteMix;

    return ColorRGB(r, g, b);
}

// RGB to HSV conversion helper
void ColorProcessor::RGBtoHSV(float r, float g, float b, float& h, float& s, float& v) {
    float cmax = std::max(std::max(r, g), b);
    float cmin = std::min(std::min(r, g), b);
    float delta = cmax - cmin;

    h = 0; // Hue
    s = 0; // Saturation
    v = cmax; // Value

    // Calculate hue
    if (delta != 0) {
        if (cmax == r) {
            h = fmod(((g - b) / delta), 6.0f);
        }
        else if (cmax == g) {
            h = ((b - r) / delta) + 2.0f;
        }
        else {
            h = ((r - g) / delta) + 4.0f;
        }

        h *= 60.0f; // Convert to degrees
        if (h < 0) h += 360.0f;
    }

    // Calculate saturation
    if (cmax != 0) {
        s = delta / cmax;
    }
}

// HSV to RGB conversion helper
void ColorProcessor::HSVtoRGB(float h, float s, float v, float& r, float& g, float& b) {
    float c = v * s; // Chroma
    float x = c * (1 - fabs(fmod(h / 60.0f, 2) - 1));
    float m = v - c;

    r = 0; g = 0; b = 0;

    if (h >= 0 && h < 60) {
        r = c; g = x; b = 0;
    }
    else if (h >= 60 && h < 120) {
        r = x; g = c; b = 0;
    }
    else if (h >= 120 && h < 180) {
        r = 0; g = c; b = x;
    }
    else if (h >= 180 && h < 240) {
        r = 0; g = x; b = c;
    }
    else if (h >= 240 && h < 300) {
        r = x; g = 0; b = c;
    }
    else {
        r = c; g = 0; b = x;
    }

    r += m;
    g += m;
    b += m;
}

// Apply saturation adjustment
ColorRGB ColorProcessor::ApplySaturation(float r, float g, float b) {
    // Skip if saturation adjustment is 0
    if (settings.saturationValue == 0) {
        return ColorRGB(r, g, b);
    }

    // Convert RGB to HSV
    float h, s, v;
    RGBtoHSV(r, g, b, h, s, v);

    // Calculate saturation adjustment factor
    // Range is -100 to +100
    // At -100, factor is 0.0 (fully desaturated)
    // At 0, factor is 1.0 (unchanged)
    // At +100, factor is 2.0 (doubled saturation)
    float saturationFactor = 1.0f + (settings.saturationValue / 100.0f);

    // Apply saturation adjustment
    s = std::clamp(s * saturationFactor, 0.0f, 1.0f);

    // Convert back to RGB
    float newR, newG, newB;
    HSVtoRGB(h, s, v, newR, newG, newB);

    return ColorRGB(newR, newG, newB);
}

ColorRGB ColorProcessor::GetSmoothedColor(float deltaTime, const ColorRGB& targetColor) {
    if (settings.enableSmoothing) {
        // Calculate appropriate smoothing factor based on time
        float smoothingFactor = std::min(1.0f, deltaTime / settings.smoothingRateValue);

        // Apply smoothing with proper dampening based on time
        currentSmoothedColor.r += (targetColor.r - currentSmoothedColor.r) * smoothingFactor;
        currentSmoothedColor.g += (targetColor.g - currentSmoothedColor.g) * smoothingFactor;
        currentSmoothedColor.b += (targetColor.b - currentSmoothedColor.b) * smoothingFactor;
    }
    else {
        // When smoothing is disabled, immediately use the target color
        currentSmoothedColor = targetColor;
    }

    return currentSmoothedColor;
}