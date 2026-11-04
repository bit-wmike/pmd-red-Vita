/*
 * Game data from the user's ROM.
 *
 * The port's binary contains the game's code but none of its graphics, maps,
 * text or sound samples. The build registers every data array with its size
 * and a hash of its contents (tools/port/gen_data.py for the assembler data
 * files, tools/port/pp_assets.py for INCBIN arrays in C). At start the ROM
 * is scanned for those contents, the arrays are filled and the pointers
 * inside the assembler data get their host addresses. The ROM offsets found
 * are cached in romindex.txt, so later starts only copy.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "port/port.h"
#include "port/port_assets.h"

#ifndef PMD_PORT_VERSION
#define PMD_PORT_VERSION "dev"
#endif

/* Pokémon Mystery Dungeon: Red Rescue Team (USA) - the version this build
 * is made for (the decomp's baserom). */
static const unsigned char sExpectedSha1[20] = {
    0x9f, 0x4c, 0xfc, 0x5b, 0x5f, 0x48, 0x59, 0xd1, 0x71, 0x69,
    0xa4, 0x85, 0x46, 0x2e, 0x97, 0x7c, 0x7a, 0xac, 0x2b, 0x89,
};

struct Asset {
    const char *name;
    unsigned char *dst;
    uint32_t size;
    uint64_t hash;
    uint64_t key1, key2;
    int32_t keyOff1, keyOff2;
    const uint32_t *holes;
    uint32_t holeCount;
    uint32_t romAddr; /* 0 = unknown */
    int found;
    int next; /* hash chain */
};

static struct Asset *sAssets;
static size_t sAssetCount, sAssetCap;

static struct Asset *NewAsset(void)
{
    if (sAssetCount == sAssetCap) {
        sAssetCap = sAssetCap ? sAssetCap * 2 : 4096;
        sAssets = (struct Asset *)realloc(sAssets, sAssetCap * sizeof(*sAssets));
    }
    memset(&sAssets[sAssetCount], 0, sizeof(struct Asset));
    return &sAssets[sAssetCount++];
}

void PortAsset_Add(const char *name, void *dst, unsigned size, unsigned long long hash,
                   unsigned long long key1, int keyOff1, unsigned long long key2, int keyOff2)
{
    struct Asset *a = NewAsset();
    a->name = name;
    a->dst = (unsigned char *)dst;
    a->size = size;
    a->hash = hash;
    a->key1 = key1;
    a->keyOff1 = keyOff1;
    a->key2 = key2;
    a->keyOff2 = keyOff2;
}

/* ---- SHA-1 (to recognise the supported ROM) ---- */

struct Sha1 {
    uint32_t h[5];
    unsigned char buf[64];
};

static uint32_t Rol(uint32_t x, int n)
{
    return (x << n) | (x >> (32 - n));
}

static void Sha1Block(struct Sha1 *s, const unsigned char *p)
{
    uint32_t w[80], a, b, c, d, e;
    int i;
    for (i = 0; i < 16; i++)
        w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
    for (i = 16; i < 80; i++)
        w[i] = Rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    a = s->h[0]; b = s->h[1]; c = s->h[2]; d = s->h[3]; e = s->h[4];
    for (i = 0; i < 80; i++) {
        uint32_t f, k, t;
        if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else { f = b ^ c ^ d; k = 0xCA62C1D6; }
        t = Rol(a, 5) + f + e + k + w[i];
        e = d; d = c; c = Rol(b, 30); b = a; a = t;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e;
}

void Port_Sha1(const void *data, size_t len, unsigned char out[20])
{
    struct Sha1 s = {{0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0}, {0}};
    const unsigned char *p = (const unsigned char *)data;
    uint64_t bits = (uint64_t)len * 8;
    size_t i;
    while (len >= 64) {
        Sha1Block(&s, p);
        p += 64;
        len -= 64;
    }
    memcpy(s.buf, p, len);
    s.buf[len++] = 0x80;
    if (len > 56) {
        memset(s.buf + len, 0, 64 - len);
        Sha1Block(&s, s.buf);
        len = 0;
    }
    memset(s.buf + len, 0, 56 - len);
    for (i = 0; i < 8; i++)
        s.buf[56 + i] = (unsigned char)(bits >> (56 - 8 * i));
    Sha1Block(&s, s.buf);
    for (i = 0; i < 5; i++) {
        out[i * 4] = (unsigned char)(s.h[i] >> 24);
        out[i * 4 + 1] = (unsigned char)(s.h[i] >> 16);
        out[i * 4 + 2] = (unsigned char)(s.h[i] >> 8);
        out[i * 4 + 3] = (unsigned char)s.h[i];
    }
}

int Port_RomMatches(const void *rom, size_t size)
{
    unsigned char sha[20];
    if (size != PORT_ROM_SIZE)
        return 0;
    Port_Sha1(rom, size, sha);
    return memcmp(sha, sExpectedSha1, 20) == 0;
}

/* ---- locating the data ---- */

static uint64_t Fnv64(const unsigned char *p, size_t n)
{
    uint64_t h = 0xcbf29ce484222325ull;
    while (n--) {
        h ^= *p++;
        h *= 0x100000001b3ull;
    }
    return h;
}

static uint64_t Load64(const unsigned char *p)
{
    uint64_t v;
    memcpy(&v, p, 8);
    return v;
}

/* Copies the candidate ROM region into the array, zeroes the holes and
 * checks the hash. On success the data stays in place. */
static int Verify(struct Asset *a, uint32_t romOff)
{
    uint32_t i;
    if ((uint64_t)romOff + a->size > gHostRomSize)
        return 0;
    memcpy(a->dst, gHostRom + romOff, a->size);
    for (i = 0; i < a->holeCount; i++) {
        uint32_t off = a->holes[i * 2], len = a->holes[i * 2 + 1];
        if (off + len <= a->size)
            memset(a->dst + off, 0, len);
    }
    if (Fnv64(a->dst, a->size) != a->hash) {
        memset(a->dst, 0, a->size);
        return 0;
    }
    a->romAddr = romOff + 0x08000000u;
    a->found = 1;
    return 1;
}

static int TryAt(struct Asset *a, uint32_t p)
{
    if (a->found || (uint64_t)p + a->size > gHostRomSize)
        return 0;
    if (a->keyOff2 >= 0 && Load64(gHostRom + p + a->keyOff2) != a->key2)
        return 0;
    return Verify(a, p);
}

#define TABLE_BITS 15

static int *sBuckets;

static uint64_t StrHash(const char *s)
{
    uint64_t h = 0xcbf29ce484222325ull;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 0x100000001b3ull;
    }
    return h;
}

