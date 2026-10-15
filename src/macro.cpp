// Macro engine: key naming, playback, hotkey registration, the recording hook and storage.
#include "app.h"

Macro g_macros[MAX_MACROS];
int   g_count;
bool  g_paused;
bool  g_hinted;

static BYTE  s_registered[MAX_MACROS];
static INPUT s_input[MAX_EVT + 16];

// ---- Key names ----------------------------------------------------------------

UINT ModBit(UINT vk)
{
    switch (vk) {
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return MOD_CONTROL;
    case VK_SHIFT:   case VK_LSHIFT:   case VK_RSHIFT:   return MOD_SHIFT;
    case VK_MENU:    case VK_LMENU:    case VK_RMENU:    return MOD_ALT;
    case VK_LWIN:    case VK_RWIN:                       return MOD_WIN;
    }
    return 0;
}

bool IsTypingKey(UINT vk)
{
    return (vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z') ||
           vk == VK_SPACE || vk == VK_RETURN || vk == VK_BACK || vk == VK_TAB ||
           (vk >= VK_OEM_1 && vk <= VK_OEM_3) || (vk >= VK_OEM_4 && vk <= VK_OEM_8) ||
           vk == VK_OEM_102 || (vk >= VK_NUMPAD0 && vk <= VK_DIVIDE);
}

static const WCHAR* FixedName(UINT vk)
{
    switch (vk) {
    case VK_LEFT:     return L"\x2190";
    case VK_UP:       return L"\x2191";
    case VK_RIGHT:    return L"\x2192";
    case VK_DOWN:     return L"\x2193";
    case VK_PRIOR:    return L"PgUp";
    case VK_NEXT:     return L"PgDn";
    case VK_HOME:     return L"Home";
    case VK_END:      return L"End";
    case VK_INSERT:   return L"Ins";
    case VK_DELETE:   return L"Del";
    case VK_BACK:     return L"Backspace";
    case VK_TAB:      return L"Tab";
    case VK_RETURN:   return L"Enter";
    case VK_ESCAPE:   return L"Esc";
    case VK_SPACE:    return L"Space";
    case VK_CAPITAL:  return L"Caps Lock";
    case VK_NUMLOCK:  return L"Num Lock";
    case VK_SCROLL:   return L"Scroll Lock";
    case VK_SNAPSHOT: return L"PrtSc";
    case VK_PAUSE:    return L"Pause";
    case VK_CANCEL:   return L"Break";
    case VK_APPS:     return L"Menu";
    case VK_SLEEP:    return L"Sleep";
    case VK_MULTIPLY: return L"Num *";
    case VK_ADD:      return L"Num +";
    case VK_SUBTRACT: return L"Num -";
    case VK_DECIMAL:  return L"Num .";
    case VK_DIVIDE:   return L"Num /";
    case VK_VOLUME_MUTE:         return L"Mute";
    case VK_VOLUME_DOWN:         return L"Volume -";
    case VK_VOLUME_UP:           return L"Volume +";
    case VK_MEDIA_NEXT_TRACK:    return L"Next track";
    case VK_MEDIA_PREV_TRACK:    return L"Prev track";
    case VK_MEDIA_STOP:          return L"Stop media";
    case VK_MEDIA_PLAY_PAUSE:    return L"Play/Pause";
    case VK_BROWSER_BACK:        return L"Browser back";
    case VK_BROWSER_FORWARD:     return L"Browser fwd";
    case VK_BROWSER_REFRESH:     return L"Refresh";
    case VK_BROWSER_STOP:        return L"Browser stop";
    case VK_BROWSER_SEARCH:      return L"Search";
    case VK_BROWSER_FAVORITES:   return L"Favorites";
    case VK_BROWSER_HOME:        return L"Browser home";
    case VK_LAUNCH_MAIL:         return L"Mail";
    case VK_LAUNCH_MEDIA_SELECT: return L"Media";
    case VK_LAUNCH_APP1:         return L"App 1";
    case VK_LAUNCH_APP2:         return L"App 2";
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return L"Ctrl";
    case VK_SHIFT:   case VK_LSHIFT:   case VK_RSHIFT:   return L"Shift";
    case VK_MENU:    case VK_LMENU:    case VK_RMENU:    return L"Alt";
    case VK_LWIN:    case VK_RWIN:                       return L"Win";
    }
    return NULL;
}

static void KeyName(UINT vk, UINT scan, bool ext, WCHAR* buf, int cch)
{
    if (const WCHAR* fixed = FixedName(vk)) {
        lstrcpynW(buf, fixed, cch);
        return;
    }
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) {
        buf[0] = (WCHAR)vk;
        buf[1] = 0;
        return;
    }
    if (vk >= VK_F1 && vk <= VK_F24) {
        wsprintfW(buf, L"F%u", vk - VK_F1 + 1);
        return;
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
        wsprintfW(buf, L"Num %u", vk - VK_NUMPAD0);
        return;
    }
    // Punctuation keys: show the character the current layout produces.
    UINT ch = MapVirtualKeyW(vk, MAPVK_VK_TO_CHAR) & 0x7FFF;
    if (ch > L' ') {
        buf[0] = (WCHAR)(ULONG_PTR)CharUpperW((LPWSTR)(ULONG_PTR)ch);
        buf[1] = 0;
        return;
    }
    if (!scan) scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    if (scan && GetKeyNameTextW((LONG)((scan << 16) | (ext ? 1u << 24 : 0)), buf, cch) > 0) return;
    wsprintfW(buf, L"Key %02X", vk);
}

