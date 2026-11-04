/*
 * GBA BIOS functions used by the game (libagbsyscall), implemented in C.
 */
#include <string.h>

#include "port/port.h"
#include "gba/types.h"

/* SWI 0x0B */
void CpuSet(const void *src, void *dst, u32 control)
{
    u32 count = control & 0x1FFFFF;
    int fill = (control >> 24) & 1;
    if (control & (1 << 26)) {
        u32 *d = (u32 *)dst;
        const u32 *s = (const u32 *)src;
        if (fill) {
            u32 v = *s;
            while (count--)
                *d++ = v;
        } else {
            memmove(d, s, count * 4);
        }
    } else {
        u16 *d = (u16 *)dst;
        const u16 *s = (const u16 *)src;
        if (fill) {
            u16 v = *s;
            while (count--)
                *d++ = v;
        } else {
            memmove(d, s, count * 2);
        }
    }
}

/* SWI 0x0C: words in blocks of 8 */
void CpuFastSet(const void *src, void *dst, u32 control)
{
    u32 count = (control & 0x1FFFFF);
    u32 *d = (u32 *)dst;
    const u32 *s = (const u32 *)src;
    count = (count + 7) & ~7u;
    if (control & (1 << 24)) {
        u32 v = *s;
        while (count--)
            *d++ = v;
    } else {
        memmove(d, s, count * 4);
    }
}

s32 Div(s32 num, s32 denom)
{
    return denom ? num / denom : 0;
}

s32 Mod(s32 num, s32 denom)
{
    return denom ? num % denom : 0;
}

u16 Sqrt(u32 number)
{
    u32 r = 0, b = 1u << 30;
    while (b > number)
        b >>= 2;
    while (b) {
        if (number >= r + b) {
            number -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    return (u16)r;
}

static void Lz77(const void *src, void *dst)
{
    const u8 *s = (const u8 *)src;
    u8 *d = (u8 *)dst;
    u32 size = s[1] | (s[2] << 8) | (s[3] << 16);
    u32 out = 0;
    s += 4;
    while (out < size) {
        u8 flags = *s++;
        int i;
        for (i = 0; i < 8 && out < size; i++, flags <<= 1) {
            if (flags & 0x80) {
                u32 len = (s[0] >> 4) + 3;
                u32 disp = (((s[0] & 0xF) << 8) | s[1]) + 1;
                s += 2;
                while (len-- && out < size) {
                    d[out] = d[out - disp];
                    out++;
                }
            } else {
                d[out++] = *s++;
            }
        }
    }
}

void LZ77UnCompWram(const void *src, void *dst)
{
    Lz77(src, dst);
}

void LZ77UnCompVram(const void *src, void *dst)
{
    Lz77(src, dst);
}

static void Rl(const void *src, void *dst)
{
    const u8 *s = (const u8 *)src;
    u8 *d = (u8 *)dst;
    u32 size = s[1] | (s[2] << 8) | (s[3] << 16);
    u32 out = 0;
    s += 4;
    while (out < size) {
        u8 flag = *s++;
        u32 len;
        if (flag & 0x80) {
            u8 v = *s++;
            len = (flag & 0x7F) + 3;
            while (len-- && out < size)
                d[out++] = v;
        } else {
            len = (flag & 0x7F) + 1;
            while (len-- && out < size)
                d[out++] = *s++;
        }
    }
}

void RLUnCompWram(const void *src, void *dst)
{
    Rl(src, dst);
}

void RLUnCompVram(const void *src, void *dst)
{
    Rl(src, dst);
}

void SoundBiasReset(void)
{
    *(u16 *)(gHostIo + 0x88) &= 0xFC00;
}

void SoundBiasSet(void)
{
    *(u16 *)(gHostIo + 0x88) = (*(u16 *)(gHostIo + 0x88) & 0xFC00) | 0x200;
}

void SoftReset(u32 resetFlags)
{
    (void)resetFlags;
    HostSyscall(0);
}

void RegisterRamReset(u32 resetFlags)
{
    if (resetFlags & 0x04)
        memset(gHostPalram, 0, sizeof(gHostPalram));
    if (resetFlags & 0x08)
        memset(gHostVram, 0, sizeof(gHostVram));
    if (resetFlags & 0x10)
        memset(gHostOam, 0, sizeof(gHostOam));
}

int MultiBoot(void *mp)
{
    (void)mp;
    return 1;
}
