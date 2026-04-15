#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cmath>
#include <cstring>
#include <algorithm>

// ============================================================================
// CreateMoonIcon
//
// Renders a 32x32 RGBA moon-phase icon using GDI DIB sections.
// The terminator (lit/dark boundary) is derived from the phase fraction:
//
//   phaseFrac == 0.00  → New Moon  (dark disk, invisible sliver)
//   phaseFrac == 0.25  → First Quarter (right half lit)
//   phaseFrac == 0.50  → Full Moon  (fully lit)
//   phaseFrac == 0.75  → Last Quarter (left half lit)
//
// Waxing (0–0.5): right side is illuminated.
// Waning (0.5–1): left side is illuminated.
//
// The terminator is an ellipse whose x-semi-axis = R·|cos(phaseAngle)|.
// At any row y, the terminator x position is:
//   xTerm = cos(phaseAngle) · sqrt(R² − y²)
//
// Caller owns the returned HICON and must DestroyIcon() it.
// ============================================================================
inline HICON CreateMoonIcon(double phaseFrac)
{
    static constexpr int   SIZE = 32;
    static constexpr float R    = 13.5f;  // disk radius (px)
    static constexpr float CX   = 15.5f;  // centre x
    static constexpr float CY   = 15.5f;  // centre y
    static constexpr float PI   = 3.14159265f;

    // BGRA pixel buffer (matches BITMAPV5HEADER mask convention below)
    DWORD pixels[SIZE * SIZE] = {};

    float phaseAngle = static_cast<float>(phaseFrac) * 2.0f * PI;
    float cosPa = std::cosf(phaseAngle);
    bool  waxing = (phaseFrac < 0.5);

    for (int y = 0; y < SIZE; ++y)
    {
        for (int x = 0; x < SIZE; ++x)
        {
            float dx   = static_cast<float>(x) - CX;
            float dy   = static_cast<float>(y) - CY;
            float dist = std::sqrtf(dx*dx + dy*dy);

            // Smooth alpha at the edge (1 px anti-aliasing band)
            float alpha = 1.0f - (dist - (R - 0.5f));
            alpha = std::fmaxf(0.0f, std::fminf(1.0f, alpha));
            if (alpha <= 0.0f)
            {
                continue;
            }

            // Terminator position at this row
            float yNorm = dy / R;
            float xSpan = std::sqrtf(std::fmaxf(0.0f, 1.0f - yNorm * yNorm)) * R;

            bool lit;
            if (waxing)
            {
                // Terminator sweeps right→left as phase goes 0→0.5
                // At new moon  (cos=+1): xTerm = +xSpan → nothing lit
                // At 1st qtr   (cos= 0): xTerm =  0     → right half lit
                // Near full    (cos=-1): xTerm = -xSpan → all lit
                float xTerm = cosPa * xSpan;
                lit = (dx >= xTerm);
            }
            else
            {
                // Terminator sweeps left→right as phase goes 0.5→1
                // Near full    (cos=-1): xTerm = +xSpan → all lit
                // At last qtr  (cos= 0): xTerm =  0     → left half lit
                // At new moon  (cos=+1): xTerm = -xSpan → nothing lit
                float xTerm = -cosPa * xSpan;
                lit = (dx <= xTerm);
            }

            BYTE r, g, b;
            if (lit)
            {
                // Warm ivory – the sunlit face of the moon
                r = 255; g = 252; b = 210;
            }
            else
            {
                // Deep indigo-dark – night side, visible against black taskbar
                r = 40; g = 42; b = 68;
            }

            BYTE a = static_cast<BYTE>(alpha * 255.0f + 0.5f);

            // Pack as 0xAARRGGBB (BITMAPV5 with R=0x00FF0000, G=0x0000FF00, B=0x000000FF)
            pixels[y * SIZE + x] = (static_cast<DWORD>(a) << 24)
                                 | (static_cast<DWORD>(r) << 16)
                                 | (static_cast<DWORD>(g) <<  8)
                                 |  static_cast<DWORD>(b);
        }
    }

    // Build a 32-bpp DIB section with alpha channel
    BITMAPV5HEADER bi = {};
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = SIZE;
    bi.bV5Height = -SIZE;         // negative = top-down scan order
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    HDC hdc = GetDC(nullptr);
    void* pbits = nullptr;
    HBITMAP hbm = CreateDIBSection(hdc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &pbits, nullptr, 0);
    ReleaseDC(nullptr, hdc);
    if (!hbm)
    {
        return nullptr;
    }

    std::memcpy(pbits, pixels, sizeof(pixels));

    // An all-zero mask means "use alpha channel from hbmColor"
    HBITMAP hMask = CreateBitmap(SIZE, SIZE, 1, 1, nullptr);

    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmColor = hbm;
    ii.hbmMask = hMask;
    HICON hIcon = CreateIconIndirect(&ii);

    DeleteObject(hbm);
    DeleteObject(hMask);
    return hIcon;
}