static int Emit(KeyTok* out, int n, int max, BYTE sep, const WCHAR* name)
{
    if (n >= max) return n;
    out[n].sep = sep;
    lstrcpynW(out[n].name, name, ARRAYSIZE(out[n].name));
    return n + 1;
}

// One chord: modifiers in Windows' usual order, then the key.
static int EmitGroup(KeyTok* out, int n, int max, UINT mods, UINT vk, UINT scan, bool ext)
{
    static const UINT   order[4] = { MOD_WIN, MOD_CONTROL, MOD_ALT, MOD_SHIFT };
    static const WCHAR* names[4] = { L"Win", L"Ctrl", L"Alt", L"Shift" };
    BYTE sep = (BYTE)(n ? TOK_GROUP : TOK_FIRST);
    for (int i = 0; i < 4; ++i) {
        if (mods & order[i]) {
            n = Emit(out, n, max, sep, names[i]);
            sep = TOK_JOIN;
        }
    }
    if (vk) {
        WCHAR name[32];
        KeyName(vk, scan, ext, name, ARRAYSIZE(name));
        n = Emit(out, n, max, sep, name);
    }
    return n;
}

int FormatHotkey(UINT mods, UINT vk, KeyTok* out, int max)
{
    UINT sc = vk ? MapVirtualKeyW(vk, MAPVK_VK_TO_VSC_EX) : 0;
    return EmitGroup(out, 0, max, mods, vk, sc & 0xFF, (sc >> 8) == 0xE0);
}

// The character a key types with the given modifiers held, if it is plain typing.
static WCHAR TypedChar(UINT vk, UINT held)
{
    if (held & ~MOD_SHIFT) return 0;
    bool shift = (held & MOD_SHIFT) != 0;
    if (vk >= 'A' && vk <= 'Z') return (WCHAR)(shift ? vk : vk + 32);
    if (shift) return 0;
    if (vk == VK_SPACE) return L' ';
    if (vk >= '0' && vk <= '9') return (WCHAR)vk;
    if ((vk >= VK_OEM_1 && vk <= VK_OEM_3) || (vk >= VK_OEM_4 && vk <= VK_OEM_8) || vk == VK_OEM_102) {
        UINT ch = MapVirtualKeyW(vk, MAPVK_VK_TO_CHAR);
        if (ch & 0x80000000) return 0;  // dead key
        ch &= 0xFFFF;
        return ch > L' ' ? (WCHAR)ch : 0;
    }
    return 0;
}

// Turns raw down/up events into readable chords, e.g. Ctrl+C, Ctrl+V. Plain typing
// is merged into a single token, so "hello" shows as one chip instead of five.
int FormatEvents(const KeyEvt* ev, int count, KeyTok* out, int max)
{
    const int runMax = ARRAYSIZE(out[0].name) - 1;
    int  n = 0;
    int  run = -1, runLen = 0;      // token holding the current run of typed text
    UINT held = 0, peak = 0;
    bool keyed = false;              // a regular key went down while modifiers were held
    for (int i = 0; i < count; ++i) {
        const KeyEvt& e = ev[i];
        UINT bit = ModBit(e.vk);
        bool up = (e.flags & EVT_UP) != 0;
        if (bit) {
            if (!up) {
                if (!held) { peak = 0; keyed = false; }
                held |= bit;
                peak |= bit;
            } else {
                held &= ~bit;
                if (!held && !keyed && peak) n = EmitGroup(out, n, max, peak, 0, 0, false);  // lone modifier tap
            }
        } else if (!up) {
            WCHAR ch = TypedChar(e.vk, held);
            if (ch && run >= 0 && run == n - 1 && runLen < runMax) {
                out[run].name[runLen++] = ch;
                out[run].name[runLen] = 0;
            } else if (ch && ch != L' ' && n < max) {
                WCHAR text[2] = { ch, 0 };
                n = Emit(out, n, max, (BYTE)(n ? TOK_GROUP : TOK_FIRST), text);
                run = n - 1;
                runLen = 1;
            } else {
                n = EmitGroup(out, n, max, held, e.vk, e.scan, (e.flags & EVT_EXT) != 0);
            }
            keyed = true;
        }
    }
    return n;
}

