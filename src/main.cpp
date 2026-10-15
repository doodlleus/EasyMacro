// Entry point, tray icon and the main window.
#include "app.h"
#include <dwmapi.h>

HINSTANCE g_inst;
HWND      g_wnd;
bool      g_autostart;

static const WCHAR kClass[] = L"EasyMacro.Window";
static const DWORD kStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;

static UINT  s_taskbarCreated;
static HICON s_icon, s_iconPaused, s_iconBig;
static DWORD s_playStart;
static bool  s_played;

enum { CMD_OPEN = 1, CMD_PAUSE, CMD_AUTOSTART, CMD_EXIT };

// Returns unused pages to Windows; the app idles in the tray with a tiny working set.
static void TrimMemory()
{
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
}

// ---- Tray -------------------------------------------------------------------------

static void Tray(DWORD action)
{
    NOTIFYICONDATAW nd = {};
    nd.cbSize = sizeof(nd);
    nd.hWnd = g_wnd;
    nd.uID = 1;
    nd.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nd.uCallbackMessage = WM_APP_TRAY;
    nd.hIcon = g_paused ? s_iconPaused : s_icon;
    if (g_paused) lstrcpyW(nd.szTip, L"EasyMacro \x2014 hotkeys paused");
    else if (g_count == 1) lstrcpyW(nd.szTip, L"EasyMacro \x2014 1 macro");
    else wsprintfW(nd.szTip, L"EasyMacro \x2014 %d macros", g_count);
    Shell_NotifyIconW(action, &nd);
    if (action == NIM_ADD) {
        nd.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &nd);
    }
}

static void TrayBalloon()
{
    NOTIFYICONDATAW nd = {};
    nd.cbSize = sizeof(nd);
    nd.hWnd = g_wnd;
    nd.uID = 1;
    nd.uFlags = NIF_INFO;
    nd.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON | NIIF_NOSOUND;
    nd.hBalloonIcon = s_iconBig;
    lstrcpyW(nd.szInfoTitle, L"EasyMacro is still running");
    lstrcpyW(nd.szInfo, L"Your hotkeys keep working. Click the tray icon to open EasyMacro again.");
    Shell_NotifyIconW(NIM_MODIFY, &nd);
}

// Popup menus follow the app's dark mode only through these undocumented uxtheme exports
// (ordinals 135/136, Windows 10 1903+). They are optional: on failure menus stay light.
static void SetMenuTheme(bool dark)
{
    typedef int  (WINAPI* SetPreferredAppModeFn)(int);
    typedef void (WINAPI* FlushMenuThemesFn)();
    typedef void (WINAPI* RtlGetNtVersionNumbersFn)(DWORD*, DWORD*, DWORD*);
    static SetPreferredAppModeFn setMode;
    static FlushMenuThemesFn     flush;
    static bool                  tried;
    if (!tried) {
        tried = true;
        DWORD major = 0, minor = 0, build = 0;
        auto version = (RtlGetNtVersionNumbersFn)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetNtVersionNumbers");
        if (version) version(&major, &minor, &build);
        if ((build & 0xFFFF) >= 18362) {
            if (HMODULE ux = LoadLibraryExW(L"uxtheme.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32)) {
                setMode = (SetPreferredAppModeFn)GetProcAddress(ux, MAKEINTRESOURCEA(135));
                flush = (FlushMenuThemesFn)GetProcAddress(ux, MAKEINTRESOURCEA(136));
            }
        }
    }
    if (setMode) {
        setMode(dark ? 2 : 3);              // ForceDark / ForceLight
        if (flush) flush();
    }
}

static void TrayMenu(int x, int y)
{
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, CMD_OPEN, L"Open EasyMacro");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING | (g_paused ? MF_CHECKED : 0), CMD_PAUSE, L"Pause hotkeys");
    AppendMenuW(menu, MF_STRING | (g_autostart ? MF_CHECKED : 0), CMD_AUTOSTART, L"Start with Windows");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, CMD_EXIT, L"Exit");
    SetMenuDefaultItem(menu, CMD_OPEN, FALSE);

    SetForegroundWindow(g_wnd);
    UINT align = GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
    int  cmd = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | align, x, y, g_wnd, NULL);
    PostMessageW(g_wnd, WM_NULL, 0, 0);
    DestroyMenu(menu);

    switch (cmd) {
    case CMD_OPEN:      PostMessageW(g_wnd, WM_APP_SHOW, 0, 0); break;
    case CMD_PAUSE:     App_SetPaused(!g_paused); break;
    case CMD_AUTOSTART: App_SetAutostart(!g_autostart); break;
    case CMD_EXIT:      DestroyWindow(g_wnd); break;
    }
}