static unsigned Bucket(uint64_t k)
{
    k ^= k >> 29;
    k *= 0x9E3779B97F4A7C15ull;
    return (unsigned)(k >> (64 - TABLE_BITS));
}

static void Scan(int step)
{
    size_t i, n = 0;
    uint32_t q, end;
    if (!sBuckets)
        sBuckets = (int *)malloc(sizeof(int) << TABLE_BITS);
    memset(sBuckets, 0xFF, sizeof(int) << TABLE_BITS);
    for (i = 0; i < sAssetCount; i++) {
        struct Asset *a = &sAssets[i];
        unsigned b;
        if (a->found || a->keyOff1 < 0)
            continue;
        b = Bucket(a->key1);
        a->next = sBuckets[b];
        sBuckets[b] = (int)i;
        n++;
    }
    if (n == 0)
        return;
    end = (uint32_t)(gHostRomSize - 8);
    for (q = 0; q <= end; q += (uint32_t)step) {
        uint64_t k = Load64(gHostRom + q);
        int idx = sBuckets[Bucket(k)];
        while (idx >= 0) {
            struct Asset *a = &sAssets[idx];
            if (!a->found && a->key1 == k && q >= (uint32_t)a->keyOff1)
                TryAt(a, q - (uint32_t)a->keyOff1);
            idx = a->next;
        }
    }
}

/* ---- the cached index ---- */

static void IndexHeader(char *buf, size_t size)
{
    unsigned char sha[20];
    int i, n;
    Port_Sha1(gHostRom, gHostRomSize, sha);
    n = snprintf(buf, size, "pmdidx 1 %s", PMD_PORT_VERSION);
    for (i = 0; i < 20 && n < (int)size; i++)
        n += snprintf(buf + n, size - n, "%02x", sha[i]);
}

static void LoadIndex(void)
{
    static char text[1 << 20];
    char header[160];
    long n = Platform_ReadFile("romindex.txt", text, sizeof(text) - 1);
    char *line;
    size_t hits = 0, i;
    if (n <= 0)
        return;
    text[n] = 0;
    IndexHeader(header, sizeof(header));
    line = strtok(text, "\n");
    if (!line || strcmp(line, header) != 0) {
        Platform_Log("assets: romindex.txt is for another ROM or version\n");
        return;
    }
    /* "name romAddr" per line, looked up through a name hash table */
    if (!sBuckets)
        sBuckets = (int *)malloc(sizeof(int) << TABLE_BITS);
    memset(sBuckets, 0xFF, sizeof(int) << TABLE_BITS);
    for (i = 0; i < sAssetCount; i++) {
        unsigned b = Bucket(StrHash(sAssets[i].name));
        sAssets[i].next = sBuckets[b];
        sBuckets[b] = (int)i;
    }
    while ((line = strtok(NULL, "\n")) != NULL) {
        char *sp = strchr(line, ' ');
        unsigned long addr;
        int idx;
        if (!sp)
            continue;
        *sp = 0;
        addr = strtoul(sp + 1, NULL, 16);
        for (idx = sBuckets[Bucket(StrHash(line))]; idx >= 0; idx = sAssets[idx].next) {
            struct Asset *a = &sAssets[idx];
            if (a->found || strcmp(a->name, line) != 0)
                continue;
            if (addr >= 0x08000000ul && Verify(a, (uint32_t)(addr - 0x08000000ul)))
                hits++;
            break;
        }
    }
    Platform_Log("assets: %u of %u blocks from romindex.txt\n", (unsigned)hits, (unsigned)sAssetCount);
}

