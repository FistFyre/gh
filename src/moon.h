#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <ctime>

// ============================================================================
// Moon phase computation for Project: Gorgon
//
// Algorithm mirrors PgMoon/MoonPhaseCalculator (github.com/dlebansais/PgMoon):
//   • Phase angles via Jean Meeus, "Astronomical Algorithms" 2nd ed., Ch. 48
//     (same formula used in Moon.cs)
//   • Phase is determined at midnight US Eastern Time, exactly as the game does
//   • The 8 named phases are assigned by the day-bucket logic in Calculator.cs:
//     each main phase (New/FQ/Full/LQ) spans exactly 3 days around its peak;
//     remaining days are classified by illumination + waxing/waning state
// ============================================================================

static constexpr double SYNODIC_PERIOD = 29.53058867;

struct MoonPhase
{
    double         age;              // days since last new moon (derived from phaseFrac)
    double         phaseFrac;        // 0=new moon, 0.5=full moon  [0, 1)
    double         illumination;     // fraction of disk lit        [0, 1]
    int            phaseIndex;       // 0–7, game phase (Eastern-midnight based)
    const wchar_t* phaseName;        // e.g. "Waxing Gibbous"
    const wchar_t* nextMainName;     // e.g. "Full Moon"
    double         daysToNext;       // days until next main phase (game calendar)
    double         hoursToNextPhase; // hours until next of the 8 named phases
    const wchar_t* nextPhaseName;    // name of the next 8-phase
    std::time_t    nextFullMoonTime; // UTC time_t of the first day of the next full moon
};

// ── Constants ────────────────────────────────────────────────────────────────

static constexpr double PG_PI           = 3.14159265358979323846;
static constexpr double UNIX_EPOCH_JD   = 2440587.5; // 1970-01-01 00:00 UTC (JD starts at noon → midnight = .5)

// ── Phase index constants (match mushroom-data order in main.cpp) ─────────────

static constexpr int PH_NEW_MOON        = 0;
static constexpr int PH_WAXING_CRESCENT = 1;
static constexpr int PH_FIRST_QUARTER   = 2;
static constexpr int PH_WAXING_GIBBOUS  = 3;
static constexpr int PH_FULL_MOON       = 4;
static constexpr int PH_WANING_GIBBOUS  = 5;
static constexpr int PH_LAST_QUARTER    = 6;
static constexpr int PH_WANING_CRESCENT = 7;

static const wchar_t* PG_PHASE_NAMES[8] = {
    L"New Moon",       L"Waxing Crescent",
    L"First Quarter",  L"Waxing Gibbous",
    L"Full Moon",      L"Waning Gibbous",
    L"Last Quarter",   L"Waning Crescent"
};

static inline bool PgIsMainPhase(int idx)
{
    return idx == PH_NEW_MOON     || idx == PH_FIRST_QUARTER ||
           idx == PH_FULL_MOON    || idx == PH_LAST_QUARTER;
}

static inline const wchar_t* PgMainPhaseName(int idx)
{
    switch (idx)
    {
    case PH_NEW_MOON:       return L"New Moon";
    case PH_FIRST_QUARTER:  return L"First Quarter";
    case PH_FULL_MOON:      return L"Full Moon";
    case PH_LAST_QUARTER:   return L"Last Quarter";
    default:                return L"";
    }
}

// ── Meeus helpers ─────────────────────────────────────────────────────────────

static inline double PgDegToRad(double d) { return d * PG_PI / 180.0; }

// Julian Day Number from a Gregorian calendar date (Meeus, p. 61).
// 'day' may carry a fractional part for intra-day precision.
static inline double PgToJD(int year, int month, double day)
{
    if (month <= 2) { year--; month += 12; }
    int a = (int)std::floor(year / 100.0);
    int b = 2 - a + (int)std::floor(a / 4.0);
    return std::floor(365.25  * (year  + 4716))
         + std::floor(30.6001 * (month + 1))
         + day + b - 1524.5;
}

