// Window content: a list of macro cards and an editor, custom drawn to match Windows 11.
#include "app.h"
#include <commdlg.h>

#ifndef EM_SETCUEBANNER
#define EM_SETCUEBANNER 0x1501
#endif

#define GLYPH_ADD      L"\xE710"
#define GLYPH_DELETE   L"\xE74D"
#define GLYPH_FORWARD  L"\xE72A"
#define GLYPH_KEYBOARD L"\xE765"
#define GLYPH_WARNING  L"\xE7BA"
#define GLYPH_INFO     L"\xE946"
#define GLYPH_GLOBE    L"\xE774"
#define GLYPH_APP      L"\xE8E5"
#define ICON_DOT       L"\x0001"
#define ICON_STOP      L"\x0002"

HWND g_edit;                    // the macro name
static HWND s_target;           // what an "open" macro launches

static UINT s_dpi = 96;
static inline int S(int v) { return MulDiv(v, (int)s_dpi, 96); }

// ---- Theme ------------------------------------------------------------------------

static struct Theme {
    bool     dark;
    COLORREF bg, card, cardHover, cardStroke, divider, text, text2, text3;
    COLORREF chip, chipStroke, field, fieldHover, fieldFocus, fieldStroke, fieldBottom;
    COLORREF btn, btnHover, btnStroke, accent, accentHover, onAccent;
    COLORREF disabledFill, disabledText, danger, dangerFill, rec;
} T;

static HBRUSH s_fieldBrush, s_fieldFocusBrush;
static HFONT  fTitle, fBody, fSemi, fSmall, fIcon, fIconSm, fIconBig;
static bool   s_fluentIcons;
static int    s_bodyH;

void UI_LoadTheme()
{
    DWORD light = 1, cb = sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &light, &cb);
    BYTE  pal[32];
    DWORD palSize = sizeof(pal);
    bool  hasPal = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent",
                                L"AccentPalette", RRF_RT_REG_BINARY, NULL, pal, &palSize) == ERROR_SUCCESS && palSize >= 32;
    T.dark = light == 0;
    if (T.dark) {
        T.bg = RGB(0x20, 0x20, 0x20);    T.card = RGB(0x2B, 0x2B, 0x2B);       T.cardHover = RGB(0x32, 0x32, 0x32);
        T.cardStroke = RGB(0x1C, 0x1C, 0x1C); T.divider = RGB(0x33, 0x33, 0x33);
        T.text = RGB(0xFF, 0xFF, 0xFF);  T.text2 = RGB(0xC8, 0xC8, 0xC8);      T.text3 = RGB(0x8F, 0x8F, 0x8F);
        T.chip = RGB(0x3A, 0x3A, 0x3A);  T.chipStroke = RGB(0x4C, 0x4C, 0x4C);
        T.field = RGB(0x2D, 0x2D, 0x2D); T.fieldHover = RGB(0x32, 0x32, 0x32); T.fieldFocus = RGB(0x1F, 0x1F, 0x1F);
        T.fieldStroke = RGB(0x3A, 0x3A, 0x3A); T.fieldBottom = RGB(0x8B, 0x8B, 0x8B);
        T.btn = RGB(0x2D, 0x2D, 0x2D);   T.btnHover = RGB(0x34, 0x34, 0x34);   T.btnStroke = RGB(0x3A, 0x3A, 0x3A);
        T.disabledFill = RGB(0x43, 0x43, 0x43); T.disabledText = RGB(0xA0, 0xA0, 0xA0);
        T.danger = RGB(0xFF, 0x99, 0xA4); T.rec = RGB(0xFF, 0x4D, 0x5A);
        T.accent = hasPal ? RGB(pal[4], pal[5], pal[6]) : RGB(0x4C, 0xC2, 0xFF);      // AccentLight2
        T.onAccent = RGB(0, 0, 0);
    } else {
        T.bg = RGB(0xF3, 0xF3, 0xF3);    T.card = RGB(0xFF, 0xFF, 0xFF);       T.cardHover = RGB(0xF8, 0xF8, 0xF8);
        T.cardStroke = RGB(0xE3, 0xE3, 0xE3); T.divider = RGB(0xE3, 0xE3, 0xE3);
        T.text = RGB(0x1B, 0x1B, 0x1B);  T.text2 = RGB(0x5F, 0x5F, 0x5F);      T.text3 = RGB(0x8D, 0x8D, 0x8D);
        T.chip = RGB(0xF5, 0xF5, 0xF5);  T.chipStroke = RGB(0xD0, 0xD0, 0xD0);
        T.field = RGB(0xFB, 0xFB, 0xFB); T.fieldHover = RGB(0xF6, 0xF6, 0xF6); T.fieldFocus = RGB(0xFF, 0xFF, 0xFF);
        T.fieldStroke = RGB(0xE5, 0xE5, 0xE5); T.fieldBottom = RGB(0x86, 0x86, 0x86);
        T.btn = RGB(0xFB, 0xFB, 0xFB);   T.btnHover = RGB(0xF4, 0xF4, 0xF4);   T.btnStroke = RGB(0xE0, 0xE0, 0xE0);
        T.disabledFill = RGB(0xC5, 0xC5, 0xC5); T.disabledText = RGB(0xFF, 0xFF, 0xFF);
        T.danger = RGB(0xC4, 0x2B, 0x1C); T.rec = RGB(0xE8, 0x11, 0x23);
        T.accent = hasPal ? RGB(pal[16], pal[17], pal[18]) : RGB(0x00, 0x67, 0xC0);   // AccentDark1
        T.onAccent = RGB(0xFF, 0xFF, 0xFF);
    }
    T.dangerFill = RGB(0xC4, 0x2B, 0x1C);
    T.accentHover = Mix(T.accent, T.bg, 28);

    if (s_fieldBrush) DeleteObject(s_fieldBrush);
    if (s_fieldFocusBrush) DeleteObject(s_fieldFocusBrush);
    s_fieldBrush = CreateSolidBrush(T.field);
    s_fieldFocusBrush = CreateSolidBrush(T.fieldFocus);
    if (g_edit) InvalidateRect(g_edit, NULL, TRUE);
    if (s_target) InvalidateRect(s_target, NULL, TRUE);
}

bool     UI_IsDark() { return T.dark; }
COLORREF UI_Background() { return T.bg; }

static int CALLBACK FontFound(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM found)
{
    *(bool*)found = true;
    return 0;
}

static bool FontExists(const WCHAR* face)
{
    LOGFONTW lf = {};
    lf.lfCharSet = DEFAULT_CHARSET;
    lstrcpynW(lf.lfFaceName, face, LF_FACESIZE);
    bool found = false;
    HDC dc = GetDC(NULL);
    EnumFontFamiliesExW(dc, &lf, FontFound, (LPARAM)&found, 0);
    ReleaseDC(NULL, dc);
    return found;
}

