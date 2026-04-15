// moonphase – Windows 11 system-tray application
// Shows a rendered moon-phase icon; hovering reveals the phase name and
// the number of days until the next main phase.
//
// Left-double-click: moon-phase chart window
// Right-click:       context menu  (Exit)

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE

#include <windows.h>
#include <shellapi.h>
#include <strsafe.h>

#include "moon.h"
#include "icon.h"

// ============================================================================
// Mushroom farming data (indexed by MoonPhase::phaseIndex 0–7)
// Source: in-game moon-phase farming chart
// ============================================================================
struct MushroomData
{
    const wchar_t* robustly; // grows well this phase
    const wchar_t* poorly;   // grows poorly this phase
};

static const MushroomData MUSHROOM_DATA[8] = {
    // 0: New Moon
    { L"Boletus, Charged, Goblin, Blusher, Granamurch, Black Foot, Ghost",
      L"Field, Coral, Groxmax, Pixie's" },
    // 1: Waxing Crescent
    { L"Mycena, Iocaline, Fly Amanita, Blood, Wizard's",
      L"Boletus, Blusher, Blastcap, Porcini, Granamurch" },
    // 2: First Quarter
    { L"Mycena, Coral, Iocaline, Pixie's, Wizard's",
      L"Field, Blood, Groxmax, False Agaric" },
    // 3: Waxing Gibbous
    { L"Field, Coral, Groxmax, Pixie's, Charged",
      L"Parasol, Milk Cap, Blastcap, Porcini, Fly Amanita" },
    // 4: Full Moon
    { L"Parasol, Fly Amanita, Milk Cap, Granamurch, Blastcap, Ghost, Porcini",
      L"Boletus, Goblin, Mycena, Iocaline, Blood, Black Foot, Charged, Wizard's" },
    // 5: Waning Gibbous
    { L"Boletus, Blusher, Blastcap, Porcini",
      L"Mycena, Goblin, Iocaline, Blood, Fly Amanita, Wizard's, Ghost" },
    // 6: Last Quarter
    { L"Field, Blood, Groxmax, False Agaric",
      L"Mycena, Coral, Iocaline, Pixie's, Wizard's" },
    // 7: Waning Crescent
    { L"Parasol, Milk Cap, False Agaric, Black Foot",
      L"Coral, Groxmax, Pixie's, Charged, Ghost" },
};

// ============================================================================
// Per-mushroom details – level requirement and best growing substrate.
// "level = 0" encodes N/A.  Keys match the short names used in MUSHROOM_DATA.
// ============================================================================
struct MushroomDetails
{
    const wchar_t* key;
    int            level;
    const wchar_t* veryWellIn;
};

static const MushroomDetails MUSHROOM_DETAILS[] = {
    { L"Mycena", 5, L"Limbs" },
    { L"Boletus", 10, L"Exotic" },
    { L"Goblin", 12, L"Exotic" },
    { L"Field", 15, L"Organs" },
    { L"Blusher", 20, L"Exotic" },
    { L"Milk Cap", 25, L"Organs" },
    { L"Blood", 30, L"Limbs" },
    { L"Blastcap", 33, L"Organs" },
    { L"Iocaline", 40, L"Limbs" },
    { L"Coral", 40, L"Limbs" },
    { L"Groxmax", 47, L"Organs" },
    { L"Porcini", 55, L"Exotic" },
    { L"False Agaric", 57, L"Limbs" },
    { L"Black Foot", 64, L"Exotic" },
    { L"Pixie's", 70, L"Organs" },
    { L"Wizard's", 75, L"Organs" },
    { L"Fly Amanita", 77, L"Organs" },
    { L"Charged", 80, L"Exotic" },
    { L"Granamurch", 85, L"Limbs" },
    { L"Ghost", 90, L"Meat" },
    { L"Parasol", 0, L"Organs" },
};

static const MushroomDetails* FindMushroomDetails(const wchar_t* key)
{
    for (const auto& d : MUSHROOM_DETAILS)
    {
        if (wcscmp(d.key, key) == 0)
        {
            return &d;
        }
    }
    return nullptr;
}

