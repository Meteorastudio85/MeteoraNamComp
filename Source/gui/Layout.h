#pragma once
// Geometry of the panel artwork (Resources/panel.png, 1680 x 637 px).
// Measured on the artwork by tools/make_assets.py - keep in sync.
#include <juce_graphics/juce_graphics.h>

namespace Layout
{
    constexpr float width  = 1680.0f;
    constexpr float height = 637.0f;

    struct Pt { float x, y; };

    // Knob: rotation centre, sprite (top-left + size, cut at that exact spot),
    // painted pointer length and width.
    struct Knob { Pt c; int spriteX, spriteY, spriteSize; float pointerLen, pointerWidth, hitR; };

    constexpr Knob threshold { { 312.0f, 497.0f },   246, 431, 132, 60.0f, 5.0f, 64.0f };
    constexpr Knob ratio     { { 580.0f, 497.0f },   514, 431, 132, 60.0f, 5.0f, 64.0f };
    constexpr Knob attack    { { 846.0f, 497.0f },   780, 431, 132, 60.0f, 5.0f, 64.0f };
    constexpr Knob release   { { 1117.0f, 497.0f }, 1051, 431, 132, 60.0f, 5.0f, 64.0f };
    constexpr Knob makeup    { { 1380.4f, 497.0f }, 1314, 431, 132, 60.0f, 5.0f, 64.0f };
    constexpr Knob mix       { { 1323.5f, 212.0f }, 1264, 152, 120, 47.0f, 4.4f, 58.0f };

    constexpr Pt power { 1523.0f, 481.0f };  constexpr int powerSpriteX = 1465, powerSpriteY = 423, powerSpriteSize = 116;
    constexpr float powerHitR = 46.0f;
    constexpr Pt clipLed { 1519.0f, 149.0f }; constexpr int ledSpriteX = 1489, ledSpriteY = 119, ledSpriteSize = 60;
    constexpr Pt vuButton { 141.0f, 489.0f }; constexpr float vuButtonR = 33.0f;
    constexpr Pt captureLed { 221.0f, 375.7f }; constexpr float captureHitW = 88.0f;
    constexpr Pt ecoLed     { 362.0f, 375.7f }; constexpr float ecoHitW = 52.0f;
    constexpr float switchLedR = 6.6f;

    // meter face and needle
    constexpr float faceX = 645.5f, faceY = 81.8f, faceW = 407.6f, faceH = 202.0f;
    constexpr Pt pivot { 849.1f, 297.35f };
    constexpr float needleInner = 20.0f, needleOuter = 192.0f, needleWidth = 2.6f;
    constexpr float angMin = -50.0f, angMax = 50.0f;     // degrees from vertical

    // knob pointer angles (degrees, 0 = up, clockwise positive)
    inline float thresholdAngle (float v) { return v * 9.0f; }              // -15..+15 -> -135..+135
    inline float ratioAngle (int i)       { return (float) (i - 1) * 60.0f; }
    inline float attackAngle (int i)      { return -125.0f + 50.0f * (float) i; }
    inline float releaseAngle (int i)     { return -120.0f + 60.0f * (float) i; }
    inline float makeupAngle (float v)    { return -135.0f + v * 18.0f; }    // 0..15
    inline float mixAngle (float v)       { return -150.0f + v * 3.0f; }     // 0..100

    // meter scales (same law as the printed face)
    inline float vuFraction (float db)    { return juce::jlimit (0.0f, 1.0f, std::pow (10.0f, db / 20.0f) / std::pow (10.0f, 3.0f / 20.0f)); }
    inline float vuAngle (float db)       { return angMin + (angMax - angMin) * vuFraction (db); }
    inline float grAngle (float grDb)
    {
        const float a0 = vuAngle (0.0f);
        return a0 - (a0 - angMin) * juce::jlimit (0.0f, 20.0f, grDb) / 20.0f;
    }
}