static HFONT MakeFont(const WCHAR* face, int px)
{
    LOGFONTW lf = {};
    lf.lfHeight = -S(px);
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lstrcpynW(lf.lfFaceName, face, LF_FACESIZE);
    return CreateFontIndirectW(&lf);
}

static void CreateFonts()
{
    HFONT* fonts[] = { &fTitle, &fBody, &fSemi, &fSmall, &fIcon, &fIconSm, &fIconBig };
    for (HFONT* f : fonts) {
        if (*f) DeleteObject(*f);
    }
    const WCHAR* icons = s_fluentIcons ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
    fTitle   = MakeFont(L"Segoe UI Semibold", 20);
    fBody    = MakeFont(L"Segoe UI", 14);
    fSemi    = MakeFont(L"Segoe UI Semibold", 14);
    fSmall   = MakeFont(L"Segoe UI", 12);
    fIcon    = MakeFont(icons, 14);
    fIconSm  = MakeFont(icons, 12);
    fIconBig = MakeFont(icons, 44);

    TEXTMETRICW tm = {};
    HDC dc = GetDC(NULL);
    HGDIOBJ old = SelectObject(dc, fBody);
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    ReleaseDC(NULL, dc);
    s_bodyH = tm.tmHeight;
    if (g_edit) SendMessageW(g_edit, WM_SETFONT, (WPARAM)fBody, FALSE);
    if (s_target) SendMessageW(s_target, WM_SETFONT, (WPARAM)fBody, FALSE);
}

// ---- State ------------------------------------------------------------------------

enum { H_NONE, H_NEW, H_CARD, H_TOGGLE, H_AUTOSTART, H_PAUSE, H_NAME, H_HOTKEY, H_ACTION, H_RECORD, H_CLEAR,
       H_TARGET, H_BROWSE, H_DELETE, H_CANCEL, H_SAVE };
#define HIT(type, index) (((type) << 8) | (index))

struct Hit { RECT rc; int id; };
static Hit  s_hits[MAX_MACROS * 2 + 16];
static int  s_nHits, s_hover, s_press;
static bool s_tracking;

enum { VIEW_LIST, VIEW_EDIT };
static int s_view, s_scroll;

enum { ED_IDLE, ED_CAPTURE, ED_RECORD };
static struct Editor {
    int    index;               // macro being edited, -1 for a new one
    UINT   mods, vk;            // hotkey
    UINT   capMask;             // modifier keys held while capturing (bit per left/right key)
    int    action;              // ACT_KEYS or ACT_OPEN
    int    mode;
    bool   confirmDelete;
    bool   msgError;
    int    nEvt;
    KeyEvt evt[MAX_EVT];
    WORD   lastDown[256];       // while recording: index + 1 of each held key's down event
    WCHAR  msg[200];
} E;

static KeyTok s_toks[MAX_EVT];
static WCHAR  s_targetText[TARGET_LEN];

static void Redraw() { InvalidateRect(g_wnd, NULL, FALSE); }

// App icons for "open" macros, loaded while the window is open and released when it hides.
static HICON s_cardIcons[MAX_MACROS];
static BYTE  s_cardIconLoaded[MAX_MACROS];
static HICON s_editIcon;
static WCHAR s_editIconFor[TARGET_LEN];

static void FreeIcons()
{
    for (int i = 0; i < MAX_MACROS; ++i) {
        if (s_cardIcons[i]) DestroyIcon(s_cardIcons[i]);
        s_cardIcons[i] = NULL;
        s_cardIconLoaded[i] = 0;
    }
    if (s_editIcon) DestroyIcon(s_editIcon);
    s_editIcon = NULL;
    s_editIconFor[0] = 0;
}

static HICON CardIcon(int i)
{
    if (!s_cardIconLoaded[i]) {
        s_cardIcons[i] = TargetIcon(g_macros[i].target);
        s_cardIconLoaded[i] = 1;
    }
    return s_cardIcons[i];
}

static HICON EditorIcon(const WCHAR* target)
{
    if (lstrcmpW(target, s_editIconFor)) {
        if (s_editIcon) DestroyIcon(s_editIcon);
        s_editIcon = target[0] ? TargetIcon(target) : NULL;
        lstrcpynW(s_editIconFor, target, TARGET_LEN);
    }
    return s_editIcon;
}

static void SetMsg(const WCHAR* text, bool error)
{
    lstrcpynW(E.msg, text ? text : L"", ARRAYSIZE(E.msg));
    E.msgError = error;
}

static void AddHit(int x, int y, int w, int h, int id)
{
    if (s_nHits >= (int)ARRAYSIZE(s_hits)) return;
    RECT r = { x, y, x + w, y + h }, clip = Gfx_GetClip();
    if (IntersectRect(&s_hits[s_nHits].rc, &r, &clip)) s_hits[s_nHits++].id = id;
}

static int HitAt(int x, int y)
{
    POINT pt = { x, y };
    for (int i = s_nHits - 1; i >= 0; --i) {
        if (PtInRect(&s_hits[i].rc, pt)) return s_hits[i].id;
    }
    return H_NONE;
}

static void RefreshHover()
{
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(g_wnd, &pt);
    int id = HitAt(pt.x, pt.y);
    if (id != s_hover) {
        s_hover = id;
        Redraw();
    }
}

// ---- Drawing helpers --------------------------------------------------------------

enum { BTN_STD, BTN_ACCENT, BTN_DANGER, BTN_SUBTLE, BTN_SUBTLE_DANGER, BTN_DISABLED };

static int ButtonWidth(const WCHAR* icon, const WCHAR* text)
{
    return Gfx_TextW(text, -1, fBody) + S(32) + (icon ? S(22) : 0);
}