void TokensToText(const KeyTok* t, int n, WCHAR* buf, int cch)
{
    int len = 0;
    buf[0] = 0;
    for (int i = 0; i < n; ++i) {
        const WCHAR* parts[2] = { t[i].sep == TOK_JOIN ? L"+" : t[i].sep == TOK_GROUP ? L", " : L"", t[i].name };
        for (int p = 0; p < 2; ++p) {
            for (const WCHAR* s = parts[p]; *s && len < cch - 1; ++s) buf[len++] = *s;
        }
    }
    buf[len] = 0;
}

// ---- Playback -------------------------------------------------------------------

static void AddKey(int& n, UINT vk, UINT scan, DWORD flags)
{
    INPUT& in = s_input[n++];
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = (WORD)vk;
    in.ki.wScan = (WORD)scan;
    in.ki.dwFlags = flags;
    in.ki.time = 0;
    in.ki.dwExtraInfo = 0;
}

void PlayMacro(const Macro* m)
{
    static const BYTE mods[8] = { VK_LCONTROL, VK_RCONTROL, VK_LSHIFT, VK_RSHIFT, VK_LMENU, VK_RMENU, VK_LWIN, VK_RWIN };
    int n = 0;

    // Release the hotkey's modifiers first so they don't leak into the macro. Tapping an
    // unassigned key before releasing Alt/Win stops the menu bar or Start menu from opening.
    BYTE held[8];
    int  nHeld = 0;
    bool mask = false;
    for (int i = 0; i < 8; ++i) {
        if (GetAsyncKeyState(mods[i]) & 0x8000) {
            held[nHeld++] = mods[i];
            if (i >= 4) mask = true;
        }
    }
    if (mask) {
        AddKey(n, 0xE8, 0, 0);
        AddKey(n, 0xE8, 0, KEYEVENTF_KEYUP);
    }
    for (int i = 0; i < nHeld; ++i) {
        UINT vk = held[i];
        bool ext = vk == VK_RCONTROL || vk == VK_RMENU || vk == VK_LWIN || vk == VK_RWIN;
        AddKey(n, vk, MapVirtualKeyW(vk, MAPVK_VK_TO_VSC), KEYEVENTF_KEYUP | (ext ? KEYEVENTF_EXTENDEDKEY : 0));
    }

    for (int i = 0; i < m->nEvt; ++i) {
        const KeyEvt& e = m->evt[i];
        AddKey(n, e.vk, e.scan, (e.flags & EVT_UP ? KEYEVENTF_KEYUP : 0) | (e.flags & EVT_EXT ? KEYEVENTF_EXTENDEDKEY : 0));
    }
    SendInput(n, s_input, sizeof(INPUT));
}

// ---- Hotkeys --------------------------------------------------------------------

void RegisterAll()
{
    for (int i = 0; i < MAX_MACROS; ++i) {
        if (s_registered[i]) {
            UnregisterHotKey(g_wnd, i + 1);
            s_registered[i] = 0;
        }
    }
    for (int i = 0; i < g_count; ++i) {
        Macro& m = g_macros[i];
        m.failed = 0;
        if (g_paused || !m.enabled || !m.vk) continue;
        if (RegisterHotKey(g_wnd, i + 1, m.mods | MOD_NOREPEAT, m.vk)) s_registered[i] = 1;
        else m.failed = 1;
    }
}

bool HotkeyAvailable(UINT mods, UINT vk)
{
    const int probe = 0xBFFF;
    if (!RegisterHotKey(g_wnd, probe, mods | MOD_NOREPEAT, vk)) return false;
    UnregisterHotKey(g_wnd, probe);
    return true;
}

// ---- Keyboard hook ------------------------------------------------------------------
// Only installed while the editor is listening. Captured keys are swallowed so they
// don't reach other apps, and so are their releases, even after capture ends.

enum { HOOK_OFF, HOOK_ACTIVE, HOOK_DRAIN };
static HHOOK s_hook;
static int   s_hookMode;
static BYTE  s_down[256];
static int   s_downCount;