// ============================================================================
// Constants
// ============================================================================
static constexpr UINT WM_TRAYICON   = WM_USER + 1;
static constexpr UINT IDM_EXIT      = 1001;
static constexpr UINT IDM_STARTUP   = 1002;
static constexpr UINT TIMER_REFRESH = 1;
static constexpr UINT REFRESH_MS    = 10 * 60 * 1000; // refresh every 10 min
static constexpr int  POPUP_W       = 380; // client width of the hover popup (px)
static constexpr int  POPUP_PAD     = 10;  // inner padding

// ============================================================================
// Globals
// ============================================================================
static NOTIFYICONDATAW g_nid = {};
static HICON g_hIcon = nullptr;
static UINT g_taskbarMsg = 0; // "TaskbarCreated" registered message
static HINSTANCE g_hInst = nullptr;
static HWND g_hPopup = nullptr;
static MoonPhase g_popupPhase = {};

// ============================================================================
// Helpers
// ============================================================================
static void BuildTooltip(wchar_t* buf, int cch, const MoonPhase& m)
{
    if (m.daysToNext < 1.0)
    {
        StringCchPrintfW(buf, cch, L"%s  \u2022  %.0f hours until %s", m.phaseName, m.daysToNext * 24.0, m.nextMainName);
    }
    else
    {
        StringCchPrintfW(buf, cch, L"%s  \u2022  %.1f days until %s", m.phaseName, m.daysToNext, m.nextMainName);
    }
}

// ============================================================================
// Tray icon management
// ============================================================================
static void TrayAdd(HWND hwnd)
{
    MoonPhase m = ComputeMoonPhase();

    if (g_hIcon)
    {
        DestroyIcon(g_hIcon);
        g_hIcon = nullptr;
    }
    g_hIcon = CreateMoonIcon(m.phaseFrac);

    g_nid = {};
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = g_hIcon;

    BuildTooltip(g_nid.szTip, ARRAYSIZE(g_nid.szTip), m);
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    // Opt into NOTIFYICON_VERSION_4 so mouse coordinates arrive in wParam
    g_nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_nid);
}

static void TrayUpdate()
{
    MoonPhase m = ComputeMoonPhase();

    if (g_hIcon)
    {
        DestroyIcon(g_hIcon);
        g_hIcon = nullptr;
    }
    g_hIcon = CreateMoonIcon(m.phaseFrac);

    g_nid.uFlags = NIF_ICON | NIF_TIP;
    g_nid.hIcon = g_hIcon;
    BuildTooltip(g_nid.szTip, ARRAYSIZE(g_nid.szTip), m);

    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void TrayRemove()
{
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    if (g_hIcon)
    {
        DestroyIcon(g_hIcon);
        g_hIcon = nullptr;
    }
}

// ============================================================================
// Startup registry helpers (HKCU Run key, no admin required)
// ============================================================================
static const wchar_t STARTUP_SUBKEY[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t STARTUP_VALUE[]  = L"MoonPhase";

static bool IsStartupEnabled()
{
    HKEY hk;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, STARTUP_SUBKEY, 0, KEY_READ, &hk) != ERROR_SUCCESS)
    {
        return false;
    }
    bool found = RegQueryValueExW(hk, STARTUP_VALUE, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    RegCloseKey(hk);
    return found;
}

static void SetStartupEnabled(bool enable)
{
    HKEY hk;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, STARTUP_SUBKEY, 0, KEY_WRITE, &hk) != ERROR_SUCCESS)
    {
        return;
    }

    if (enable)
    {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        RegSetValueExW(hk, STARTUP_VALUE, 0, REG_SZ, reinterpret_cast<const BYTE*>(path), static_cast<DWORD>((wcslen(path) + 1) * sizeof(wchar_t)));
    }
    else
    {
        RegDeleteValueW(hk, STARTUP_VALUE);
    }
    RegCloseKey(hk);
}

// ============================================================================
// Right-click context menu
// ============================================================================
static void ShowContextMenu(HWND hwnd, int x, int y)
{
    bool startup = IsStartupEnabled();
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING | (startup ? MF_CHECKED : 0u), IDM_STARTUP, L"Run at Windows start");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"Exit Moon Phase");
    SetForegroundWindow(hwnd);
    TrackPopupMenuEx(hMenu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN | TPM_LEFTBUTTON, x, y, hwnd, nullptr);
    DestroyMenu(hMenu);
}

// ============================================================================
// Custom hover popup – GDI-rendered, supports colored/bold text
// ============================================================================

