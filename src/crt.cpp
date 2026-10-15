// The few C runtime symbols the compiler may emit on its own. We link without the CRT,
// which keeps the executable tiny and the process footprint minimal.
#include <intrin.h>
#include <stddef.h>

extern "C" {

int _fltused = 0;

void* __cdecl memset(void* dst, int c, size_t n);
void* __cdecl memcpy(void* dst, const void* src, size_t n);
void* __cdecl memmove(void* dst, const void* src, size_t n);
#pragma function(memset, memcpy, memmove)

void* __cdecl memset(void* dst, int c, size_t n)
{
    __stosb((unsigned char*)dst, (unsigned char)c, n);
    return dst;
}

void* __cdecl memcpy(void* dst, const void* src, size_t n)
{
    __movsb((unsigned char*)dst, (const unsigned char*)src, n);
    return dst;
}

void* __cdecl memmove(void* dst, const void* src, size_t n)
{
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    if (d <= s || d >= s + n) {
        __movsb(d, s, n);
    } else {
        while (n--) d[n] = s[n];
    }
    return dst;
}

}
