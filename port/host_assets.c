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
    const uint64_t *chunks;
    uint32_t chunkCount;
    uint32_t romAddr; /* 0 = unknown */
    int found;        /* 1 = exact, 2 = partial, 3 = through the children, 4 = embedded fallback */
    int parent;       /* index of the containing block, -1 */
    uint32_t childCount;
    const unsigned char *fallback;
    uint32_t capacity; /* C arrays: their real size (room for other versions) */
    int next; /* hash chain */
};
static struct Asset **sByAddress; /* assets without children, sorted by dst */
static size_t sByAddressCount;

enum { SCAN_PARENTS, SCAN_CHILDREN, SCAN_PARTIAL };

unsigned gPortAssetsPartial, gPortAssetsMissing;
#ifdef __vita__
static const size_t sMissingLogLimit = 60;
#else
static const size_t sMissingLogLimit = 100000;
#endif

static struct Asset *sAssets;
static size_t sAssetCount, sAssetCap;

static struct Asset *NewAsset(void)
{
    if (sAssetCount == sAssetCap) {
        sAssetCap = sAssetCap ? sAssetCap * 2 : 4096;
        sAssets = (struct Asset *)realloc(sAssets, sAssetCap * sizeof(*sAssets));
    }
    memset(&sAssets[sAssetCount], 0, sizeof(struct Asset));
    sAssets[sAssetCount].parent = -1;
    return &sAssets[sAssetCount++];
}