// Phase angle i (degrees) at Julian Day jd.
// Identical to the formula in Moon.cs (PgMoon).
static inline double PgPhaseAngle(double jd)
{
    double t      = (jd - 2451545.0) / 36525.0;
    double D      = 297.8501921 + t * (445267.1114034  + t * (-0.0018819  + t * ( 1.0 / 545868.0  - t / 113065000.0)));
    double M      = 357.5291092 + t * ( 35999.0502909  + t * (-0.0001536  + t /  24490000.0));
    double Mprime = 134.9633964 + t * (477198.8675055  + t * ( 0.0087414  + t * ( 1.0 /  69699.0  - t /  14712000.0)));
    return 180.0
        - D
        - 6.289 * std::sin(PgDegToRad(Mprime))
        + 2.100 * std::sin(PgDegToRad(M))
        - 1.274 * std::sin(PgDegToRad(2.0*D - Mprime))
        - 0.658 * std::sin(PgDegToRad(2.0*D))
        - 0.214 * std::sin(PgDegToRad(2.0*Mprime))
        - 0.110 * std::sin(PgDegToRad(D));
}

// Illuminated fraction of the disk.  0 = new moon, 1 = full moon.
static inline double PgIllumination(double phaseAngle)
{
    return (1.0 + std::cos(PgDegToRad(phaseAngle))) * 0.5;
}

// True when the moon is waning (shrinking).
static inline bool PgIsWaning(double phaseAngle)
{
    double s = std::sin(PgDegToRad(phaseAngle));
    if (s < 0.0) return true;
    if (s > 0.0) return false;
    return std::cos(PgDegToRad(phaseAngle)) > 0.0;
}

// Continuous phaseFrac ∈ [0, 1) from a Meeus phase angle.
// Matches the convention expected by CreateMoonIcon and the phase chart:
//   0.00 → new moon   (icon fully dark)
//   0.25 → 1st quarter
//   0.50 → full moon  (icon fully lit)
//   0.75 → last quarter
static inline double PgPhaseFrac(double phaseAngle)
{
    double ill = PgIllumination(phaseAngle);
    // cos(phaseFrac·2π) = 1 − 2·illumination  (derived from the icon formula)
    double x = std::max(-1.0, std::min(1.0, 1.0 - 2.0 * ill));
    double f = std::acos(x) / (2.0 * PG_PI);   // ∈ [0, 0.5]
    return PgIsWaning(phaseAngle) ? (1.0 - f) : f;
}

// ── US Eastern Time helpers ───────────────────────────────────────────────────

// Cache the Eastern Standard Time zone entry (queried once from the registry).
static DYNAMIC_TIME_ZONE_INFORMATION PgGetEasternTZ()
{
    static DYNAMIC_TIME_ZONE_INFORMATION dtzi = {};
    static bool initialized = false;
    if (!initialized)
    {
        DYNAMIC_TIME_ZONE_INFORMATION tmp = {};
        for (DWORD i = 0; EnumDynamicTimeZoneInformation(i, &tmp) == ERROR_SUCCESS; ++i)
        {
            if (wcscmp(tmp.TimeZoneKeyName, L"Eastern Standard Time") == 0)
            {
                dtzi = tmp;
                break;
            }
        }
        initialized = true;
    }
    return dtzi;
}

static inline FILETIME PgTimeToFT(std::time_t t)
{
    ULARGE_INTEGER uli;
    uli.QuadPart = (ULONGLONG)t * 10000000ULL + 116444736000000000ULL;
    FILETIME ft  = { uli.LowPart, uli.HighPart };
    return ft;
}

static inline std::time_t PgFTToTime(FILETIME ft)
{
    ULARGE_INTEGER uli = { ft.dwLowDateTime, ft.dwHighDateTime };
    return (std::time_t)((uli.QuadPart - 116444736000000000ULL) / 10000000ULL);
}