// ---- App actions --------------------------------------------------------------------

static void ApplyTheme()
{
    UI_LoadTheme();
    BOOL dark = UI_IsDark();
    if (FAILED(DwmSetWindowAttribute(g_wnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark))))
        DwmSetWindowAttribute(g_wnd, 19, &dark, sizeof(dark));
    COLORREF caption = UI_Background();
    DwmSetWindowAttribute(g_wnd, 35 /* DWMWA_CAPTION_COLOR, Windows 11 */, &caption, sizeof(caption));
    SetMenuTheme(dark != FALSE);
    UI_Refresh();
}

static void App_Show()
{
    if (!IsWindowVisible(g_wnd)) {
        // Open centered on the monitor with the mouse, sized for that monitor's DPI.
        POINT pt;
        GetCursorPos(&pt);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
        const RECT& wa = mi.rcWork;
        SetWindowPos(g_wnd, NULL, wa.left, wa.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        UINT dpi = GetDpiForWindow(g_wnd);
        UI_SetDpi(dpi);
        RECT r = { 0, 0, MulDiv(WIN_W, dpi, 96), MulDiv(WIN_H, dpi, 96) };
        AdjustWindowRectExForDpi(&r, kStyle, FALSE, 0, dpi);
        int w = r.right - r.left, h = r.bottom - r.top;
        SetWindowPos(g_wnd, NULL, wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - h) / 2, w, h,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        ShowWindow(g_wnd, SW_SHOW);
    } else if (IsIconic(g_wnd)) {
        ShowWindow(g_wnd, SW_RESTORE);
    }
    SetForegroundWindow(g_wnd);
}

void App_Hide()
{
    UI_Hidden();
    ShowWindow(g_wnd, SW_HIDE);
    if (!g_hinted) {
        g_hinted = true;
        SaveMacros();
        TrayBalloon();
    }
    TrimMemory();
}

void App_SetPaused(bool paused)
{
    g_paused = paused;
    RegisterAll();
    SaveMacros();
    Tray(NIM_MODIFY);
    UI_Refresh();
}

void App_SetAutostart(bool on)
{
    SetAutostart(on);
    g_autostart = GetAutostart();
    UI_Refresh();
}

void App_MacrosChanged()
{
    RegisterAll();
    SaveMacros();
    Tray(NIM_MODIFY);
    UI_Refresh();
}

// ---- Window -------------------------------------------------------------------------

static LRESULT CALLBACK WndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        UI_Paint(dc);
        EndPaint(wnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE: case WM_MOUSELEAVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_MOUSEWHEEL:
        return UI_Mouse(msg, wp, lp);
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            SetCursor(LoadCursorW(NULL, UI_WantsIBeam() ? IDC_IBEAM : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_COMMAND:
        if (LOWORD(wp) == ID_EDIT) UI_EditNotify(HIWORD(wp));
        return 0;
    case WM_CTLCOLOREDIT:
        return UI_EditColor((HDC)wp);
    case WM_HOTKEY: {
        // A macro that types its own hotkey would retrigger itself forever, so ignore
        // hotkeys raised while (or just after) our own keystrokes were being sent.
        if (s_played && (DWORD)GetMessageTime() - s_playStart < 60) return 0;
        int i = (int)wp - 1;
        if (i >= 0 && i < g_count && g_macros[i].enabled && !g_paused) {
            s_playStart = GetTickCount();
            s_played = true;
            PlayMacro(&g_macros[i]);
        }
        return 0;
    }
    case WM_APP_KEY:
        UI_HookKey(wp, lp);
        return 0;
    case WM_APP_UNHOOK:
        HookDrained();
        return 0;
    case WM_APP_SHOW:
        App_Show();
        return 0;
    case WM_APP_TRAY:
        switch (LOWORD(lp)) {
        case NIN_SELECT:
        case NIN_KEYSELECT:
            App_Show();
            break;
        case WM_CONTEXTMENU:
            TrayMenu((short)LOWORD(wp), (short)HIWORD(wp));
            break;
        }
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) UI_Deactivated();
        break;
    case WM_CLOSE:
        App_Hide();
        return 0;
    case WM_SETTINGCHANGE:
        if (lp && !lstrcmpiW((LPCWSTR)lp, L"ImmersiveColorSet")) ApplyTheme();
        break;
    case WM_DWMCOLORIZATIONCOLORCHANGED:
        ApplyTheme();
        break;
    case WM_DPICHANGED: {
        UI_SetDpi(HIWORD(wp));
        const RECT* r = (const RECT*)lp;
        SetWindowPos(wnd, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_DESTROY:
        HookStop();
        Tray(NIM_DELETE);
        PostQuitMessage(0);
        return 0;
    default:
        if (msg == s_taskbarCreated && msg) {   // Explorer restarted: put the icon back
            Tray(NIM_ADD);
            return 0;
        }
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

static bool HasArg(const WCHAR* cmd, const WCHAR* arg)
{
    int n = lstrlenW(arg);
    for (; *cmd; ++cmd) {
        if (CompareStringOrdinal(cmd, Min(n, lstrlenW(cmd)), arg, n, TRUE) == CSTR_EQUAL) return true;
    }
    return false;
}

static HICON LoadIco(int id, int size)
{
    return (HICON)LoadImageW(g_inst, MAKEINTRESOURCEW(id), IMAGE_ICON, size, size, LR_DEFAULTCOLOR);
}

static int Run()
{
    g_inst = GetModuleHandleW(NULL);

    // One instance only: a second launch just opens the running one.
    HANDLE mutex = CreateMutexW(NULL, FALSE, L"Local\\EasyMacro.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND other = FindWindowW(kClass, NULL)) {
            DWORD pid = 0;
            GetWindowThreadProcessId(other, &pid);
            AllowSetForegroundWindow(pid);
            PostMessageW(other, WM_APP_SHOW, 0, 0);
        }
        return 0;
    }

    LoadMacros();
    g_autostart = GetAutostart();
    if (g_autostart) SetAutostart(true);    // keep the path current if the exe moved

    int smallSize = GetSystemMetrics(SM_CXSMICON), bigSize = GetSystemMetrics(SM_CXICON);
    s_icon = LoadIco(1, smallSize);
    s_iconPaused = LoadIco(2, smallSize);
    s_iconBig = LoadIco(1, bigSize);

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = g_inst;
    wc.hIcon = s_iconBig;
    wc.hIconSm = s_icon;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);

    g_wnd = CreateWindowExW(0, kClass, APP_NAME, kStyle, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, NULL, NULL, g_inst, NULL);
    if (!g_wnd) return 1;
    UI_Create(g_wnd);
    ApplyTheme();

    s_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    ChangeWindowMessageFilterEx(g_wnd, s_taskbarCreated, MSGFLT_ALLOW, NULL);
    RegisterAll();
    Tray(NIM_ADD);

    if (HasArg(GetCommandLineW(), L"/tray")) TrimMemory();
    else App_Show();

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        if (m.message == WM_KEYDOWN && UI_KeyDown(m.wParam)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    CloseHandle(mutex);
    return 0;
}

extern "C" void EntryPoint()
{
    ExitProcess(Run());
}