void PortAsset_Add(const char *name, void *dst, unsigned size, unsigned capacity, unsigned long long hash,
                   unsigned long long key1, int keyOff1, unsigned long long key2, int keyOff2,
                   const unsigned long long *chunks, unsigned chunkCount)
{
    struct Asset *a = NewAsset();
    a->name = name;
    a->dst = (unsigned char *)dst;
    a->size = size;
    a->capacity = capacity;
    a->hash = hash;
    a->key1 = key1;
    a->keyOff1 = keyOff1;
    a->key2 = key2;
    a->keyOff2 = keyOff2;
    a->chunks = (const uint64_t *)chunks;
    a->chunkCount = chunkCount;
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

int Port_RomVersion(const void *rom, size_t size)
{
    const unsigned char *r = (const unsigned char *)rom;
    if (size != PORT_ROM_SIZE)
        return 0;
    /* cartridge header: game code at 0xAC, "B24x" for Red Rescue Team */
    if (r[0xAC] != 'B' || r[0xAD] != '2' || r[0xAE] != '4')
        return 0;
    return r[0xAF];
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
static unsigned char *sScratch;
static size_t sScratchSize;

static int Verify(struct Asset *a, uint32_t romOff)
{
    uint32_t i;
    if ((uint64_t)romOff + a->size > gHostRomSize)
        return 0;
    /* checked in a scratch buffer: a failed try must not disturb parts of
     * the block that are already in place (children from the index) */
    if (a->size > sScratchSize) {
        sScratchSize = a->size;
        sScratch = (unsigned char *)realloc(sScratch, sScratchSize);
    }
    memcpy(sScratch, gHostRom + romOff, a->size);
    for (i = 0; i < a->holeCount; i++) {
        uint32_t off = a->holes[i * 2], len = a->holes[i * 2 + 1];
        if (off + len <= a->size)
            memset(sScratch + off, 0, len);
    }
    if (Fnv64(sScratch, a->size) != a->hash)
        return 0;
    /* the raw bytes: the holes hold baserom.gba parts (portraits, samples)
     * that the hash cannot know, and pointers that Relocate() patches */
    memcpy(a->dst, gHostRom + romOff, a->size);
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

/* Another ROM version: the block may differ in places (text, fonts). Take
 * the candidate if at least half of its chunks match; the mismatching
 * chunks keep the ROM's bytes, which beats zeros. */
static int TryPartial(struct Asset *a, uint32_t p)
{
    uint32_t i, ok = 0;
    if (a->found || a->chunkCount == 0 || (uint64_t)p + a->size > gHostRomSize)
        return 0;
    if (a->size > sScratchSize) {
        sScratchSize = a->size;
        sScratch = (unsigned char *)realloc(sScratch, sScratchSize);
    }
    memcpy(sScratch, gHostRom + p, a->size);
    for (i = 0; i < a->holeCount; i++) {
        uint32_t off = a->holes[i * 2], len = a->holes[i * 2 + 1];
        if (off + len <= a->size)
            memset(sScratch + off, 0, len);
    }
    for (i = 0; i < a->chunkCount; i++) {
        uint32_t off = i * PORT_CHUNK, len = a->size - off < PORT_CHUNK ? a->size - off : PORT_CHUNK;
        if (Fnv64(sScratch + off, len) == a->chunks[i])
            ok++;
    }
    if (ok * 2 < a->chunkCount)
        return 0;
    memcpy(a->dst, gHostRom + p, a->size); /* raw: see Verify() */
    Platform_Log("assets: %s taken over partly (%u of %u chunks)\n", a->name, ok, a->chunkCount);
    a->romAddr = p + 0x08000000u;
    a->found = 2;
    return 1;
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

/* Which assets a scan pass looks for. */
static int Wanted(const struct Asset *a, int mode)
{
    if (a->found || a->keyOff1 < 0)
        return 0;
    switch (mode) {
        case SCAN_PARENTS: return a->parent < 0;
        case SCAN_CHILDREN: return a->parent >= 0;
        default: return a->chunkCount > 0 && a->childCount == 0 && a->fallback == NULL;
    }
}

static void Scan(int step, int mode)
{
    const int partial = mode == SCAN_PARTIAL;
    size_t i, n = 0;
    uint32_t q, end;
    if (!sBuckets)
        sBuckets = (int *)malloc(sizeof(int) << TABLE_BITS);
    memset(sBuckets, 0xFF, sizeof(int) << TABLE_BITS);
    for (i = 0; i < sAssetCount; i++) {
        struct Asset *a = &sAssets[i];
        unsigned b;
        if (!Wanted(a, mode))
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
            if (!a->found && a->key1 == k && q >= (uint32_t)a->keyOff1) {
                if (partial)
                    TryPartial(a, q - (uint32_t)a->keyOff1);
                else
                    TryAt(a, q - (uint32_t)a->keyOff1);
            }
            idx = a->next;
        }
    }
}

static void BuildAddressIndex(void);
static void IndexRomArchives(void);
static void FillArraysFromRomFiles(void);

/* Parts of a block found as a whole are found too; a block whose parts
 * were all found counts as found. */
static void PropagateParents(void)
{
    size_t i;
    for (i = 0; i < sAssetCount; i++) {
        struct Asset *a = &sAssets[i];
        if (a->parent >= 0 && !a->found && sAssets[a->parent].found == 1)
            a->found = 3;
    }
}

static size_t CountUnfound(void)
{
    size_t i, n = 0;
    for (i = 0; i < sAssetCount; i++) {
        const struct Asset *a = &sAssets[i];
        if (!a->found && !a->childCount)
            n++;
    }
    return n;
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
    static char text[4 << 20];
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
            if (addr >= 0x08000000ul &&
                (Verify(a, (uint32_t)(addr - 0x08000000ul)) || TryPartial(a, (uint32_t)(addr - 0x08000000ul))))
                hits++;
            break;
        }
    }
    Platform_Log("assets: %u of %u blocks from romindex.txt\n", (unsigned)hits, (unsigned)sAssetCount);
}

static void SaveIndex(void)
{
    size_t cap = 64 + sAssetCount * 120, n = 0, i;
    char *text = (char *)malloc(cap);
    if (!text)
        return;
    IndexHeader(text, cap);
    n = strlen(text);
    text[n++] = '\n';
    for (i = 0; i < sAssetCount; i++) {
        struct Asset *a = &sAssets[i];
        if (a->found != 1 && a->found != 2)
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
        size_t parentIndex = sAssetCount, c;
        for (c = 0; c <= b->childCount; c++) {
            const struct PortBlob *src = c == 0 ? b : &b->children[c - 1];
            struct Asset *a = NewAsset();
            a->name = src->name;
            a->dst = src->dst;
            a->size = src->size;
            a->hash = src->hash;
            a->key1 = src->key1;
            a->keyOff1 = src->keyOff1;
            a->key2 = src->key2;
            a->keyOff2 = src->keyOff2;
            a->holes = src->holes;
            a->holeCount = src->holeCount;
            a->chunks = src->chunks;
            a->chunkCount = src->chunkCount;
            a->romAddr = src->romAddr;
            a->childCount = c == 0 ? b->childCount : 0;
            a->parent = c == 0 ? -1 : (int)parentIndex;
            a->fallback = src->fallback;
        }
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
    PropagateParents();
    missing = CountUnfound();
    if (missing) {
        Platform_Log("assets: scanning the ROM for %u blocks\n", (unsigned)missing);
        Scan(4, SCAN_PARENTS);
        if (CountUnfound())
            Scan(1, SCAN_PARENTS);
        PropagateParents();
        if (CountUnfound()) {
            /* another ROM version: the parts of the unmatched blocks */
            Platform_Log("assets: looking for the parts of %u blocks\n", (unsigned)CountUnfound());
            Scan(4, SCAN_CHILDREN);
            if (CountUnfound())
                Scan(1, SCAN_CHILDREN);
        }
        if (CountUnfound()) {
            Platform_Log("assets: looking for partial matches of %u blocks\n", (unsigned)CountUnfound());
            Scan(4, SCAN_PARTIAL);
        }
        SaveIndex();
    }
    IndexRomArchives();
    FillArraysFromRomFiles();
    missing = 0;
    gPortAssetsPartial = 0;
    for (i = 0; i < sAssetCount; i++) {
        struct Asset *a = &sAssets[i];
        if (a->childCount)
            continue; /* judged by its parts */
        if (a->found == 5)
            gPortAssetsPartial++;
        if (!a->found && a->fallback) {
            memcpy(a->dst, a->fallback, a->size);
            a->found = 4;
            gPortAssetsPartial++;
            continue;
        }
        if (a->found == 2)
            gPortAssetsPartial++;
        if (!a->found) {
            if (missing < sMissingLogLimit)
                Platform_Log("assets: not found in the ROM: %s (%u bytes)\n", a->name, a->size);
            missing++;
        }
    }
    gPortAssetsMissing = (unsigned)missing;
    Relocate();
    BuildAddressIndex();
#ifndef __vita__
    {
        const char *dbg = getenv("PMD_ASSET_DEBUG");
        unsigned fb = 0, byState[6] = {0};
        for (i = 0; i < sAssetCount; i++) {
            struct Asset *a = &sAssets[i];
            if (a->found >= 0 && a->found < 6)
                byState[a->found]++;
            if (a->found == 4)
                fb++;
            if (dbg && strstr(a->name, dbg))
                Platform_Log("asset %s: state %d parent %d dst %p size %u fallback %p bytes %02x %02x %02x %02x %02x %02x %02x %02x\n",
                             a->name, a->found, a->parent, (void *)a->dst, a->size, (const void *)a->fallback,
                             a->dst[0], a->dst[1], a->dst[2], a->dst[3], a->dst[4], a->dst[5], a->dst[6], a->dst[7]);
        }
        Platform_Log("assets: states: unfound %u exact %u partial %u via-parent %u fallback %u rom-file %u\n", byState[0], byState[1],
                     byState[2], byState[3], byState[4], byState[5]);
    }
#endif
    Platform_Log("assets: %u blocks, %u by address, %u partial, %u missing, %u relocations, %u ms\n",
                 (unsigned)sAssetCount, (unsigned)known, gPortAssetsPartial, (unsigned)missing, gPortRelocCount,
                 (unsigned)((Platform_TimeUs() - t0) / 1000));
    /* the asset table stays: OpenFile asks about the state of its files */
    free(sBuckets);
    sBuckets = NULL;
    return 0;
}

/* ---- the game's files ---- */

static int CompareByDst(const void *x, const void *y)
{
    const struct Asset *a = *(struct Asset *const *)x, *b = *(struct Asset *const *)y;
    return a->dst < b->dst ? -1 : a->dst > b->dst ? 1 : 0;
}

static void BuildAddressIndex(void)
{
    size_t i, n = 0;
    sByAddress = (struct Asset **)malloc(sAssetCount * sizeof(*sByAddress));
    for (i = 0; i < sAssetCount; i++)
        if (!sAssets[i].childCount)
            sByAddress[n++] = &sAssets[i];
    sByAddressCount = n;
    qsort(sByAddress, n, sizeof(*sByAddress), CompareByDst);
}

int Port_AssetStateAt(const void *p, unsigned *holes)
{
    size_t lo = 0, hi = sByAddressCount;
    const unsigned char *q = (const unsigned char *)p;
    *holes = 0;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (sByAddress[mid]->dst + sByAddress[mid]->size <= q)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < sByAddressCount && sByAddress[lo]->dst <= q) {
        *holes = sByAddress[lo]->holeCount;
        return sByAddress[lo]->found ? sByAddress[lo]->found : -1;
    }
    return 0;
}

struct RomFile {
    uint32_t nameOff;
    uint32_t dataOff;
    uint32_t size;
    uint32_t relocated;
};
static struct RomFile *sRomFiles;
static size_t sRomFileCount;

static int CompareU32(const void *x, const void *y);
static void FillArraysFromRomFiles(void);

static int CompareRomFile(const void *x, const void *y)
{
    const struct RomFile *a = (const struct RomFile *)x, *b = (const struct RomFile *)y;
    return strcmp((const char *)gHostRom + a->nameOff, (const char *)gHostRom + b->nameOff);
}

/* Finds the ROM's "pksdir0" archives and lists their files by name. */
static void IndexRomArchives(void)
{
    size_t p, cap = 0, archives = 0;
    for (p = 0; p + 16 <= gHostRomSize; p += 4) {
        int32_t count;
        uint32_t entries, i;
        if (memcmp(gHostRom + p, "pksdir0", 8) != 0)
            continue;
        memcpy(&count, gHostRom + p + 8, 4);
        memcpy(&entries, gHostRom + p + 12, 4);
        if (count <= 0 || count > 5000 || entries < 0x08000000u || entries - 0x08000000u + (uint32_t)count * 8 > gHostRomSize)
            continue;
        entries -= 0x08000000u;
        for (i = 0; i < (uint32_t)count; i++) {
            uint32_t nameAddr, dataAddr;
            memcpy(&nameAddr, gHostRom + entries + i * 8, 4);
            memcpy(&dataAddr, gHostRom + entries + i * 8 + 4, 4);
            if (nameAddr < 0x08000000u || dataAddr < 0x08000000u || nameAddr - 0x08000000u >= gHostRomSize ||
                dataAddr - 0x08000000u >= gHostRomSize)
                break;
            if (sRomFileCount == cap) {
                cap = cap ? cap * 2 : 1024;
                sRomFiles = (struct RomFile *)realloc(sRomFiles, cap * sizeof(*sRomFiles));
            }
            sRomFiles[sRomFileCount].nameOff = nameAddr - 0x08000000u;
            sRomFiles[sRomFileCount].dataOff = dataAddr - 0x08000000u;
            sRomFileCount++;
        }
        archives++;
    }
    /* sizes: up to the next file's data */
    {
        size_t i, j;
        uint32_t *offs = (uint32_t *)malloc((sRomFileCount + 1) * sizeof(uint32_t));
        for (i = 0; i < sRomFileCount; i++)
            offs[i] = sRomFiles[i].dataOff;
        offs[sRomFileCount] = (uint32_t)gHostRomSize;
        qsort(offs, sRomFileCount + 1, sizeof(uint32_t), CompareU32);
        for (i = 0; i < sRomFileCount; i++) {
            uint32_t o = sRomFiles[i].dataOff;
            size_t lo = 0, hi = sRomFileCount + 1;
            while (lo < hi) {
                size_t mid = (lo + hi) / 2;
                if (offs[mid] <= o)
                    lo = mid + 1;
                else
                    hi = mid;
            }
            j = lo; /* first offset above o */
            sRomFiles[i].size = (j <= sRomFileCount ? offs[j] : (uint32_t)gHostRomSize) - o;
            sRomFiles[i].relocated = 0;
        }
        free(offs);
    }
    qsort(sRomFiles, sRomFileCount, sizeof(*sRomFiles), CompareRomFile);
    Platform_Log("assets: the ROM has %u file archives with %u files\n", (unsigned)archives, (unsigned)sRomFileCount);
}

static int CompareU32(const void *x, const void *y)
{
    uint32_t a = *(const uint32_t *)x, b = *(const uint32_t *)y;
    return a < b ? -1 : a > b ? 1 : 0;
}

static struct RomFile *FindRomFile(const char *name)
{
    size_t lo = 0, hi = sRomFileCount;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        int c = strcmp((const char *)gHostRom + sRomFiles[mid].nameOff, name);
        if (c == 0)
            return &sRomFiles[mid];
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return NULL;
}

unsigned Port_RomFileSize(const char *name)
{
    struct RomFile *f = FindRomFile(name);
    return f ? f->size : 0;
}

void Port_RelocateRomFile(const unsigned char *data, unsigned size)
{
    uint32_t off = (uint32_t)(data - gHostRom), i, n = 0;
    struct RomFile *f = NULL;
    size_t k;
    for (k = 0; k < sRomFileCount; k++)
        if (sRomFiles[k].dataOff == off) {
            f = &sRomFiles[k];
            break;
        }
    if (f && f->relocated)
        return;
    if (f)
        f->relocated = 1;
    for (i = 0; i + 4 <= size; i += 4) {
        uint32_t v;
        memcpy(&v, data + i, 4);
        if (v >= 0x08000000u && v - 0x08000000u < gHostRomSize) {
            const void *host = gHostRom + (v - 0x08000000u);
            memcpy((unsigned char *)data + i, &host, 4);
            n++;
        }
    }
    Platform_Log("file: %u pointers translated in a file of the user's ROM\n", n);
}

/* The European ROM stores the ground tilesets (*.bpc) packed; the US code
 * expects them raw (16-byte header, 32 bytes per tile, chunk cells as u16).
 * Tiles: a byte run-length code with two colour registers.
 *   00..3f  literal, n+1 bytes          40..7d  literal, n+65 bytes
 *   7e nn   literal, nn+1 bytes         7f lo hi literal, 16-bit count+1
 *   80..be vv  vv repeated n+1 times    bf nn vv  vv repeated nn+1 times
 *           (vv becomes register 0, the old register 0 becomes register 1)
 *   c0..de  register 0 repeated n+1     df nn  register 0 repeated nn+1
 *   e0..fe  register 1 repeated n+1, then the registers swap;  ff nn  likewise
 * The cells follow at an even offset (pad byte 0xcd) as two streams, the
 * high bytes and then the low bytes:
 *   00..7f  n+1 zero bytes   80..bf vv  vv repeated n+1   c0..ff  literal n+1
 * Returns the unpacked size, 0 when the data is not a packed tileset (the
 * European ROM packs every tileset; elsewhere a file smaller than the raw
 * size counts as packed). */
static unsigned UnpackEuTiles(unsigned char *dst, unsigned cap, const unsigned char *src, unsigned size, int packed)
{
    unsigned w = src[0] | (src[1] << 8), h = src[2] | (src[3] << 8);
    unsigned numTiles = src[4] | (src[5] << 8), numChunks = src[14] | (src[15] << 8);
    unsigned tl, nw, raw, i, o, n, s, reg0 = 0, reg1 = 0;
    unsigned char *cells;
    if (size < 16 || w != h || (w != 2 && w != 3) || numTiles == 0 || numChunks == 0)
        return 0;
    tl = (numTiles - 1) * 32;
    nw = (numChunks - 1) * w * h;
    raw = 16 + tl + nw * 2;
    if ((size >= raw && !packed) || raw > cap)
        return 0;
    memcpy(dst, src, 16);
    o = 16;
    i = 16;
    while (o < 16 + tl && i < size) {
        unsigned c = src[i++], t = c >> 6;
        n = c & 0x3f;
        if (t <= 1) {
            if (c == 0x7e) {
                n = src[i++] + 1;
            } else if (c == 0x7f) {
                n = (src[i] | (src[i + 1] << 8)) + 1;
                i += 2;
            } else {
                n += t ? 65 : 1;
            }
            if (i + n > size)
                n = size - i;
            if (o + n > 16 + tl)
                n = 16 + tl - o;
            memcpy(dst + o, src + i, n);
            i += n;
            o += n;
        } else if (t == 2) {
            unsigned v;
            if (c == 0xbf)
                n = src[i++] + 1;
            else
                n += 1;
            if (i >= size)
                break;
            v = src[i++];
            if (v != reg0) {
                reg1 = reg0;
                reg0 = v;
            }
            if (o + n > 16 + tl)
                n = 16 + tl - o;
            memset(dst + o, v, n);
            o += n;
        } else {
            unsigned v = c < 0xe0 ? reg0 : reg1;
            n = c & 0x1f;
            if (n == 0x1f)
                n = src[i++] + 1;
            else
                n += 1;
            if (c >= 0xe0) {
                reg1 = reg0;
                reg0 = v;
            }
            if (o + n > 16 + tl)
                n = 16 + tl - o;
            memset(dst + o, v, n);
            o += n;
        }
    }
    if (i & 1)
        i++;
    cells = dst + 16 + tl;
    memset(cells, 0, nw * 2);
    for (s = 0; s < 2; s++) { /* high bytes, then low bytes */
        unsigned k = 0, b = 1 - s;
        while (k < nw && i < size) {
            unsigned c = src[i++];
            n = (c & 0x3f) + 1;
            if (c < 0x80) {
                k += c + 1; /* zeros */
            } else if (c < 0xc0) {
                unsigned v = src[i++];
                for (; n && k < nw; n--, k++)
                    cells[k * 2 + b] = v;
            } else {
                for (; n && k < nw && i < size; n--, k++)
                    cells[k * 2 + b] = src[i++];
            }
        }
    }
    return raw;
}

/* C arrays of the port's version that the user's ROM does not have: the
 * ROM's file of the same name (gFileXYZ <-> "XYZ"), as far as it fits. */
static void FillArraysFromRomFiles(void)
{
    size_t i;
    unsigned n = 0, unpacked = 0;
    for (i = 0; i < sAssetCount; i++) {
        struct Asset *a = &sAssets[i];
        struct RomFile *f;
        size_t len;
        if (a->found || a->childCount || a->capacity == 0 || strncmp(a->name, "gFile", 5) != 0)
            continue;
        f = FindRomFile(a->name + 5);
        if (f == NULL)
            continue;
        len = strlen(a->name);
        if (a->name[len - 1] == 'c' &&
            UnpackEuTiles((unsigned char *)a->dst, a->capacity, gHostRom + f->dataOff, f->size, Port_RomVersion(gHostRom, gHostRomSize) == 'P')) {
            unpacked++;
        } else {
            if (f->size > a->capacity)
                Platform_Log("assets: %s: the ROM's file is larger (%u) than the array (%u), cut\n", a->name, f->size, a->capacity);
            memcpy(a->dst, gHostRom + f->dataOff, f->size < a->capacity ? f->size : a->capacity);
        }
        a->found = 5;
        n++;
    }
    Platform_Log("assets: %u arrays filled with the files of the user's ROM (%u packed tilesets unpacked)\n", n, unpacked);
}

/* A file of the user's ROM in the port's layout but with empty pointer
 * fields (the European item table has no name pointers, its game looks
 * names up by id): the pointer fields of the port's copy are put in. */
unsigned Port_FillPointerFields(const unsigned char *portCopy, unsigned char *romCopy, unsigned size)
{
    unsigned i, n = 0;
    for (i = 0; i < gPortRelocCount; i++) {
        const unsigned char *where = gPortRelocs[i].where;
        uint32_t off, v;
        if (where < portCopy || where >= portCopy + size)
            continue;
        off = (uint32_t)(where - portCopy);
        if (off + 4 > size)
            continue;
        memcpy(&v, romCopy + off, 4);
        if (v != 0)
            continue;
        memcpy(romCopy + off, where, 4);
        n++;
    }
    return n;
}

const unsigned char *Port_RomFile(const char *name)
{
    struct RomFile *f = FindRomFile(name);
    return f ? gHostRom + f->dataOff : NULL;
}
