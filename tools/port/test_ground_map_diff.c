/* Differential test for the translated ground-map decoder (sub_80A5204):
 * original Thumb routine under qemu-arm vs. the generated C (ARM and x86-32).
 * Linked with build/port-gen/ground_map_asm.s (reference) or ground_map_asm.c. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gba/types.h"
#include "structs/str_ground_bg.h"

u8 *Port_sub_80A5204(void *a, const u8 *b, BmaHeader *c, s32 d);
/* referenced by the generic address mapping of the generated code, never hit here */
unsigned char gHostIo[0x400], gHostIwram[0x8000], gHostEwram[0x40000];

static u32 rng = 0x12345678u;
static u32 rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static u32 fnv(u32 h, const void *p, u32 n)
{
    const u8 *b = p;
    while (n--) h = (h ^ *b++) * 16777619u;
    return h;
}

#define OUT_SIZE (256 * (4 + 80) + 0x2000)
#define IN_SIZE  0x20000

int main(void)
{
    u8 *out = malloc(OUT_SIZE), *in = malloc(IN_SIZE);
    int k;
    setvbuf(stdout, NULL, _IONBF, 0);
    for (k = 0; k < 300; k++) {
        BmaHeader hdr;
        u32 i, h, n;
        u8 *ret;
        s32 d;
        memset(&hdr, 0, sizeof(hdr));
        hdr.mapWidthTiles = (u8)(k < 4 ? k : 1 + rnd() % 60);
        hdr.mapHeightTiles = (u8)(k < 4 ? 1 : 1 + rnd() % 40);
        hdr.tilingWidth = hdr.tilingHeight = 3;
        hdr.numLayers = 1 + (rnd() & 1);
        hdr.hasDataLayer = (s16)(k & 1);
        hdr.hasCollision = (s16)(rnd() % 3);
        d = hdr.mapHeightTiles + (s32)(rnd() % 9);
        for (i = 0; i < IN_SIZE; i++) {
            u32 r = rnd();
            /* mostly small run lengths, sometimes anything */
            in[i] = (r & 0x300) ? (u8)(r % 0xD0) : (u8)r;
        }
        memset(out, 0, OUT_SIZE);
        ret = Port_sub_80A5204(out, in, &hdr, d);
        n = (u32)(ret - in);
        h = fnv(2166136261u, out, OUT_SIZE);
        h = fnv(h, &n, 4);
        printf("%3d w=%2u h=%2u dl=%d col=%d d=%d consumed=%u hash=%08x\n", k, hdr.mapWidthTiles, hdr.mapHeightTiles,
               hdr.hasDataLayer, hdr.hasCollision, d, n, h);
        if (n > IN_SIZE) { printf("input overrun\n"); return 1; }
    }
    return 0;
}