// Create normal + bold variants of the system message font.
// Caller must DeleteObject() both handles.
static void CreatePopupFonts(HFONT* phNorm, HFONT* phBold)
{
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    *phNorm = CreateFontIndirectW(&ncm.lfMessageFont);
    ncm.lfMessageFont.lfWeight = FW_BOLD;
    *phBold = CreateFontIndirectW(&ncm.lfMessageFont);
}

// Draw (paint=true) or measure (paint=false) a block of word-wrapped text.
// Returns the height of the text in logical pixels.
static int TextBlock(HDC hdc, int x, int y, int w, const wchar_t* text, COLORREF col, HFONT font, bool paint)
{
    HFONT prev = (HFONT)SelectObject(hdc, font);
    SetTextColor(hdc, col);
    RECT r = { x, y, x + w, y + 2000 };
    UINT flags = DT_LEFT | DT_WORDBREAK | (paint ? 0u : DT_CALCRECT);
    int h = DrawTextW(hdc, text, -1, &r, flags);
    SelectObject(hdc, prev);
    return h;
}

// Draw (or measure) a comma-separated mushroom list, one entry per line,
// with level and substrate appended as "(level, substrate)".
// Returns total height consumed.
static int DrawMushroomLines(HDC hdc, int x, int y, int w, const wchar_t* csv, COLORREF col, HFONT font, bool paint)
{
    wchar_t buf[512];
    StringCchCopyW(buf, ARRAYSIZE(buf), csv);

    HFONT prev = (HFONT)SelectObject(hdc, font);
    SetTextColor(hdc, col);
    SetBkMode(hdc, TRANSPARENT);

    TEXTMETRICW tm;
    GetTextMetricsW(hdc, &tm);
    int lineH = tm.tmHeight + 2;
    int totalH = 0;

    wchar_t* ctx = nullptr;
    wchar_t* tok = wcstok_s(buf, L",", &ctx);
    while (tok)
    {
        while (*tok == L' ')
        {
            tok++;
        }

        const MushroomDetails* det = FindMushroomDetails(tok);
        wchar_t line[80];
        if (det)
        {
            StringCchPrintfW(line, ARRAYSIZE(line), L"%s (%d, %s)", tok, det->level, det->veryWellIn);
        }
        else
        {
            StringCchCopyW(line, ARRAYSIZE(line), tok);
        }

        if (paint)
        {
            TextOutW(hdc, x, y + totalH, line, (int)wcslen(line));
        }
        totalH += lineH;
        tok = wcstok_s(nullptr, L",", &ctx);
    }

    SelectObject(hdc, prev);
    return totalH;
}

// Paint or measure the popup content. Returns the total client height needed.
static int LayoutPopup(HDC hdc, bool paint, const MoonPhase& m)
{
    const MushroomData& shrooms = MUSHROOM_DATA[m.phaseIndex];
    const int iw = POPUP_W - POPUP_PAD * 2; // inner width

    HFONT hNorm, hBold;
    CreatePopupFonts(&hNorm, &hBold);
    SetBkMode(hdc, TRANSPARENT);

    int y = POPUP_PAD;

    // Phase name + days-to-next  (bold, black)
    wchar_t header[128];
    if (m.daysToNext < 1.0)
    {
        StringCchPrintfW(header, ARRAYSIZE(header), L"%s  \u2022  %.0f hours until %s", m.phaseName, m.daysToNext * 24.0, m.nextMainName);
    }
    else
    {
        StringCchPrintfW(header, ARRAYSIZE(header), L"%s  \u2022  %.1f days until %s", m.phaseName, m.daysToNext, m.nextMainName);
    }
    y += TextBlock(hdc, POPUP_PAD, y, iw, header, RGB(0, 0, 0), hBold, paint) + 4;

    // Age / illumination  (normal, dark grey)
    wchar_t info[128];
    StringCchPrintfW(info, ARRAYSIZE(info), L"Age: %.1f / %.1f days  \u2022  Illumination: %.0f%%", m.age, SYNODIC_PERIOD, m.illumination * 100.0);
    y += TextBlock(hdc, POPUP_PAD, y, iw, info, RGB(90, 90, 90), hNorm, paint) + 8;

    // Separator line
    if (paint)
    {
        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(210, 210, 210));
        HPEN hOld = (HPEN)SelectObject(hdc, hPen);
        MoveToEx(hdc, POPUP_PAD, y, nullptr);
        LineTo(hdc, POPUP_W - POPUP_PAD, y);
        SelectObject(hdc, hOld);
        DeleteObject(hPen);
    }
    y += 8;

    // Robust mushrooms – arrow header, then one per line with (level, substrate)
    y += TextBlock(hdc, POPUP_PAD, y, iw, L"\u2191", RGB(0, 140, 0), hBold, paint) + 1;
    y += DrawMushroomLines(hdc, POPUP_PAD + 12, y, iw - 12, shrooms.robustly, RGB(0, 140, 0), hBold, paint) + 6;

    // Poor mushrooms – arrow header, then one per line with (level, substrate)
    y += TextBlock(hdc, POPUP_PAD, y, iw, L"\u2193", RGB(190, 0, 0), hNorm, paint) + 1;
    y += DrawMushroomLines(hdc, POPUP_PAD + 12, y, iw - 12, shrooms.poorly, RGB(190, 0, 0), hNorm, paint) + POPUP_PAD;

    DeleteObject(hNorm);
    DeleteObject(hBold);
    return y;
}