// UTC time_t → calendar date in US Eastern Time.
static void PgUtcToEasternDate(std::time_t utc, int& year, int& month, int& day)
{
    FILETIME  ft = PgTimeToFT(utc);
    SYSTEMTIME utcST, estST;
    FileTimeToSystemTime(&ft, &utcST);
    DYNAMIC_TIME_ZONE_INFORMATION dtzi = PgGetEasternTZ();
    SystemTimeToTzSpecificLocalTimeEx(&dtzi, &utcST, &estST);
    year  = estST.wYear;
    month = estST.wMonth;
    day   = estST.wDay;
}

// Eastern midnight (year, month, day 00:00:00) → UTC time_t.
static std::time_t PgEasternMidnightToUtc(int year, int month, int day)
{
    SYSTEMTIME estST = {};
    estST.wYear  = (WORD)year;
    estST.wMonth = (WORD)month;
    estST.wDay   = (WORD)day;
    SYSTEMTIME utcST;
    DYNAMIC_TIME_ZONE_INFORMATION dtzi = PgGetEasternTZ();
    TzSpecificLocalTimeToSystemTimeEx(&dtzi, &estST, &utcST);
    FILETIME ft;
    SystemTimeToFileTime(&utcST, &ft);
    return PgFTToTime(ft);
}

// ── Day-window phase assignment (mirrors Calculator.cs) ──────────────────────

struct PgDayInfo
{
    double illumination;
    bool   isWaning;
    int    phaseIndex;   // -1 = unassigned
};

static void PgAssignPhases(PgDayInfo* d, int n)
{
    for (int i = 0; i < n; i++) d[i].phaseIndex = -1;

    // Mark day i and its ±1 neighbours with 'phase' (bounds-checked).
    auto mark = [&](int i, int phase)
    {
        d[i].phaseIndex = phase;
        if (i + 1 < n) d[i + 1].phaseIndex = phase;
        if (i > 0)     d[i - 1].phaseIndex = phase;
    };

    // Full Moon: last waxing day immediately before waning begins
    for (int i = 0; i < n - 1; i++)
        if (!d[i].isWaning && d[i+1].isWaning) { mark(i, PH_FULL_MOON); break; }

    // New Moon: last waning day immediately before waxing begins
    for (int i = 0; i < n - 1; i++)
        if (d[i].isWaning && !d[i+1].isWaning)  { mark(i, PH_NEW_MOON); break; }

    // First Quarter: waxing, illumination crosses 50 % upward
    for (int i = 0; i < n - 1; i++)
        if (!d[i].isWaning && !d[i+1].isWaning &&
            d[i].illumination <= 0.5 && d[i+1].illumination > 0.5)
            { mark(i, PH_FIRST_QUARTER); break; }

    // Last Quarter: waning, illumination crosses 50 % downward
    for (int i = 0; i < n - 1; i++)
        if (d[i].isWaning && d[i+1].isWaning &&
            d[i].illumination >= 0.5 && d[i+1].illumination < 0.5)
            { mark(i, PH_LAST_QUARTER); break; }

    // Remaining unassigned days get their phase from illumination + direction
    for (int i = 0; i < n; i++)
    {
        if (d[i].phaseIndex >= 0) continue;
        if (!d[i].isWaning)
            d[i].phaseIndex = (d[i].illumination <= 0.5) ? PH_WAXING_CRESCENT : PH_WAXING_GIBBOUS;
        else
            d[i].phaseIndex = (d[i].illumination >= 0.5) ? PH_WANING_GIBBOUS  : PH_WANING_CRESCENT;
    }
}

// ── Main entry point ──────────────────────────────────────────────────────────