static LRESULT CALLBACK LowLevelKeyboard(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION && s_hookMode != HOOK_OFF) {
        const KBDLLHOOKSTRUCT* k = (const KBDLLHOOKSTRUCT*)lp;
        if (!(k->flags & LLKHF_INJECTED) && k->vkCode < 256) {
            BYTE   vk = (BYTE)k->vkCode;
            bool   up = (k->flags & LLKHF_UP) != 0;
            WPARAM info = vk | (up ? 0x100 : 0) | ((k->flags & LLKHF_EXTENDED) ? 0x200 : 0);
            if (up) {
                if (s_down[vk]) {
                    s_down[vk] = 0;
                    --s_downCount;
                    if (s_hookMode == HOOK_ACTIVE) PostMessageW(g_wnd, WM_APP_KEY, info, k->scanCode);
                    else if (!s_downCount) PostMessageW(g_wnd, WM_APP_UNHOOK, 0, 0);
                    return 1;
                }
            } else if (s_down[vk]) {
                return 1;                               // auto-repeat of a captured key
            } else if (s_hookMode == HOOK_ACTIVE) {
                s_down[vk] = 1;
                ++s_downCount;
                PostMessageW(g_wnd, WM_APP_KEY, info, k->scanCode);
                return 1;
            }
        }
    }
    return CallNextHookEx(NULL, code, wp, lp);
}

bool HookStart()
{
    if (!s_hook) s_hook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboard, g_inst, 0);
    s_hookMode = s_hook ? HOOK_ACTIVE : HOOK_OFF;
    return s_hook != NULL;
}

void HookFinish()
{
    if (s_downCount) s_hookMode = HOOK_DRAIN;
    else HookStop();
}

void HookDrained()
{
    if (s_hookMode == HOOK_DRAIN && !s_downCount) HookStop();
}

void HookStop()
{
    if (s_hook) UnhookWindowsHookEx(s_hook);
    s_hook = NULL;
    s_hookMode = HOOK_OFF;
    ZeroMemory(s_down, sizeof(s_down));
    s_downCount = 0;
}

// ---- Storage ----------------------------------------------------------------------
// %APPDATA%\EasyMacro\macros.cfg, UTF-8, one macro per line:
//   M <tab> enabled <tab> mods <tab> vk <tab> events (vk scan flags, 2 hex digits each) <tab> name

static WCHAR s_line[64 + MAX_EVT * 6 + NAME_LEN];
static char  s_utf8[sizeof(s_line) / sizeof(WCHAR) * 3];

static bool ConfigPath(WCHAR* path, const WCHAR* file)
{
    DWORD n = GetEnvironmentVariableW(L"APPDATA", path, MAX_PATH);
    if (!n || n > MAX_PATH - 32) return false;
    lstrcatW(path, L"\\EasyMacro");
    if (file) {
        lstrcatW(path, L"\\");
        lstrcatW(path, file);
    }
    return true;
}

static WCHAR* PutHex(WCHAR* p, UINT v, int digits)
{
    for (int i = digits - 1; i >= 0; --i, v >>= 4) p[i] = L"0123456789ABCDEF"[v & 15];
    return p + digits;
}

static UINT GetHex(const WCHAR* s, int digits)
{
    UINT v = 0;
    for (int i = 0; i < digits && s[i]; ++i) {
        WCHAR c = s[i];
        int d = c >= '0' && c <= '9' ? c - '0' : (c | 0x20) >= 'a' && (c | 0x20) <= 'f' ? (c | 0x20) - 'a' + 10 : -1;
        if (d < 0) break;
        v = v * 16 + d;
    }
    return v;
}

static bool WriteLine(HANDLE f, const WCHAR* s, int n)
{
    int bytes = WideCharToMultiByte(CP_UTF8, 0, s, n, s_utf8, sizeof(s_utf8), NULL, NULL);
    DWORD written = 0;
    return bytes > 0 && WriteFile(f, s_utf8, bytes, &written, NULL) && written == (DWORD)bytes;
}