static LRESULT CALLBACK PopupWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1; // prevent flicker; background drawn in WM_PAINT

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
        LayoutPopup(hdc, true, g_popupPhase);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void ShowPopup(const MoonPhase& m)
{
    g_popupPhase = m;

    // Measure required height using a temporary memory DC
    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    ReleaseDC(nullptr, hdcScreen);
    int clientH = LayoutPopup(hdcMem, false, m);
    DeleteDC(hdcMem);

    // Account for window border when sizing
    RECT adj = { 0, 0, POPUP_W, clientH };
    AdjustWindowRectEx(&adj, WS_POPUP | WS_BORDER, FALSE, WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
    int ww = adj.right - adj.left;
    int wh = adj.bottom - adj.top;

    // Position above the work area (above the taskbar), flush to right edge
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int x = wa.right - ww - 4;
    int y = wa.bottom - wh - 4;

    if (!g_hPopup)
    {
        g_hPopup = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"MoonPhasePopup", nullptr, WS_POPUP | WS_BORDER, x, y, ww, wh, nullptr, nullptr, g_hInst, nullptr);
    }
    else
    {
        SetWindowPos(g_hPopup, HWND_TOPMOST, x, y, ww, wh, SWP_NOACTIVATE);
    }
    ShowWindow(g_hPopup, SW_SHOWNOACTIVATE);
    UpdateWindow(g_hPopup);
}

static void HidePopup()
{
    if (g_hPopup)
    {
        ShowWindow(g_hPopup, SW_HIDE);
    }
}

// ============================================================================
// Moon phase chart – full diagram drawn with GDI primitives
// ============================================================================
static HWND g_hChart = nullptr;

// Phase names indexed by phaseIndex (same order as MUSHROOM_DATA)
static const wchar_t* const CHART_PHASE_NAMES[8] = {
    L"New Moon",       L"Waxing Crescent", L"First Quarter",  L"Waxing Gibbous",
    L"Full Moon",      L"Waning Gibbous",  L"Last Quarter",   L"Waning Crescent"
};

// Draw a comma-separated list of names as a centered vertical column at (cx,cy).
static void DrawNameColumn(HDC hdc, const wchar_t* csv, int cx, int cy, HFONT font, COLORREF col)
{
    wchar_t buf[256];
    StringCchCopyW(buf, ARRAYSIZE(buf), csv);

    HFONT prev = (HFONT)SelectObject(hdc, font);
    SetTextColor(hdc, col);
    SetBkMode(hdc, TRANSPARENT);

    TEXTMETRICW tm;
    GetTextMetricsW(hdc, &tm);
    int lineH = tm.tmHeight + 1;

    // Count tokens
    int n = 1;
    for (const wchar_t* p = buf; *p; ++p)
    {
        if (*p == L',')
        {
            ++n;
        }
    }

    int y = cy - (n * lineH) / 2;
    wchar_t* ctx = nullptr;
    wchar_t* tok = wcstok_s(buf, L",", &ctx);
    while (tok)
    {
        while (*tok == L' ')
        {
            tok++; // trim leading space after comma
        }
        int len = (int)wcslen(tok);
        SIZE sz;
        GetTextExtentPoint32W(hdc, tok, len, &sz);
        TextOutW(hdc, cx - sz.cx / 2, y, tok, len);
        y += lineH;
        tok = wcstok_s(nullptr, L",", &ctx);
    }
    SelectObject(hdc, prev);
}

