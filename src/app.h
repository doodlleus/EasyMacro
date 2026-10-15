// EasyMacro - a tiny tray app that plays recorded keystrokes when you press a hotkey.
// Built without the C runtime; see build.cmd.
#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define WINVER       0x0A00
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <shellapi.h>

#define APP_NAME   L"EasyMacro"
#define MAX_MACROS 64
#define MAX_EVT    256          // key events per macro (a down + an up per keystroke)
#define NAME_LEN   64
#define WIN_W      440          // client size in DIPs
#define WIN_H      560

#define WM_APP_TRAY   (WM_APP + 1)
#define WM_APP_KEY    (WM_APP + 2)   // from the keyboard hook: wParam = vk | up<<8 | ext<<9, lParam = scan code
#define WM_APP_UNHOOK (WM_APP + 3)
#define WM_APP_SHOW   (WM_APP + 4)

#define ID_EDIT 100

template <class T> static inline T Min(T a, T b) { return a < b ? a : b; }
template <class T> static inline T Max(T a, T b) { return a > b ? a : b; }

// ---- Macros (macro.cpp) -----------------------------------------------------

enum { EVT_UP = 1, EVT_EXT = 2 };
struct KeyEvt { BYTE vk, scan, flags; };

struct Macro {
    WCHAR  name[NAME_LEN];
    UINT   mods, vk;            // trigger: MOD_* flags + virtual key
    BYTE   enabled;
    BYTE   failed;              // runtime only: the hotkey could not be registered
    WORD   nEvt;
    KeyEvt evt[MAX_EVT];
};

extern Macro g_macros[MAX_MACROS];
extern int   g_count;
extern bool  g_paused;
extern bool  g_hinted;          // the "still running in the tray" balloon has been shown

// Key chips: a flat list of key names. sep says how a token joins the one before it.
enum { TOK_FIRST, TOK_JOIN, TOK_GROUP };
struct KeyTok { BYTE sep; WCHAR name[23]; };

UINT ModBit(UINT vk);
bool IsTypingKey(UINT vk);
int  FormatHotkey(UINT mods, UINT vk, KeyTok* out, int max);
int  FormatEvents(const KeyEvt* e, int n, KeyTok* out, int max);
void TokensToText(const KeyTok* t, int n, WCHAR* buf, int cch);

void PlayMacro(const Macro* m);
void RegisterAll();
bool HotkeyAvailable(UINT mods, UINT vk);
void LoadMacros();
void SaveMacros();
bool GetAutostart();
void SetAutostart(bool on);

// Low-level keyboard hook: installed only while capturing a hotkey or recording.
bool HookStart();
void HookFinish();              // stop capturing; unhook once captured keys are released
void HookDrained();             // WM_APP_UNHOOK handler
void HookStop();

// ---- Rendering (gfx.cpp) ----------------------------------------------------

bool     Gfx_Begin(HDC wndDC, int w, int h);
void     Gfx_Present(HDC wndDC);
void     Gfx_Free();
void     Gfx_Clip(const RECT* r);
RECT     Gfx_GetClip();
void     Gfx_Fill(int x, int y, int w, int h, COLORREF c);
void     Gfx_Round(int x, int y, int w, int h, float r, COLORREF c, int alpha = 255);
void     Gfx_Frame(int x, int y, int w, int h, float r, COLORREF fill, COLORREF stroke, int t = 1);
void     Gfx_Text(const WCHAR* s, int n, int x, int y, int w, int h, HFONT f, COLORREF c, UINT fmt);
int      Gfx_TextW(const WCHAR* s, int n, HFONT f);
COLORREF Mix(COLORREF a, COLORREF b, int t256);

// ---- UI (ui.cpp) ------------------------------------------------------------

void    UI_Create(HWND wnd);
void    UI_LoadTheme();
bool    UI_IsDark();
COLORREF UI_Background();
void    UI_SetDpi(UINT dpi);
void    UI_Paint(HDC dc);
LRESULT UI_Mouse(UINT msg, WPARAM wp, LPARAM lp);
bool    UI_WantsIBeam();
bool    UI_KeyDown(WPARAM vk);
void    UI_HookKey(WPARAM info, LPARAM scan);
void    UI_EditNotify(UINT code);
LRESULT UI_EditColor(HDC dc);
void    UI_Deactivated();
void    UI_Hidden();
void    UI_Refresh();

// ---- App (main.cpp) ---------------------------------------------------------

extern HINSTANCE g_inst;
extern HWND      g_wnd;
extern HWND      g_edit;
extern bool      g_autostart;

void App_Hide();
void App_SetPaused(bool paused);
void App_SetAutostart(bool on);
void App_MacrosChanged();