void SaveMacros()
{
    WCHAR dir[MAX_PATH], tmp[MAX_PATH], path[MAX_PATH];
    if (!ConfigPath(dir, NULL) || !ConfigPath(tmp, L"macros.tmp") || !ConfigPath(path, L"macros.cfg")) return;
    CreateDirectoryW(dir, NULL);
    HANDLE f = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;

    int  n = wsprintfW(s_line, L"EasyMacro 1\r\nP\t%d\r\nH\t%d\r\n", g_paused ? 1 : 0, g_hinted ? 1 : 0);
    bool ok = WriteLine(f, s_line, n);
    for (int i = 0; i < g_count && ok; ++i) {
        const Macro& m = g_macros[i];
        WCHAR* p = s_line;
        *p++ = 'M'; *p++ = '\t';
        *p++ = m.enabled ? '1' : '0'; *p++ = '\t';
        p = PutHex(p, m.mods, 2); *p++ = '\t';
        p = PutHex(p, m.vk, 2); *p++ = '\t';
        for (int e = 0; e < m.nEvt; ++e) {
            p = PutHex(p, m.evt[e].vk, 2);
            p = PutHex(p, m.evt[e].scan, 2);
            p = PutHex(p, m.evt[e].flags, 2);
        }
        if (!m.nEvt) *p++ = '-';
        *p++ = '\t';
        for (const WCHAR* s = m.name; *s; ++s) *p++ = (*s == '\t' || *s == '\r' || *s == '\n') ? L' ' : *s;
        *p++ = '\r'; *p++ = '\n';
        ok = WriteLine(f, s_line, (int)(p - s_line));
    }
    CloseHandle(f);
    if (ok) MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING);
    else DeleteFileW(tmp);
}

static void ParseConfig(WCHAR* p)
{
    g_count = 0;
    while (*p) {
        WCHAR* line = p;
        while (*p && *p != '\n') ++p;
        WCHAR* end = p;
        if (*p) ++p;
        if (end > line && end[-1] == '\r') --end;
        *end = 0;

        WCHAR* f[6];
        int nf = 0;
        f[nf++] = line;
        for (WCHAR* q = line; *q && nf < 6; ++q) {
            if (*q == '\t') { *q = 0; f[nf++] = q + 1; }
        }

        if (line[0] == 'P' && nf >= 2) g_paused = f[1][0] == '1';
        else if (line[0] == 'H' && nf >= 2) g_hinted = f[1][0] == '1';
        else if (line[0] == 'M' && nf == 6 && g_count < MAX_MACROS) {
            Macro& m = g_macros[g_count];
            ZeroMemory(&m, sizeof(m));
            m.enabled = f[1][0] == '1';
            m.mods = GetHex(f[2], 2) & (MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN);
            m.vk = GetHex(f[3], 2);
            if (f[4][0] != '-') {
                int len = lstrlenW(f[4]);
                for (int i = 0; i + 6 <= len && m.nEvt < MAX_EVT; i += 6) {
                    KeyEvt& e = m.evt[m.nEvt++];
                    e.vk = (BYTE)GetHex(f[4] + i, 2);
                    e.scan = (BYTE)GetHex(f[4] + i + 2, 2);
                    e.flags = (BYTE)GetHex(f[4] + i + 4, 2) & (EVT_UP | EVT_EXT);
                }
            }
            lstrcpynW(m.name, f[5], NAME_LEN);
            if (m.vk) ++g_count;
        }
    }
}

void LoadMacros()
{
    WCHAR path[MAX_PATH];
    if (!ConfigPath(path, L"macros.cfg")) return;
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;

    HANDLE heap = GetProcessHeap();
    DWORD  size = GetFileSize(f, NULL), got = 0;
    char*  raw = size && size < (4u << 20) ? (char*)HeapAlloc(heap, 0, size) : NULL;
    if (raw && ReadFile(f, raw, size, &got, NULL) && got == size) {
        int    wn = MultiByteToWideChar(CP_UTF8, 0, raw, (int)size, NULL, 0);
        WCHAR* text = wn > 0 ? (WCHAR*)HeapAlloc(heap, 0, (wn + 1) * sizeof(WCHAR)) : NULL;
        if (text) {
            MultiByteToWideChar(CP_UTF8, 0, raw, (int)size, text, wn);
            text[wn] = 0;
            ParseConfig(text);
            HeapFree(heap, 0, text);
        }
    }
    if (raw) HeapFree(heap, 0, raw);
    CloseHandle(f);
}

// ---- Start with Windows -----------------------------------------------------------

static const WCHAR kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

bool GetAutostart()
{
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, APP_NAME, RRF_RT_REG_SZ, NULL, NULL, NULL) == ERROR_SUCCESS;
}

void SetAutostart(bool on)
{
    if (!on) {
        RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, APP_NAME);
        return;
    }
    WCHAR exe[MAX_PATH], cmd[MAX_PATH + 16];
    DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (!n || n >= MAX_PATH) return;
    int len = wsprintfW(cmd, L"\"%s\" /tray", exe);
    RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, APP_NAME, REG_SZ, cmd, (DWORD)((len + 1) * sizeof(WCHAR)));
}