static void DrawMoonChart(HDC hdc, RECT rc, int currentPhaseIndex)
{
    static const double PI = 3.14159265358979;

    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    int cx = rc.left + w / 2;
    int cy = rc.top + h / 2;
    int R = min(w, h) * 40 / 100;   // outer radius
    int Rm = R * 57 / 100;          // outer/inner ring boundary

    // ---- Background ----
    HBRUSH bgBr = CreateSolidBrush(RGB(196, 162, 112));
    FillRect(hdc, &rc, bgBr);
    DeleteObject(bgBr);
    SetBkMode(hdc, TRANSPARENT);

    // ---- Geometry helpers ----
    // angle: degrees clockwise from north; x = cx + r*sin(a), y = cy - r*cos(a)

    // ---- Pens / brushes ----
    HPEN penBlk = CreatePen(PS_SOLID, 2, RGB(30, 30, 30));
    HBRUSH brGrn = CreateSolidBrush(RGB(100, 185, 80));
    HBRUSH brRed = CreateSolidBrush(RGB(215, 75, 55));

    // Outer green circle (filled)
    SelectObject(hdc, brGrn);
    SelectObject(hdc, (HPEN)GetStockObject(NULL_PEN));
    Ellipse(hdc, cx-R, cy-R, cx+R+1, cy+R+1);

    // 8 radial dividers to outer edge
    SelectObject(hdc, penBlk);
    for (int i = 0; i < 8; ++i)
    {
        double a = (i * 45.0 + 22.5) * PI / 180.0;
        MoveToEx(hdc, cx, cy, nullptr);
        LineTo(hdc, cx + (int)(R * sin(a)), cy - (int)(R * cos(a)));
    }

    // Inner red circle (covers centre portions of dividers)
    SelectObject(hdc, brRed);
    SelectObject(hdc, (HPEN)GetStockObject(NULL_PEN));
    Ellipse(hdc, cx-Rm, cy-Rm, cx+Rm+1, cy+Rm+1);

    // Re-draw dividers over the inner circle
    SelectObject(hdc, penBlk);
    for (int i = 0; i < 8; ++i)
    {
        double a = (i * 45.0 + 22.5) * PI / 180.0;
        MoveToEx(hdc, cx, cy, nullptr);
        LineTo(hdc, cx + (int)(R * sin(a)), cy - (int)(R * cos(a)));
    }

    // Circle outlines (no fill)
    SelectObject(hdc, (HBRUSH)GetStockObject(NULL_BRUSH));
    Ellipse(hdc, cx-R,  cy-R,  cx+R+1,  cy+R+1);
    Ellipse(hdc, cx-Rm, cy-Rm, cx+Rm+1, cy+Rm+1);

    // ---- Fonts ----
    HFONT hPhaseF  = CreateFontW(-13, 0, 0, 0, FW_BOLD,   FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT hRobustF = CreateFontW(-10, 0, 0, 0, FW_BOLD,   FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT hPoorF   = CreateFontW(-10, 0, 0, 0, FW_BOLD,   FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT hLegF    = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    // ---- Per-section content ----
    // Slot 0 = top = Full Moon (phaseIndex 4), then clockwise.
    // phaseIndex for slot s: (s + 4) % 8
    for (int slot = 0; slot < 8; ++slot)
    {
        int idx = (slot + 4) % 8;
        double ad = slot * 45.0;
        double ar = ad * PI / 180.0;
        double sa = sin(ar), ca = cos(ar);

        // Phase label – centred just outside the outer circle
        {
            int lx = cx + (int)((R + 50) * sa);
            int ly = cy - (int)((R + 50) * ca);
            HFONT prev = (HFONT)SelectObject(hdc, hPhaseF);
            SetTextColor(hdc, RGB(0, 0, 0));
            // Measure height for vertical centering
            RECT mr = { lx - 60, 0, lx + 60, 200 };
            DrawTextW(hdc, CHART_PHASE_NAMES[idx], -1, &mr, DT_CENTER | DT_WORDBREAK | DT_CALCRECT);
            int th = mr.bottom - mr.top;
            RECT dr = { lx - 60, ly - th / 2, lx + 60, ly + th / 2 + 1 };
            DrawTextW(hdc, CHART_PHASE_NAMES[idx], -1, &dr, DT_CENTER | DT_WORDBREAK);
            SelectObject(hdc, prev);
        }

        // Outer ring – robust mushrooms, bold dark-green
        {
            int ox = cx + (int)((R + Rm) / 2 * sa);
            int oy = cy - (int)((R + Rm) / 2 * ca);
            DrawNameColumn(hdc, MUSHROOM_DATA[idx].robustly, ox, oy, hRobustF, RGB(0, 70, 0));
        }

        // Inner ring – poor mushrooms, normal dark-red
        {
            int ix = cx + (int)(Rm * 0.7 * sa);
            int iy = cy - (int)(Rm * 0.7 * ca);
            DrawNameColumn(hdc, MUSHROOM_DATA[idx].poorly, ix, iy, hPoorF, RGB(110, 0, 0));
        }
    }

    // ---- Legend (top-left corner) ----
    {
        HFONT prev = (HFONT)SelectObject(hdc, hLegF);
        SetTextColor(hdc, RGB(0, 0, 0));
        int lx = rc.left + 10, ly = rc.top + 10;

        HBRUSH swBr = CreateSolidBrush(RGB(100, 185, 80));
        RECT sw = { lx, ly + 1, lx + 14, ly + 13 };
        FillRect(hdc, &sw, swBr);
        FrameRect(hdc, &sw, (HBRUSH)GetStockObject(BLACK_BRUSH));
        DeleteObject(swBr);
        TextOutW(hdc, lx + 18, ly, L"Outer Ring: Grows Robustly", 26);

        ly += 16;
        swBr = CreateSolidBrush(RGB(215, 75, 55));
        sw = { lx, ly + 1, lx + 14, ly + 13 };
        FillRect(hdc, &sw, swBr);
        FrameRect(hdc, &sw, (HBRUSH)GetStockObject(BLACK_BRUSH));
        DeleteObject(swBr);
        TextOutW(hdc, lx + 18, ly, L"Inner Ring: Grows Poorly", 24);

        SelectObject(hdc, prev);
    }

    // ---- Current-phase highlight – white wedge outline drawn on top ----
    {
        // Convert phaseIndex to drawing slot (slot 0 = Full Moon at top, CW)
        int slot = (currentPhaseIndex + 4) % 8;
        double startCw = slot * 45.0 - 22.5;   // section start boundary, degrees CW from N
        // AngleArc angles: CCW from east (positive x-axis); clockwise sweep is negative
        auto gdiStart = static_cast<FLOAT>(90.0 - startCw);

        HPEN hWhi = CreatePen(PS_SOLID, 4, RGB(255, 255, 255));
        HPEN hOld = (HPEN)SelectObject(hdc, hWhi);
        SelectObject(hdc, (HBRUSH)GetStockObject(NULL_BRUSH));

        // Path: centre → (implicit line to arc start) → 45° arc → back to centre
        BeginPath(hdc);
        MoveToEx(hdc, cx, cy, nullptr);
        AngleArc(hdc, cx, cy, static_cast<FLOAT>(R), gdiStart, -45.0f);
        LineTo(hdc, cx, cy);
        EndPath(hdc);
        StrokePath(hdc);

        SelectObject(hdc, hOld);
        DeleteObject(hWhi);
    }

    // ---- Donation text (bottom centre) ----
    {
        static const wchar_t* DONATION_TEXT = L"Version 1.0 - Enjoying the tool? In-game donations are always appreciated - FistFyre@Arisetu";
        HFONT prev = (HFONT)SelectObject(hdc, hLegF);
        SetTextColor(hdc, RGB(60, 60, 60));
        SetBkMode(hdc, TRANSPARENT);
        SIZE sz;
        GetTextExtentPoint32W(hdc, DONATION_TEXT, (int)wcslen(DONATION_TEXT), &sz);
        TextOutW(hdc, cx - sz.cx / 2, rc.bottom - sz.cy - 6, DONATION_TEXT, (int)wcslen(DONATION_TEXT));
        SelectObject(hdc, prev);
    }

    // ---- Cleanup ----
    DeleteObject(hPhaseF);
    DeleteObject(hRobustF);
    DeleteObject(hPoorF);
    DeleteObject(hLegF);
    DeleteObject(penBlk);
    DeleteObject(brGrn);
    DeleteObject(brRed);
}

static LRESULT CALLBACK ChartWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_SIZE:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        // Double-buffer to avoid resize flicker
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, bmp);
        DrawMoonChart(memDC, rc, ComputeMoonPhase().phaseIndex);
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBmp);
        DeleteObject(bmp);
        DeleteDC(memDC);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        g_hChart = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void ShowChart()
{
    if (g_hChart)
    {
        ShowWindow(g_hChart, SW_RESTORE);
        SetForegroundWindow(g_hChart);
        return;
    }
    g_hChart = CreateWindowExW(0, L"MoonPhaseChart", L"Mushroom Farming", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 900, 940, nullptr, nullptr, g_hInst, nullptr);
    ShowWindow(g_hChart, SW_SHOW);
    UpdateWindow(g_hChart);
}

// ============================================================================
// Window procedure
// ============================================================================
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // Re-add icon if Explorer/shell restarts
    if (msg == g_taskbarMsg && g_taskbarMsg != 0)
    {
        TrayAdd(hwnd);
        return 0;
    }

    switch (msg)
    {

    case WM_TRAYICON:
        // With NOTIFYICON_VERSION_4:
        //   LOWORD(lParam) = notification code
        //   LOWORD(wParam) = mouse x,  HIWORD(wParam) = mouse y
        switch (LOWORD(lParam))
        {

        case WM_LBUTTONDBLCLK:
            ShowChart();
            break;

        case WM_RBUTTONUP:
        case NIN_KEYSELECT:
            ShowContextMenu(hwnd, LOWORD(wParam), HIWORD(wParam));
            break;

        case NIN_POPUPOPEN:
        {
            MoonPhase m = ComputeMoonPhase();
            ShowPopup(m);
            break;
        }

        case NIN_POPUPCLOSE:
            HidePopup();
            break;
        }
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDM_EXIT)
        {
            DestroyWindow(hwnd);
        }
        else if (LOWORD(wParam) == IDM_STARTUP)
        {
            SetStartupEnabled(!IsStartupEnabled());
        }
        return 0;

    case WM_TIMER:
        if (wParam == TIMER_REFRESH)
        {
            TrayUpdate();
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_REFRESH);
        TrayRemove();
        if (g_hPopup)
        {
            DestroyWindow(g_hPopup);
            g_hPopup = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ============================================================================
// Entry point
// ============================================================================
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    g_hInst = hInst;

    // Single-instance guard
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"GorgonHelperTray_v1_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (hMutex)
        {
            CloseHandle(hMutex);
        }
        return 0;
    }

    // Per-monitor DPI awareness (Windows 10 1703+)
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Register the shell "TaskbarCreated" broadcast message
    g_taskbarMsg = RegisterWindowMessageW(L"TaskbarCreated");

    // Register the main message-only window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"GorgonHelperTray_WndClass";
    if (!RegisterClassExW(&wc))
    {
        return 1;
    }

    // Register the hover popup window class
    WNDCLASSEXW wcp = {};
    wcp.cbSize = sizeof(wcp);
    wcp.lpfnWndProc = PopupWndProc;
    wcp.hInstance = hInst;
    wcp.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wcp.lpszClassName = L"MoonPhasePopup";
    RegisterClassExW(&wcp);

    // Register the chart window class
    WNDCLASSEXW wcc = {};
    wcc.cbSize = sizeof(wcc);
    wcc.lpfnWndProc = ChartWndProc;
    wcc.hInstance = hInst;
    wcc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wcc.lpszClassName = L"MoonPhaseChart";
    RegisterClassExW(&wcc);

    // Create invisible message-only window
    HWND hwnd = CreateWindowExW(0, L"GorgonHelperTray_WndClass", L"Gorgon Helper Tray", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInst, nullptr);
    if (!hwnd)
    {
        return 1;
    }

    TrayAdd(hwnd);
    SetTimer(hwnd, TIMER_REFRESH, REFRESH_MS, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (hMutex)
    {
        CloseHandle(hMutex);
    }
    return static_cast<int>(msg.wParam);
}