static void DrawButton(int x, int y, int w, int h, const WCHAR* icon, const WCHAR* text, int style, int id)
{
    bool hover = s_hover == id && style != BTN_DISABLED;
    COLORREF bg, stroke, fg;
    switch (style) {
    case BTN_ACCENT:        bg = hover ? T.accentHover : T.accent;                 stroke = bg; fg = T.onAccent; break;
    case BTN_DANGER:        bg = hover ? Mix(T.dangerFill, T.bg, 36) : T.dangerFill; stroke = bg; fg = RGB(255, 255, 255); break;
    case BTN_SUBTLE:        bg = hover ? T.btnHover : T.bg;                        stroke = bg; fg = T.text; break;
    case BTN_SUBTLE_DANGER: bg = hover ? T.btnHover : T.bg;                        stroke = bg; fg = T.danger; break;
    case BTN_DISABLED:      bg = T.disabledFill;                                   stroke = bg; fg = T.disabledText; break;
    default:                bg = hover ? T.btnHover : T.btn;                       stroke = T.btnStroke; fg = T.text; break;
    }
    if (hover && s_press == id) {
        bg = Mix(bg, T.bg, 70);
        if (style != BTN_STD && style != BTN_SUBTLE && style != BTN_SUBTLE_DANGER) stroke = bg;
    }
    Gfx_Frame(x, y, w, h, (float)S(4), bg, stroke);

    int tw = Gfx_TextW(text, -1, fBody), iw = icon ? S(22) : 0;
    int cx = x + (w - tw - iw) / 2, cy = y + h / 2;
    if (icon) {
        int d = S(10);
        if (icon[0] == 1)      Gfx_Round(cx + S(3), cy - d / 2, d, d, d / 2.f, T.rec);
        else if (icon[0] == 2) Gfx_Round(cx + S(3), cy - d / 2, d, d, (float)S(2), fg);
        else                   Gfx_Text(icon, 1, cx, y, S(16), h, fIcon, fg, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        cx += iw;
    }
    Gfx_Text(text, -1, cx, y, tw + S(4), h, fBody, fg, DT_SINGLELINE | DT_VCENTER);
    AddHit(x, y, w, h, id);
}

static void DrawToggle(int x, int y, bool on, bool hover, COLORREF surface)
{
    int w = S(40), h = S(20), k = hover ? S(14) : S(12), cy = y + h / 2;
    if (on) {
        Gfx_Round(x, y, w, h, h / 2.f, hover ? T.accentHover : T.accent);
        int kx = x + w - h / 2;
        Gfx_Round(kx - k / 2, cy - k / 2, k, k, k / 2.f, T.onAccent);
    } else {
        Gfx_Frame(x, y, w, h, h / 2.f, hover ? Mix(surface, T.text, 18) : surface, T.text2);
        int kx = x + h / 2;
        Gfx_Round(kx - k / 2, cy - k / 2, k, k, k / 2.f, T.text2);
    }
}

enum { FIELD_NORMAL, FIELD_HOVER, FIELD_FOCUS, FIELD_ACTIVE, FIELD_RECORD };

static void DrawField(int x, int y, int w, int h, int state)
{
    float r = (float)S(4);
    COLORREF fill = state == FIELD_FOCUS ? T.fieldFocus : state == FIELD_HOVER ? T.fieldHover : T.field;
    if (state == FIELD_ACTIVE || state == FIELD_RECORD) {
        Gfx_Frame(x, y, w, h, r, fill, state == FIELD_ACTIVE ? T.accent : T.rec, Max(2, S(2)));
        return;
    }
    int bottom = state == FIELD_FOCUS ? Max(2, S(2)) : 1;
    Gfx_Round(x, y, w, h, r, state == FIELD_FOCUS ? T.accent : T.fieldBottom);
    Gfx_Round(x, y, w, h - bottom, r, T.fieldStroke);
    Gfx_Round(x + 1, y + 1, w - 2, h - 1 - bottom, r - 1, fill);
}

enum { CHIP_KEY, CHIP_TRIGGER };

// Draws key tokens as keycaps. With bottom > 0 the chips wrap onto new lines until bottom;
// otherwise they stay on one line. Returns the x after the last chip, or -1 if cut short.
static void ChipColors(int style, bool dim, COLORREF surface, COLORREF* bg, COLORREF* stroke, COLORREF* fg)
{
    if (style == CHIP_TRIGGER) {
        *bg = Mix(surface, T.accent, T.dark ? 44 : 28);
        *stroke = Mix(surface, T.accent, T.dark ? 110 : 90);
        *fg = T.accent;
    } else {
        *bg = T.chip;
        *stroke = T.chipStroke;
        *fg = T.text;
    }
    if (dim) {
        *bg = Mix(surface, *bg, 128);
        *stroke = Mix(surface, *stroke, 128);
        *fg = T.text3;
    }
}

static int DrawChips(const KeyTok* t, int n, int x, int y, int right, int bottom, int h, HFONT f,
                     int style, bool dim, COLORREF surface)
{
    COLORREF bg, stroke, fg;
    ChipColors(style, dim, surface, &bg, &stroke, &fg);
    int left = x, pad = S(7), r = S(4), lineGap = S(6);
    int ellipsis = Gfx_TextW(L"\x2026", 1, f) + S(8);
    for (int i = 0; i < n; ++i) {
        int gap = !i ? 0 : t[i].sep == TOK_JOIN ? S(3) : S(16);
        int cw = Max(Gfx_TextW(t[i].name, -1, f) + 2 * pad, h);
        int reserve = i < n - 1 ? ellipsis : 0;
        if (x + gap + cw + reserve > right) {
            if (bottom && y + 2 * h + lineGap <= bottom && cw <= right - left) {
                x = left;
                y += h + lineGap;
                gap = 0;
            } else {
                Gfx_Text(L"\x2026", 1, x + S(4), y, ellipsis, h, f, T.text3, DT_SINGLELINE | DT_VCENTER);
                return -1;
            }
        }
        if (gap > S(3)) Gfx_Text(L"\x203A", 1, x, y, gap, h, f, T.text3, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        x += gap;
        Gfx_Round(x, y, cw, h, (float)r, stroke);
        Gfx_Round(x + 1, y + 1, cw - 2, h - 3, (float)(r - 1), bg);   // thicker bottom edge reads as a keycap
        Gfx_Text(t[i].name, -1, x, y, cw, h - 1, f, fg, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        x += cw;
    }
    return x;
}

// What an "open" macro launches, as a chip: the app's icon (or a glyph) and its name.
static void DrawTargetChip(const WCHAR* target, HICON icon, int x, int y, int right, int h, bool dim, COLORREF surface)
{
    COLORREF bg, stroke, fg;
    ChipColors(CHIP_KEY, dim, surface, &bg, &stroke, &fg);
    WCHAR label[NAME_LEN];
    TargetLabel(target, label, NAME_LEN);
    int pad = S(7), iw = S(16), gap = S(6), r = S(4);
    int cw = Min(pad + iw + gap + Gfx_TextW(label, -1, fSmall) + pad, right - x);
    if (cw < h) return;
    Gfx_Round(x, y, cw, h, (float)r, stroke);
    Gfx_Round(x + 1, y + 1, cw - 2, h - 3, (float)(r - 1), bg);
    if (icon) Gfx_Icon(icon, x + pad, y + (h - 1 - iw) / 2, iw);
    else Gfx_Text(IsUrl(target) ? GLYPH_GLOBE : GLYPH_APP, 1, x + pad, y, iw, h - 1, fIconSm, fg, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    Gfx_Text(label, -1, x + pad + iw + gap, y, cw - pad - iw - gap - pad / 2, h - 1, fSmall, fg,
             DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
}

static void Label(const WCHAR* text, int x, int y, int w, const WCHAR* hint, COLORREF hintColor)
{
    Gfx_Text(text, -1, x, y, w, S(20), fSemi, T.text, DT_SINGLELINE | DT_VCENTER);
    if (hint) Gfx_Text(hint, -1, x, y, w, S(20), fSmall, hintColor, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
}

// ---- List view --------------------------------------------------------------------

static int CardH() { return S(76); }
static int CardStep() { return S(84); }
static int ListTop() { return S(84); }
static int ListBottom(int H) { return H - S(64); }

static int ListMaxScroll(int H)
{
    int content = g_count * CardStep() - (CardStep() - CardH()) + S(12);
    return Max(0, content - (ListBottom(H) - ListTop()));
}

static void PaintCard(int i, int x, int y, int w, int h)
{
    const Macro& m = g_macros[i];
    int  cardId = HIT(H_CARD, i), toggleId = HIT(H_TOGGLE, i);
    bool hover = s_hover == cardId || s_hover == toggleId;
    COLORREF surface = hover ? T.cardHover : T.card;
    Gfx_Frame(x, y, w, h, (float)S(8), surface, T.cardStroke);
    AddHit(x, y, w, h, cardId);

    int in = S(16);
    int tx = x + w - in - S(40), ty = y + (h - S(20)) / 2;
    DrawToggle(tx, ty, m.enabled != 0, s_hover == toggleId, surface);
    AddHit(tx - S(8), ty - S(8), S(56), S(36), toggleId);

    bool dim = !m.enabled || g_paused;
    int  left = x + in, right = tx - S(16), nameRight = right;
    if (m.failed) {
        const WCHAR* warn = L"Hotkey in use";
        int ww = Gfx_TextW(warn, -1, fSmall);
        Gfx_Text(warn, -1, right - ww, y + S(13), ww, S(20), fSmall, T.danger, DT_SINGLELINE | DT_VCENTER);
        Gfx_Text(GLYPH_WARNING, 1, right - ww - S(18), y + S(13), S(14), S(20), fIconSm, T.danger, DT_SINGLELINE | DT_VCENTER);
        nameRight = right - ww - S(26);
    }
    Gfx_Text(m.name, -1, left, y + S(13), nameRight - left, S(20), fSemi, dim ? T.text3 : T.text,
             DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

    int cy = y + S(42), ch = S(22);
    int n = FormatHotkey(m.mods, m.vk, s_toks, MAX_EVT);
    int cx = DrawChips(s_toks, n, left, cy, right, 0, ch, fSmall, CHIP_TRIGGER, dim, surface);
    if (cx >= 0 && cx + S(48) < right) {
        Gfx_Text(GLYPH_FORWARD, 1, cx, cy, S(28), ch, fIconSm, T.text3, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        if (m.action == ACT_OPEN) {
            DrawTargetChip(m.target, CardIcon(i), cx + S(28), cy, right, ch, dim, surface);
        } else {
            n = FormatEvents(m.evt, m.nEvt, s_toks, MAX_EVT);
            DrawChips(s_toks, n, cx + S(28), cy, right, 0, ch, fSmall, CHIP_KEY, dim, surface);
        }
    }
}

static void PaintList(int W, int H)
{
    int pad = S(20);
    Gfx_Text(L"Macros", -1, pad, S(18), W - 2 * pad, S(30), fTitle, T.text, DT_SINGLELINE | DT_VCENTER);

    WCHAR sub[64];
    if (g_count == 1) lstrcpyW(sub, L"1 macro");
    else if (g_count) wsprintfW(sub, L"%d macros", g_count);
    else lstrcpyW(sub, L"Hotkeys that type for you");
    if (g_paused) lstrcatW(sub, L"  \x00B7  hotkeys paused");
    Gfx_Text(sub, -1, pad, S(48), W - 2 * pad, S(18), fSmall, T.text2, DT_SINGLELINE | DT_VCENTER);

    int bw = ButtonWidth(GLYPH_ADD, L"New macro");
    DrawButton(W - pad - bw, S(26), bw, S(32), GLYPH_ADD, L"New macro", g_count < MAX_MACROS ? BTN_ACCENT : BTN_DISABLED, HIT(H_NEW, 0));

    int top = ListTop(), bottom = ListBottom(H), maxScroll = ListMaxScroll(H);
    s_scroll = Min(Max(s_scroll, 0), maxScroll);
    RECT view = { 0, top, W, bottom };
    Gfx_Clip(&view);
    if (!g_count) {
        int cy = (top + bottom) / 2 - S(16);
        UINT center = DT_SINGLELINE | DT_VCENTER | DT_CENTER;
        Gfx_Text(GLYPH_KEYBOARD, 1, 0, cy - S(70), W, S(56), fIconBig, T.text3, center);
        Gfx_Text(L"No macros yet", -1, 0, cy - S(8), W, S(22), fSemi, T.text, center);
        Gfx_Text(L"Record a few keys, then pick a hotkey that plays them.", -1, 0, cy + S(16), W, S(20), fSmall, T.text2, center);
    }
    int y = top - s_scroll;
    for (int i = 0; i < g_count; ++i, y += CardStep()) {
        if (y + CardH() > top && y < bottom) PaintCard(i, pad, y, W - 2 * pad, CardH());
    }
    Gfx_Clip(NULL);

    if (maxScroll) {
        int trackTop = top + S(4), trackH = bottom - top - S(8);
        int thumbH = Max(S(24), trackH * (bottom - top) / (maxScroll + bottom - top));
        int thumbY = trackTop + (trackH - thumbH) * s_scroll / maxScroll;
        Gfx_Round(W - S(9), thumbY, S(3), thumbH, S(3) / 2.f, T.text3);
    }

    // Footer
    int fy = H - S(64), cy = fy + S(32), ty = cy - S(10);
    Gfx_Fill(0, fy, W, 1, T.divider);

    int autoId = HIT(H_AUTOSTART, 0);
    const WCHAR* autoText = L"Start with Windows";
    int aw = Gfx_TextW(autoText, -1, fBody);
    DrawToggle(pad, ty, g_autostart, s_hover == autoId, T.bg);
    Gfx_Text(autoText, -1, pad + S(52), ty, aw + S(4), S(20), fBody, T.text, DT_SINGLELINE | DT_VCENTER);
    AddHit(pad - S(4), cy - S(16), S(56) + aw + S(4), S(32), autoId);

    int pauseId = HIT(H_PAUSE, 0);
    const WCHAR* pauseText = g_paused ? L"Hotkeys paused" : L"Hotkeys on";
    int pw = Gfx_TextW(pauseText, -1, fBody), px = W - pad - S(40);
    DrawToggle(px, ty, !g_paused, s_hover == pauseId, T.bg);
    Gfx_Text(pauseText, -1, px - S(12) - pw, ty, pw + S(4), S(20), fBody, T.text, DT_SINGLELINE | DT_VCENTER);
    AddHit(px - S(16) - pw, cy - S(16), pw + S(60), S(32), pauseId);
}

// ---- Editor -----------------------------------------------------------------------

// Editor layout, in DIPs.
static const int kNameY = 90, kNameH = 36, kHotkeyY = 166, kHotkeyH = 48, kActionY = 230;
static const int kFieldY = 262, kKeysH = 104, kTargetH = 36, kBrowseW = 100;

// Positions the two text boxes (name, and the target in "open" mode) over their fields.
static void LayoutEdit()
{
    if (!g_edit) return;
    bool editing = s_view == VIEW_EDIT, open = editing && E.action == ACT_OPEN;
    HWND focus = GetFocus();
    if ((focus == g_edit && !editing) || (focus == s_target && !open)) SetFocus(g_wnd);

    RECT rc;
    GetClientRect(g_wnd, &rc);
    int  pad = S(20), fw = rc.right - 2 * pad;
    UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW;
    if (editing) SetWindowPos(g_edit, NULL, pad + S(12), S(kNameY) + (S(kNameH) - s_bodyH) / 2, fw - S(24), s_bodyH, flags);
    else ShowWindow(g_edit, SW_HIDE);
    if (open) {
        int fieldW = fw - S(kBrowseW) - S(8);
        SetWindowPos(s_target, NULL, pad + S(36), S(kFieldY) + (S(kTargetH) - s_bodyH) / 2, fieldW - S(46), s_bodyH, flags);
    } else {
        ShowWindow(s_target, SW_HIDE);
    }
}

static int ModIndex(UINT vk)
{
    switch (vk) {
    case VK_LCONTROL: return 0; case VK_RCONTROL: return 1;
    case VK_LSHIFT:   return 2; case VK_RSHIFT:   return 3;
    case VK_LMENU:    return 4; case VK_RMENU:    return 5;
    case VK_LWIN:     return 6; case VK_RWIN:     return 7;
    }
    return -1;
}

static UINT MaskToMods(UINT mask)
{
    return (mask & 0x03 ? MOD_CONTROL : 0) | (mask & 0x0C ? MOD_SHIFT : 0) |
           (mask & 0x30 ? MOD_ALT : 0) | (mask & 0xC0 ? MOD_WIN : 0);
}

static void CancelCapture()
{
    E.mode = ED_IDLE;
    E.capMask = 0;
    HookFinish();
}

static void FinishRecording()
{
    E.mode = ED_IDLE;
    HookFinish();
    // Release anything still held so playback never leaves a key stuck down.
    for (int vk = 0; vk < 256; ++vk) {
        if (E.lastDown[vk] && E.nEvt < MAX_EVT) {
            KeyEvt e = E.evt[E.lastDown[vk] - 1];
            e.flags |= EVT_UP;
            E.evt[E.nEvt++] = e;
        }
    }
    ZeroMemory(E.lastDown, sizeof(E.lastDown));
    SetMsg(NULL, false);
}

static void StopInput()
{
    if (E.mode == ED_RECORD) FinishRecording();
    else if (E.mode == ED_CAPTURE) CancelCapture();
}

static void StartCapture()
{
    StopInput();
    SetFocus(g_wnd);
    if (!HookStart()) {
        SetMsg(L"EasyMacro couldn't listen to the keyboard.", true);
        return;
    }
    E.mode = ED_CAPTURE;
    E.capMask = 0;
    SetMsg(NULL, false);
}

static void StartRecording()
{
    StopInput();
    SetFocus(g_wnd);
    if (!HookStart()) {
        SetMsg(L"EasyMacro couldn't listen to the keyboard.", true);
        return;
    }
    E.mode = ED_RECORD;
    E.nEvt = 0;
    ZeroMemory(E.lastDown, sizeof(E.lastDown));
    SetMsg(L"Keys you press now are captured here and won't reach other apps until you click Stop.", false);
}

static bool CheckHotkey(UINT mods, UINT vk)
{
    for (int i = 0; i < g_count; ++i) {
        if (i != E.index && g_macros[i].vk == vk && g_macros[i].mods == mods) {
            wsprintfW(E.msg, L"\x201C%s\x201D already uses this hotkey.", g_macros[i].name);
            E.msgError = true;
            return false;
        }
    }
    bool unchanged = E.index >= 0 && g_macros[E.index].mods == mods && g_macros[E.index].vk == vk;
    if (!unchanged && !HotkeyAvailable(mods, vk)) {
        SetMsg(L"Windows or another app already uses this hotkey. Try a different one.", true);
        return false;
    }
    return true;
}

static void SetHotkey(UINT mods, UINT vk)
{
    E.mods = mods;
    E.vk = vk;
    SetMsg(NULL, false);
    if (CheckHotkey(mods, vk) && !mods && IsTypingKey(vk))
        SetMsg(L"Heads up: without Ctrl, Alt, Shift or Win, this key won't type normally while the macro is on.", false);
}

static void OpenEditor(int index)
{
    ZeroMemory(&E, sizeof(E));
    E.index = index;
    if (index >= 0) {
        const Macro& m = g_macros[index];
        E.mods = m.mods;
        E.vk = m.vk;
        E.action = m.action;
        E.nEvt = m.nEvt;
        CopyMemory(E.evt, m.evt, m.nEvt * sizeof(KeyEvt));
        SetWindowTextW(g_edit, m.name);
        SetWindowTextW(s_target, m.target);
    } else {
        SetWindowTextW(g_edit, L"");
        SetWindowTextW(s_target, L"");
    }
    s_view = VIEW_EDIT;
    s_hover = 0;
    LayoutEdit();
    Redraw();
    UpdateWindow(g_wnd);
    RefreshHover();
}

static void CloseEditor()
{
    StopInput();
    s_view = VIEW_LIST;
    LayoutEdit();
    s_hover = 0;
    Redraw();
    UpdateWindow(g_wnd);
    RefreshHover();
}

static void Trim(WCHAR* s)
{
    int len = lstrlenW(s), start = 0;
    while (len > 0 && s[len - 1] == L' ') --len;
    while (start < len && s[start] == L' ') ++start;
    MoveMemory(s, s + start, (len - start) * sizeof(WCHAR));
    s[len - start] = 0;
}

static void SaveEditor()
{
    StopInput();
    GetWindowTextW(s_target, s_targetText, TARGET_LEN);
    Trim(s_targetText);
    bool open = E.action == ACT_OPEN;
    if (!E.vk) { SetMsg(L"Set a hotkey first.", true); return; }
    if (!open && !E.nEvt) { SetMsg(L"Record the keys this macro should send.", true); return; }
    if (open && !s_targetText[0]) { SetMsg(L"Choose an app, file, folder or web address to open.", true); return; }
    if (open && TargetMissing(s_targetText)) { SetMsg(L"EasyMacro can't find that file or folder. Check the path, or use Browse.", true); return; }
    if (!CheckHotkey(E.mods, E.vk)) return;

    int i = E.index;
    if (i < 0) {
        if (g_count >= MAX_MACROS) return;
        i = g_count++;
        ZeroMemory(&g_macros[i], sizeof(Macro));
        g_macros[i].enabled = 1;
        s_scroll = 1 << 20;                 // clamped on paint: shows the new card
    }
    Macro& m = g_macros[i];
    m.mods = E.mods;
    m.vk = E.vk;
    m.action = (BYTE)E.action;
    m.nEvt = (WORD)E.nEvt;
    CopyMemory(m.evt, E.evt, E.nEvt * sizeof(KeyEvt));
    lstrcpynW(m.target, s_targetText, TARGET_LEN);
    GetWindowTextW(g_edit, m.name, NAME_LEN);
    Trim(m.name);
    if (!m.name[0] && open) {
        WCHAR label[NAME_LEN - 5];
        TargetLabel(m.target, label, ARRAYSIZE(label));
        wsprintfW(m.name, L"Open %s", label);
    } else if (!m.name[0]) {
        TokensToText(s_toks, FormatEvents(m.evt, m.nEvt, s_toks, MAX_EVT), m.name, NAME_LEN);
    }
    CloseEditor();
    App_MacrosChanged();
}

static void DeleteMacro()
{
    if (!E.confirmDelete) {
        E.confirmDelete = true;
        return;
    }
    int i = E.index;
    if (i >= 0 && i < g_count) {
        MoveMemory(&g_macros[i], &g_macros[i + 1], (g_count - i - 1) * sizeof(Macro));
        --g_count;
    }
    CloseEditor();
    App_MacrosChanged();
}

// "Send keys | Open app" tabs, with an accent pill under the selected one.
static void DrawSelector(int x, int y)
{
    static const WCHAR* items[2] = { L"Send keys", L"Open app" };
    for (int i = 0; i < 2; ++i) {
        int  id = HIT(H_ACTION, i), tw = Gfx_TextW(items[i], -1, fSemi);
        bool selected = E.action == i;
        Gfx_Text(items[i], -1, x, y, tw + S(2), S(20), fSemi, selected || s_hover == id ? T.text : T.text2, DT_SINGLELINE | DT_VCENTER);
        if (selected) Gfx_Round(x + (tw - S(16)) / 2, y + S(24), S(16), S(3), S(3) / 2.f, T.accent);
        AddHit(x - S(8), y - S(6), tw + S(16), S(36), id);
        x += tw + S(28);
    }
}

// Lets the user pick an app or shortcut. comdlg32 is loaded only when this is first used.
static void BrowseTarget()
{
    typedef BOOL (WINAPI* GetOpenFileNameFn)(OPENFILENAMEW*);
    static GetOpenFileNameFn getOpenFileName;
    StopInput();
    if (!getOpenFileName) {
        if (HMODULE dlg = LoadLibraryExW(L"comdlg32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32))
            getOpenFileName = (GetOpenFileNameFn)GetProcAddress(dlg, "GetOpenFileNameW");
        if (!getOpenFileName) return;
    }
    EnsureCom();
    static WCHAR file[MAX_PATH];
    WCHAR startIn[MAX_PATH];
    file[0] = 0;
    ExpandEnvironmentStringsW(L"%ProgramData%\\Microsoft\\Windows\\Start Menu\\Programs", startIn, MAX_PATH);

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_wnd;
    ofn.lpstrFilter = L"Apps and shortcuts\0*.exe;*.lnk;*.url;*.appref-ms;*.bat;*.cmd;*.ps1;*.msc\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = startIn;
    ofn.lpstrTitle = L"Choose what this hotkey opens";
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NODEREFERENCELINKS | OFN_HIDEREADONLY | OFN_DONTADDTORECENT;
    if (getOpenFileName(&ofn)) {
        SetWindowTextW(s_target, file);
        SetMsg(NULL, false);
    }
}

static void PaintEditor(int W, int H)
{
    int pad = S(20), fw = W - 2 * pad, inset = S(12), chipH = S(28);
    Gfx_Text(E.index < 0 ? L"New macro" : L"Edit macro", -1, pad, S(18), fw, S(30), fTitle, T.text, DT_SINGLELINE | DT_VCENTER);

    // Name
    Label(L"Name", pad, S(66), fw, L"Optional", T.text3);
    DrawField(pad, S(kNameY), fw, S(kNameH), GetFocus() == g_edit ? FIELD_FOCUS : FIELD_NORMAL);
    AddHit(pad, S(kNameY), fw, S(kNameH), HIT(H_NAME, 0));

    // Hotkey
    int  hotId = HIT(H_HOTKEY, 0), hy = S(kHotkeyY), hh = S(kHotkeyH);
    bool capturing = E.mode == ED_CAPTURE;
    Label(L"Hotkey", pad, S(142), fw, capturing ? L"Esc to cancel" : E.vk ? L"Click to change" : NULL, T.text3);
    int state = capturing ? FIELD_ACTIVE : s_hover == hotId ? FIELD_HOVER : FIELD_NORMAL;
    DrawField(pad, hy, fw, hh, state);
    AddHit(pad, hy, fw, hh, hotId);
    COLORREF hotFill = state == FIELD_HOVER ? T.fieldHover : T.field;
    int chipY = hy + (hh - chipH) / 2, right = pad + fw - inset;
    if (capturing && E.capMask) {
        int n = FormatHotkey(MaskToMods(E.capMask), 0, s_toks, MAX_EVT);
        int x = DrawChips(s_toks, n, pad + inset, chipY, right, 0, chipH, fBody, CHIP_TRIGGER, false, hotFill);
        if (x >= 0) Gfx_Text(L"+ \x2026", -1, x + S(8), hy, S(40), hh, fBody, T.text3, DT_SINGLELINE | DT_VCENTER);
    } else if (capturing) {
        Gfx_Text(L"Press the hotkey you want to use\x2026", -1, pad + inset, hy, fw - 2 * inset, hh, fBody, T.text2, DT_SINGLELINE | DT_VCENTER);
    } else if (E.vk) {
        int n = FormatHotkey(E.mods, E.vk, s_toks, MAX_EVT);
        DrawChips(s_toks, n, pad + inset, chipY, right, 0, chipH, fBody, CHIP_TRIGGER, false, hotFill);
    } else {
        Gfx_Text(L"Click here, then press a key combination", -1, pad + inset, hy, fw - 2 * inset, hh, fBody, T.text3, DT_SINGLELINE | DT_VCENTER);
    }

    // What the hotkey does: send keys, or open something
    bool  recording = E.mode == ED_RECORD, open = E.action == ACT_OPEN;
    int   strokes = 0, top = S(kFieldY), bh = S(32), my;
    WCHAR hint[32] = L"";
    for (int i = 0; i < E.nEvt; ++i) strokes += !(E.evt[i].flags & EVT_UP);
    if (recording) lstrcpyW(hint, L"\x25CF  Recording");
    else if (open) hint[0] = 0;
    else if (strokes == 1) lstrcpyW(hint, L"1 keystroke");
    else if (strokes) wsprintfW(hint, L"%d keystrokes", strokes);
    DrawSelector(pad, S(kActionY));
    if (hint[0]) Gfx_Text(hint, -1, pad, S(kActionY), fw, S(20), fSmall, recording ? T.rec : T.text3, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);

    if (open) {
        int th = S(kTargetH), fieldW = fw - S(kBrowseW) - S(8), iw = S(16);
        DrawField(pad, top, fieldW, th, GetFocus() == s_target ? FIELD_FOCUS : FIELD_NORMAL);
        AddHit(pad, top, fieldW, th, HIT(H_TARGET, 0));
        GetWindowTextW(s_target, s_targetText, TARGET_LEN);
        if (HICON icon = EditorIcon(s_targetText)) Gfx_Icon(icon, pad + S(12), top + (th - iw) / 2, iw);
        else Gfx_Text(IsUrl(s_targetText) ? GLYPH_GLOBE : GLYPH_APP, 1, pad + S(12), top, iw, th, fIcon, T.text3, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        DrawButton(pad + fieldW + S(8), top, S(kBrowseW), th, NULL, L"Browse\x2026", BTN_STD, HIT(H_BROWSE, 0));
        Gfx_Text(L"An app, file, folder or web address. To pass arguments, put the path in quotes first, "
                 L"e.g. \"C:\\Tools\\app.exe\" --new-window",
                 -1, pad, top + th + S(10), fw, S(40), fSmall, T.text3, DT_WORDBREAK);
        my = top + th + S(62);
    } else {
        int kh = S(kKeysH);
        DrawField(pad, top, fw, kh, recording ? FIELD_RECORD : FIELD_NORMAL);
        if (E.nEvt) {
            int n = FormatEvents(E.evt, E.nEvt, s_toks, MAX_EVT);
            DrawChips(s_toks, n, pad + inset, top + inset, right, top + kh - inset, chipH, fBody, CHIP_KEY, false, T.field);
        } else {
            Gfx_Text(recording ? L"Type the keys now. Everything you press is captured until you click Stop."
                               : L"Click Record, then press the keys you want this hotkey to send.",
                     -1, pad + inset, top + inset, fw - 2 * inset, kh - 2 * inset, fBody, T.text3, DT_WORDBREAK);
        }
        int by = top + kh + S(12);
        if (recording) {
            DrawButton(pad, by, Max(S(104), ButtonWidth(ICON_STOP, L"Stop")), bh, ICON_STOP, L"Stop", BTN_DANGER, HIT(H_RECORD, 0));
        } else {
            const WCHAR* label = E.nEvt ? L"Re-record" : L"Record";
            int rw = Max(S(104), ButtonWidth(ICON_DOT, label));
            DrawButton(pad, by, rw, bh, ICON_DOT, label, BTN_STD, HIT(H_RECORD, 0));
            if (E.nEvt) DrawButton(pad + rw + S(8), by, S(80), bh, NULL, L"Clear", BTN_SUBTLE, HIT(H_CLEAR, 0));
        }
        my = by + bh + S(16);
    }

    if (E.msg[0]) {
        COLORREF c = E.msgError ? T.danger : T.text2;
        Gfx_Text(E.msgError ? GLYPH_WARNING : GLYPH_INFO, 1, pad, my, S(16), S(20), fIcon, c, DT_SINGLELINE | DT_VCENTER);
        Gfx_Text(E.msg, -1, pad + S(26), my + S(1), fw - S(26), S(60), fBody, c, DT_WORDBREAK);
    }

    // Footer
    int fy = H - S(64), y2 = fy + S(16), bw = S(96);
    Gfx_Fill(0, fy, W, 1, T.divider);
    bool valid = E.vk && (open ? GetWindowTextLengthW(s_target) > 0 : E.nEvt > 0) && E.mode == ED_IDLE;
    DrawButton(W - pad - bw, y2, bw, bh, NULL, L"Save", valid ? BTN_ACCENT : BTN_DISABLED, HIT(H_SAVE, 0));
    DrawButton(W - pad - 2 * bw - S(8), y2, bw, bh, NULL, L"Cancel", BTN_STD, HIT(H_CANCEL, 0));
    if (E.index >= 0) {
        const WCHAR* label = E.confirmDelete ? L"Confirm delete" : L"Delete";
        DrawButton(pad, y2, Max(S(96), ButtonWidth(GLYPH_DELETE, label)), bh, GLYPH_DELETE, label,
                   E.confirmDelete ? BTN_DANGER : BTN_SUBTLE_DANGER, HIT(H_DELETE, 0));
    }
}

// ---- Events -----------------------------------------------------------------------

static void OnClick(int id)
{
    int type = id >> 8, i = id & 0xFF;
    if (type != H_DELETE) E.confirmDelete = false;
    switch (type) {
    case H_NEW:       if (g_count < MAX_MACROS) OpenEditor(-1); break;
    case H_CARD:      OpenEditor(i); break;
    case H_TOGGLE:    g_macros[i].enabled ^= 1; App_MacrosChanged(); break;
    case H_AUTOSTART: App_SetAutostart(!g_autostart); break;
    case H_PAUSE:     App_SetPaused(!g_paused); break;
    case H_HOTKEY:    if (E.mode == ED_CAPTURE) CancelCapture(); else StartCapture(); break;
    case H_ACTION:
        if (E.action != i) {
            StopInput();
            E.action = i;
            SetMsg(NULL, false);
            LayoutEdit();
        }
        break;
    case H_RECORD:    if (E.mode == ED_RECORD) FinishRecording(); else StartRecording(); break;
    case H_CLEAR:     E.nEvt = 0; SetMsg(NULL, false); break;
    case H_BROWSE:    BrowseTarget(); break;
    case H_DELETE:    DeleteMacro(); break;
    case H_CANCEL:    CloseEditor(); break;
    case H_SAVE:      SaveEditor(); break;
    }
    Redraw();
}

LRESULT UI_Mouse(UINT msg, WPARAM wp, LPARAM lp)
{
    int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
    switch (msg) {
    case WM_MOUSEMOVE: {
        if (!s_tracking) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, g_wnd, 0 };
            s_tracking = TrackMouseEvent(&tme) != FALSE;
        }
        int id = HitAt(x, y);
        if (id != s_hover) {
            s_hover = id;
            Redraw();
        }
        break;
    }
    case WM_MOUSELEAVE:
        s_tracking = false;
        if (s_hover && GetCapture() != g_wnd) {
            s_hover = 0;
            Redraw();
        }
        break;
    case WM_LBUTTONDOWN: {
        s_press = HitAt(x, y);
        SetCapture(g_wnd);
        int type = s_press >> 8;
        if (E.mode == ED_CAPTURE && type != H_HOTKEY) CancelCapture();
        if (type == H_NAME || type == H_TARGET) {
            HWND edit = type == H_NAME ? g_edit : s_target;
            SetFocus(edit);
            int len = GetWindowTextLengthW(edit);
            SendMessageW(edit, EM_SETSEL, len, len);
        } else if (GetFocus() == g_edit || GetFocus() == s_target) {
            SetFocus(g_wnd);
        }
        Redraw();
        break;
    }
    case WM_LBUTTONUP: {
        int pressed = s_press;
        s_press = 0;
        if (GetCapture() == g_wnd) ReleaseCapture();
        if (pressed && HitAt(x, y) == pressed) OnClick(pressed);
        Redraw();
        break;
    }
    case WM_MOUSEWHEEL:
        if (s_view == VIEW_LIST) {
            RECT rc;
            GetClientRect(g_wnd, &rc);
            int before = s_scroll;
            s_scroll -= MulDiv(GET_WHEEL_DELTA_WPARAM(wp), S(84), WHEEL_DELTA);
            s_scroll = Min(Max(s_scroll, 0), ListMaxScroll(rc.bottom));
            if (s_scroll != before) {
                Redraw();
                UpdateWindow(g_wnd);
                RefreshHover();
            }
        }
        break;
    }
    return 0;
}

bool UI_WantsIBeam() { return s_view == VIEW_EDIT && (s_hover == HIT(H_NAME, 0) || s_hover == HIT(H_TARGET, 0)); }

bool UI_KeyDown(WPARAM vk)
{
    if (!IsWindowVisible(g_wnd)) return false;
    if (s_view == VIEW_EDIT) {
        if (vk == VK_ESCAPE) { CloseEditor(); return true; }
        if (vk == VK_RETURN) { SaveEditor(); Redraw(); return true; }
        return false;
    }
    if (vk == VK_ESCAPE) { App_Hide(); return true; }
    if (vk == 'N' && GetKeyState(VK_CONTROL) < 0) {
        if (g_count < MAX_MACROS) OpenEditor(-1);
        return true;
    }
    return false;
}

void UI_HookKey(WPARAM info, LPARAM scan)
{
    if (s_view != VIEW_EDIT) return;
    UINT vk = info & 0xFF;
    bool up = (info & 0x100) != 0, ext = (info & 0x200) != 0;
    if (E.mode == ED_RECORD) {
        if (up && !E.lastDown[vk]) return;          // released a key pressed before recording began
        if (E.nEvt >= MAX_EVT - 16) {
            FinishRecording();
            SetMsg(L"That's as long as a macro can be, so recording stopped.", false);
        } else {
            E.lastDown[vk] = up ? 0 : (WORD)(E.nEvt + 1);
            KeyEvt& e = E.evt[E.nEvt++];
            e.vk = (BYTE)vk;
            e.scan = (BYTE)scan;
            e.flags = (BYTE)((up ? EVT_UP : 0) | (ext ? EVT_EXT : 0));
        }
    } else if (E.mode == ED_CAPTURE) {
        int mi = ModIndex(vk);
        if (mi >= 0) {
            if (up) E.capMask &= ~(1u << mi);
            else E.capMask |= 1u << mi;
        } else if (!up) {
            UINT mods = MaskToMods(E.capMask);
            CancelCapture();
            if (vk != VK_ESCAPE || mods) SetHotkey(mods, vk);
        }
    }
    Redraw();
}

void UI_EditNotify(UINT id, UINT code)
{
    HWND edit = id == ID_TARGET ? s_target : g_edit;
    if (code == EN_SETFOCUS) StopInput();
    if (code == EN_SETFOCUS || code == EN_KILLFOCUS) {
        InvalidateRect(edit, NULL, TRUE);
        Redraw();
    }
    if (code == EN_CHANGE && id == ID_TARGET) Redraw();    // icon and Save button follow the text
}

LRESULT UI_EditColor(HDC dc, HWND edit)
{
    bool focus = GetFocus() == edit;
    SetTextColor(dc, T.text);
    SetBkColor(dc, focus ? T.fieldFocus : T.field);
    return (LRESULT)(focus ? s_fieldFocusBrush : s_fieldBrush);
}

void UI_Deactivated()
{
    if (E.mode != ED_IDLE) {
        StopInput();
        Redraw();
    }
}

void UI_Hidden()
{
    StopInput();
    s_hover = s_press = 0;
    FreeIcons();
    Gfx_Free();
}

void UI_Refresh() { Redraw(); }

void UI_MacrosChanged()
{
    FreeIcons();                // card positions may have shifted
    Redraw();
}

void UI_SetDpi(UINT dpi)
{
    if (dpi == s_dpi && fBody) return;
    s_dpi = dpi;
    CreateFonts();
    Gfx_Free();
    LayoutEdit();
    Redraw();
}

void UI_Paint(HDC dc)
{
    RECT rc;
    GetClientRect(g_wnd, &rc);
    if (!Gfx_Begin(dc, rc.right, rc.bottom)) return;
    s_nHits = 0;
    Gfx_Fill(0, 0, rc.right, rc.bottom, T.bg);
    if (s_view == VIEW_LIST) PaintList(rc.right, rc.bottom);
    else PaintEditor(rc.right, rc.bottom);
    Gfx_Present(dc);
}

void UI_Create(HWND wnd)
{
    s_dpi = GetDpiForWindow(wnd);
    s_fluentIcons = FontExists(L"Segoe Fluent Icons");
    UI_LoadTheme();
    g_edit = CreateWindowExW(0, L"EDIT", NULL, WS_CHILD | ES_AUTOHSCROLL, 0, 0, 0, 0, wnd, (HMENU)(INT_PTR)ID_EDIT, g_inst, NULL);
    SendMessageW(g_edit, EM_SETLIMITTEXT, NAME_LEN - 1, 0);
    SendMessageW(g_edit, EM_SETCUEBANNER, FALSE, (LPARAM)L"e.g. Copy and paste");
    s_target = CreateWindowExW(0, L"EDIT", NULL, WS_CHILD | ES_AUTOHSCROLL, 0, 0, 0, 0, wnd, (HMENU)(INT_PTR)ID_TARGET, g_inst, NULL);
    SendMessageW(s_target, EM_SETLIMITTEXT, TARGET_LEN - 1, 0);
    SendMessageW(s_target, EM_SETCUEBANNER, FALSE, (LPARAM)L"e.g. notepad, or https://github.com");
    CreateFonts();
    SendMessageW(g_edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    SendMessageW(s_target, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
}