inline MoonPhase ComputeMoonPhase()
{
    std::time_t now = std::time(nullptr);

    // ── 1. Today's date in US Eastern Time ───────────────────────────────────
    int estYear, estMonth, estDay;
    PgUtcToEasternDate(now, estYear, estMonth, estDay);

    std::time_t todayMidnightUtc = PgEasternMidnightToUtc(estYear, estMonth, estDay);

    // ── 2. Build a 35-day window ─────────────────────────────────────────────
    // window[0] = today−1, window[1] = today, window[2] = today+1, …, window[34] = today+33
    // 35 days guarantees the next full moon is always within the window.
    static constexpr int WINDOW = 35;
    PgDayInfo days[WINDOW];

    double jdBase = PgToJD(estYear, estMonth, estDay); // JD at midnight today (Eastern)
    for (int i = 0; i < WINDOW; i++)
    {
        double pa          = PgPhaseAngle(jdBase + (i - 1));
        days[i].illumination = PgIllumination(pa);
        days[i].isWaning     = PgIsWaning(pa);
    }
    PgAssignPhases(days, WINDOW);

    int todayPhase = days[1].phaseIndex; // window[1] = today

    // ── 3. Continuous values at the exact current moment ─────────────────────
    double jdNow     = UNIX_EPOCH_JD + (double)now / 86400.0;
    double angleNow  = PgPhaseAngle(jdNow);
    double illNow    = PgIllumination(angleNow);
    double pfNow     = PgPhaseFrac(angleNow);

    // ── 4. Next change among the 8 phases ────────────────────────────────────
    int nextPhaseOffset = -1;
    int nextPhaseIdx    = -1;
    for (int i = 2; i < WINDOW; i++)
    {
        if (days[i].phaseIndex != todayPhase)
        {
            nextPhaseOffset = i - 1;   // days from today
            nextPhaseIdx    = days[i].phaseIndex;
            break;
        }
    }
    double hoursToNextPhase = 0.0;
    if (nextPhaseOffset > 0)
    {
        std::time_t changeUtc = todayMidnightUtc + (std::time_t)nextPhaseOffset * 86400;
        double secs = std::difftime(changeUtc, now);
        if (secs < 0.0) secs = 0.0;
        hoursToNextPhase = secs / 3600.0;
    }

    // ── 5. Next main phase (New / First Quarter / Full / Last Quarter) ────────
    // Scan forward for the first day that belongs to a main phase different
    // from today's.  If today is itself a main phase the scan naturally skips
    // trailing days of the same 3-day block.
    int nextMainOffset   = -1;
    int nextMainPhaseIdx = -1;
    for (int i = 2; i < WINDOW; i++)
    {
        if (PgIsMainPhase(days[i].phaseIndex) && days[i].phaseIndex != todayPhase)
        {
            nextMainOffset   = i - 1;
            nextMainPhaseIdx = days[i].phaseIndex;
            break;
        }
    }
    double daysToNext = 0.0;
    if (nextMainOffset > 0)
    {
        std::time_t mainUtc = todayMidnightUtc + (std::time_t)nextMainOffset * 86400;
        daysToNext = std::difftime(mainUtc, now) / 86400.0;
    }

    // ── 6. Next full moon ─────────────────────────────────────────────────────
    // First day of the next Full-Moon block (strictly after today).
    std::time_t nextFullMoonTime = 0;
    for (int i = 2; i < WINDOW; i++)
    {
        if (days[i].phaseIndex == PH_FULL_MOON && days[i-1].phaseIndex != PH_FULL_MOON)
        {
            nextFullMoonTime = todayMidnightUtc + (std::time_t)(i - 1) * 86400;
            break;
        }
    }

    // ── 7. Pack and return ────────────────────────────────────────────────────
    MoonPhase m;
    m.age              = pfNow * SYNODIC_PERIOD;
    m.phaseFrac        = pfNow;
    m.illumination     = illNow;
    m.phaseIndex       = todayPhase;
    m.phaseName        = PG_PHASE_NAMES[todayPhase];
    m.nextMainName     = (nextMainPhaseIdx >= 0) ? PgMainPhaseName(nextMainPhaseIdx) : L"";
    m.daysToNext       = daysToNext;
    m.hoursToNextPhase = hoursToNextPhase;
    m.nextPhaseName    = (nextPhaseIdx >= 0) ? PG_PHASE_NAMES[nextPhaseIdx]
                                             : PG_PHASE_NAMES[(todayPhase + 1) % 8];
    m.nextFullMoonTime = nextFullMoonTime;
    return m;
}