static void SaveIndex(void)
{
    size_t cap = 64 + sAssetCount * 80, n = 0, i;
    char *text = (char *)malloc(cap);
    if (!text)
        return;
    IndexHeader(text, cap);
    n = strlen(text);
    text[n++] = '\n';
    for (i = 0; i < sAssetCount; i++) {
        struct Asset *a = &sAssets[i];
        if (!a->found)
            continue;
        n += (size_t)snprintf(text + n, cap - n, "%s %08x\n", a->name, a->romAddr);
    }
    if (Platform_WriteFile("romindex.txt", text, n) != 0)
        Platform_Log("assets: cannot write romindex.txt\n");
    free(text);
}

/* ---- loading ---- */

const void *Port_RomPtr(const void *p)
{
    uintptr_t v = (uintptr_t)p;
    if (v >= 0x08000000u && v < 0x08000000u + gHostRomSize && gHostRom)
        return gHostRom + (v - 0x08000000u);
    return p;
}

static void Relocate(void)
{
    unsigned i;
    for (i = 0; i < gPortRelocCount; i++) {
        uint32_t v = (uint32_t)(uintptr_t)(gPortRelocs[i].target + gPortRelocs[i].addend);
        memcpy(gPortRelocs[i].where, &v, 4);
    }
}

int Port_LoadRom(unsigned char *rom, size_t size)
{
    size_t i, missing = 0, known = 0;
    uint64_t t0 = Platform_TimeUs();

    gHostRom = rom;
    gHostRomSize = size;

    /* the assembler data blocks */
    for (i = 0; i < gPortBlobCount; i++) {
        const struct PortBlob *b = &gPortBlobs[i];
        struct Asset *a = NewAsset();
        a->name = b->name;
        a->dst = b->dst;
        a->size = b->size;
        a->hash = b->hash;
        a->key1 = b->key1;
        a->keyOff1 = b->keyOff1;
        a->key2 = b->key2;
        a->keyOff2 = b->keyOff2;
        a->holes = b->holes;
        a->holeCount = b->holeCount;
        a->romAddr = b->romAddr;
    }
    /* the INCBIN arrays of the C files */
    PortAssets_RegisterAll();

    /* 1. constant blocks need no ROM; blocks whose address the decomp states */
    for (i = 0; i < sAssetCount; i++) {
        struct Asset *a = &sAssets[i];
        if (a->keyOff1 == -2) {
            memset(a->dst, (int)(a->key1 & 0xFF), a->size);
            a->found = 1;
            known++;
            continue;
        }
        if (a->romAddr >= 0x08000000u) {
            if (Verify(a, a->romAddr - 0x08000000u))
                known++;
            else
                Platform_Log("assets: %s is not at %08x\n", a->name, a->romAddr);
        }
    }
    /* 2. the cached index, 3. scanning the ROM */
    LoadIndex();
    for (i = 0; i < sAssetCount; i++)
        if (!sAssets[i].found)
            missing++;
    if (missing) {
        Platform_Log("assets: scanning the ROM for %u blocks\n", (unsigned)missing);
        Scan(4);
        missing = 0;
        for (i = 0; i < sAssetCount; i++)
            if (!sAssets[i].found)
                missing++;
        if (missing)
            Scan(1);
        SaveIndex();
    }
    missing = 0;
    for (i = 0; i < sAssetCount; i++) {
        if (!sAssets[i].found) {
            if (missing < 20)
                Platform_Log("assets: not found in the ROM: %s (%u bytes)\n", sAssets[i].name, sAssets[i].size);
            missing++;
        }
    }
    Relocate();
    Platform_Log("assets: %u blocks, %u by address, %u missing, %u relocations, %u ms\n", (unsigned)sAssetCount,
                 (unsigned)known, (unsigned)missing, gPortRelocCount, (unsigned)((Platform_TimeUs() - t0) / 1000));
    free(sAssets);
    sAssets = NULL;
    sAssetCount = sAssetCap = 0;
    free(sBuckets);
    sBuckets = NULL;
    return 0;
}
