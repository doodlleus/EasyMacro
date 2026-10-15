// A minimal software renderer: antialiased rounded rectangles drawn straight into a
// 32-bit DIB, with GDI handling text (ClearType). No GDI+/Direct2D, so nothing extra loads.
#include "app.h"
#include <xmmintrin.h>

static HDC     s_dc;
static HBITMAP s_bmp, s_old;
static DWORD*  s_px;
static int     s_w, s_h;
static RECT    s_clip;

static inline float Sqrt(float v) { return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(v))); }
static inline DWORD ToPixel(COLORREF c) { return ((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF); }

static inline void Blend(DWORD* d, DWORD s, int a)   // a: 0..256
{
    DWORD v = *d;
    int r = (v >> 16) & 255, g = (v >> 8) & 255, b = v & 255;
    r += (((int)((s >> 16) & 255) - r) * a) >> 8;
    g += (((int)((s >> 8) & 255) - g) * a) >> 8;
    b += (((int)(s & 255) - b) * a) >> 8;
    *d = (DWORD)((r << 16) | (g << 8) | b);
}

COLORREF Mix(COLORREF a, COLORREF b, int t)
{
    int r = GetRValue(a) + (((GetRValue(b) - GetRValue(a)) * t) >> 8);
    int g = GetGValue(a) + (((GetGValue(b) - GetGValue(a)) * t) >> 8);
    int bl = GetBValue(a) + (((GetBValue(b) - GetBValue(a)) * t) >> 8);
    return RGB(r, g, bl);
}

bool Gfx_Begin(HDC wnd, int w, int h)
{
    if (s_dc && w == s_w && h == s_h) {
        Gfx_Clip(NULL);
        return true;
    }
    Gfx_Free();
    if (w <= 0 || h <= 0) return false;

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;                 // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    s_bmp = CreateDIBSection(wnd, &bi, DIB_RGB_COLORS, (void**)&s_px, NULL, 0);
    if (!s_bmp) return false;
    s_dc = CreateCompatibleDC(wnd);
    s_old = (HBITMAP)SelectObject(s_dc, s_bmp);
    SetBkMode(s_dc, TRANSPARENT);
    s_w = w;
    s_h = h;
    Gfx_Clip(NULL);
    return true;
}

void Gfx_Present(HDC wnd)
{
    if (s_dc) BitBlt(wnd, 0, 0, s_w, s_h, s_dc, 0, 0, SRCCOPY);
}

void Gfx_Free()
{
    if (!s_dc) return;
    SelectObject(s_dc, s_old);
    DeleteObject(s_bmp);
    DeleteDC(s_dc);
    s_dc = NULL;
    s_bmp = NULL;
    s_px = NULL;
    s_w = s_h = 0;
}

void Gfx_Clip(const RECT* r)
{
    RECT full = { 0, 0, s_w, s_h };
    if (r) {
        IntersectRect(&s_clip, r, &full);
        HRGN rgn = CreateRectRgnIndirect(&s_clip);
        SelectClipRgn(s_dc, rgn);
        DeleteObject(rgn);
    } else {
        s_clip = full;
        SelectClipRgn(s_dc, NULL);
    }
}

RECT Gfx_GetClip() { return s_clip; }

void Gfx_Fill(int x, int y, int w, int h, COLORREF c)
{
    Gfx_Round(x, y, w, h, 0.f, c);
}

// Fills a rounded rectangle. Edges are pixel aligned, so only the corner arcs need coverage.
void Gfx_Round(int x, int y, int w, int h, float r, COLORREF c, int alpha)
{
    if (!s_px || w <= 0 || h <= 0 || alpha <= 0) return;
    int x0 = Max(x, (int)s_clip.left), x1 = Min(x + w, (int)s_clip.right);
    int y0 = Max(y, (int)s_clip.top), y1 = Min(y + h, (int)s_clip.bottom);
    if (x0 >= x1 || y0 >= y1) return;
    GdiFlush();

    DWORD s = ToPixel(c);
    int a = alpha >= 255 ? 256 : alpha;
    r = Min(r, Min(w * 0.5f, h * 0.5f));
    if (r < 0.f) r = 0.f;
    float L = x + r, R = x + w - r, T = y + r, B = y + h - r;

    for (int py = y0; py < y1; ++py) {
        DWORD* row = s_px + (size_t)py * s_w;
        float fy = py + 0.5f;
        float dy = fy < T ? T - fy : fy > B ? fy - B : 0.f;
        for (int px = x0; px < x1; ++px) {
            int k = a;
            if (dy > 0.f) {
                float fx = px + 0.5f;
                float dx = fx < L ? L - fx : fx > R ? fx - R : 0.f;
                if (dx > 0.f) {
                    float cover = r + 0.5f - Sqrt(dx * dx + dy * dy);
                    if (cover <= 0.f) continue;
                    if (cover < 1.f) k = (int)(k * cover);
                }
            }
            if (k >= 256) row[px] = s;
            else Blend(row + px, s, k);
        }
    }
}

void Gfx_Frame(int x, int y, int w, int h, float r, COLORREF fill, COLORREF stroke, int t)
{
    Gfx_Round(x, y, w, h, r, stroke);
    Gfx_Round(x + t, y + t, w - 2 * t, h - 2 * t, r > t ? r - t : 0.f, fill);
}

void Gfx_Text(const WCHAR* s, int n, int x, int y, int w, int h, HFONT f, COLORREF c, UINT fmt)
{
    if (!s_dc) return;
    RECT rc = { x, y, x + w, y + h };
    SelectObject(s_dc, f);
    SetTextColor(s_dc, c);
    DrawTextW(s_dc, s, n, &rc, fmt | DT_NOPREFIX);
}

int Gfx_TextW(const WCHAR* s, int n, HFONT f)
{
    if (!s_dc) return 0;
    SIZE sz = {};
    SelectObject(s_dc, f);
    GetTextExtentPoint32W(s_dc, s, n < 0 ? lstrlenW(s) : n, &sz);
    return sz.cx;
}
