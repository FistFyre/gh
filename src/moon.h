#pragma once
#include <cmath>
#include <ctime>

// ============================================================================
// Moon phase computation
// Algorithm: Julian Day Number + known new moon reference
// Reference new moon: 2000-01-06 18:14 UTC  →  JD 2451550.259722
// Synodic period: 29.53058867 days
// ============================================================================

static constexpr double SYNODIC_PERIOD = 29.53058867;

struct MoonPhase
{
    double         age;          // days since last new moon  [0, SYNODIC_PERIOD)
    double         phaseFrac;    // age / SYNODIC_PERIOD       [0, 1)
    double         illumination; // fraction of disk lit       [0, 1]
    int            phaseIndex;   // 0–7 (see PHASE_NAMES below)
    const wchar_t* phaseName;    // e.g. "Waxing Gibbous"
    const wchar_t* nextMainName; // e.g. "Full Moon"
    double         daysToNext;   // days until next main phase (new/quarter/full)
};

inline MoonPhase ComputeMoonPhase()
{
    static constexpr double KNOWN_NM_JD = 2451550.259722; // 2000-01-06 18:14 UTC
    static constexpr double UNIX_EPOCH_JD = 2440589;      // 1970-01-01 00:00 UTC
    static constexpr double PI = 3.14159265358979323846;

    std::time_t raw = std::time(nullptr);
    struct tm gmt{};
    gmtime_s(&gmt, &raw);
    std::time_t now = _mkgmtime(&gmt);
    double jd = UNIX_EPOCH_JD + static_cast<double>(now) / 86400.0;

    double age = std::fmod(jd - KNOWN_NM_JD, SYNODIC_PERIOD);
    if (age < 0.0)
    {
        age += SYNODIC_PERIOD;
    }

    double phaseFrac = age / SYNODIC_PERIOD;

    // Illuminated fraction: 0 at new moon, 1 at full moon
    double illumination = (1.0 - std::cos(phaseFrac * 2.0 * PI)) / 2.0;

    // Eight named phases, each spanning 1/8 of the cycle.
    // We shift by half a sector so sector 0 is centred on new moon (phaseFrac ≈ 0).
    static const wchar_t* PHASE_NAMES[8] = {
        L"New Moon",        L"Waxing Crescent",
        L"First Quarter",   L"Waxing Gibbous",
        L"Full Moon",       L"Waning Gibbous",
        L"Last Quarter",    L"Waning Crescent"
    };
    double shifted = std::fmod(phaseFrac + 1.0 / 16.0, 1.0);
    int phaseIndex = static_cast<int>(shifted * 8.0) % 8;

    // Four main phases (quarters) at 0, 0.25, 0.5, 0.75 of the cycle
    static const wchar_t* MAIN_NAMES[4] = {
        L"New Moon", L"First Quarter", L"Full Moon", L"Last Quarter"
    };
    int curQuarter = static_cast<int>(phaseFrac * 4.0);
    int nextQi = (curQuarter + 1) % 4;

    double nextFrac = (curQuarter + 1) / 4.0;
    if (nextFrac >= 1.0)
    {
        nextFrac -= 1.0;
    }

    double daysToNext;
    if (nextFrac > phaseFrac)
    {
        daysToNext = (nextFrac - phaseFrac) * SYNODIC_PERIOD;
    }
    else
    {
        daysToNext = (1.0 + nextFrac - phaseFrac) * SYNODIC_PERIOD;
    }

    MoonPhase m;
    m.age = age;
    m.phaseFrac = phaseFrac;
    m.illumination = illumination;
    m.phaseIndex = phaseIndex;
    m.phaseName = PHASE_NAMES[phaseIndex];
    m.nextMainName = MAIN_NAMES[nextQi];
    m.daysToNext = daysToNext;
    return m;
}
