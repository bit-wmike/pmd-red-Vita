/*
 * Game data from the user's ROM.
 *
 * The port's binary contains the game's code but none of its graphics, maps,
 * text or sound samples. The build registers every data array with its size
 * and a hash of its contents (tools/port/gen_data.py for the assembler data
 * files, tools/port/pp_assets.py for INCBIN arrays in C). At start the ROM
 * is scanned for those contents, the arrays are filled and the pointers
 * inside the assembler data get their host addresses. The search result is
 * cached in romindex.bin (keyed by a fingerprint of the ROM and a hash of
 * the block table, so a new port version with unchanged data keeps it), so
 * later starts only copy: no hashing of blocks, no SHA-1 of the ROM.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "port/port.h"
#include "port/port_assets.h"

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
    int absent;       /* the index says the scans did not find it: not scanned again */
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

/* One 64-byte block; the four round groups as separate loops (the first
 * start hashes all 32 MiB, a branch per round tripled the time). */
#define SHA1_ROUND(f, k)                                     \
    do {                                                     \
        uint32_t t_ = Rol(a, 5) + (f) + e + (k) + w[i];      \
        e = d; d = c; c = Rol(b, 30); b = a; a = t_;         \
    } while (0)

static void Sha1Block(struct Sha1 *s, const unsigned char *p)
{
    uint32_t w[80], a, b, c, d, e;
    int i;
    for (i = 0; i < 16; i++)
        w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
    for (i = 16; i < 80; i++)
        w[i] = Rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    a = s->h[0]; b = s->h[1]; c = s->h[2]; d = s->h[3]; e = s->h[4];
    for (i = 0; i < 20; i++)
        SHA1_ROUND(d ^ (b & (c ^ d)), 0x5A827999u);
    for (; i < 40; i++)
        SHA1_ROUND(b ^ c ^ d, 0x6ED9EBA1u);
    for (; i < 60; i++)
        SHA1_ROUND((b & c) | (d & (b | c)), 0x8F1BBCDCu);
    for (; i < 80; i++)
        SHA1_ROUND(b ^ c ^ d, 0xCA62C1D6u);
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

/* A block that is holes only (a pointer table into baserom.gba data, which
 * the decomp could not resolve): nothing to hash, nothing to fill. */
static int HoleOnly(const struct Asset *a)
{
    uint32_t i, n = 0;
    for (i = 0; i < a->holeCount; i++)
        n += a->holes[i * 2 + 1];
    return a->holeCount > 0 && n >= a->size;
}

/* Does the block have a hole of baserom.gba data (more than 64 bytes)?
 * Smaller holes are pointers, which Relocate fills. */
static int LargeHole(const struct Asset *a)
{
    uint32_t i;
    for (i = 0; i < a->holeCount; i++)
        if (a->holes[i * 2 + 1] > 64)
            return 1;
    return 0;
}

/* Small holes (up to 64 bytes) are pointer tables the decomp left as raw
 * bytes: their ROM addresses become host pointers into the ROM copy.
 * (Pointer holes of 4 bytes are patched again by Relocate().) */
static void TranslateSmallHoles(struct Asset *a)
{
    uint32_t i, k;
    for (i = 0; i < a->holeCount; i++) {
        uint32_t off = a->holes[i * 2], len = a->holes[i * 2 + 1];
        if (len > 64 || off + len > a->size)
            continue;
        for (k = 0; k + 4 <= len; k += 4) {
            uint32_t v;
            memcpy(&v, a->dst + off + k, 4);
            if (v >= 0x08000000u && v - 0x08000000u < gHostRomSize) {
                const void *host = gHostRom + (v - 0x08000000u);
                memcpy(a->dst + off + k, &host, 4);
            }
        }
    }
}

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
    TranslateSmallHoles(a);
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
    TranslateSmallHoles(a);
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
    if (a->found || a->absent || a->keyOff1 < 0)
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
static void PlaceByNeighbours(void);

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
        if (!a->found && !a->absent && !a->childCount)
            n++;
    }
    return n;
}

/* ---- the cached index ---- */

/* The SHA-1 of the loaded ROM, computed once (32 MiB take seconds on the
 * Vita). A start from romindex.bin takes it from there (the cache's ROM
 * fingerprint matched) and never hashes the whole ROM. */
static unsigned char sRomSha[20];
static const unsigned char *sRomShaOf;
static size_t sRomShaSize;

void Port_RomSha1(unsigned char out[20])
{
    if (sRomShaOf != gHostRom || sRomShaSize != gHostRomSize) {
        Port_Sha1(gHostRom, gHostRomSize, sRomSha);
        sRomShaOf = gHostRom;
        sRomShaSize = gHostRomSize;
    }
    memcpy(out, sRomSha, 20);
}

static void HashBytes(uint64_t *h, const void *p, size_t n)
{
    const unsigned char *b = (const unsigned char *)p;
    while (n--) {
        *h ^= *b++;
        *h *= 0x100000001b3ull;
    }
}

/* A hash over the block table (names, sizes, content hashes, probe keys,
 * holes, chunk hashes of every block, children and INCBIN arrays included):
 * the cache is keyed by it, not by the port version, so a new version with
 * unchanged data reuses it, while any change to a block forces a rescan. */
static uint64_t TableHash(void)
{
    uint64_t h = 0xcbf29ce484222325ull;
    size_t i;
    uint32_t n = (uint32_t)sAssetCount;
    HashBytes(&h, &n, 4);
    for (i = 0; i < sAssetCount; i++) {
        const struct Asset *a = &sAssets[i];
        int32_t parent = a->parent;
        HashBytes(&h, a->name, strlen(a->name) + 1);
        HashBytes(&h, &a->size, 4);
        HashBytes(&h, &a->hash, 8);
        HashBytes(&h, &a->key1, 8);
        HashBytes(&h, &a->keyOff1, 4);
        HashBytes(&h, &a->key2, 8);
        HashBytes(&h, &a->keyOff2, 4);
        HashBytes(&h, &a->romAddr, 4);
        HashBytes(&h, &a->holeCount, 4);
        if (a->holes)
            HashBytes(&h, a->holes, (size_t)a->holeCount * 2 * 4);
        HashBytes(&h, &a->chunkCount, 4);
        if (a->chunks)
            HashBytes(&h, a->chunks, (size_t)a->chunkCount * 8);
        HashBytes(&h, &a->childCount, 4);
        HashBytes(&h, &parent, 4);
    }
    return h;
}

/* The header line of romindex.txt, the cache of port versions up to 0.1.19
 * (read once to build romindex.bin without a new scan; no longer written).
 * Computed before the scan (the scan changes the romAddr fields that the
 * table hash covers). */
static char sIndexHeader[160];

static const char *IndexHeader(void)
{
    if (!sIndexHeader[0]) {
        unsigned char sha[20];
        size_t size = sizeof(sIndexHeader);
        int i, n;
        Port_RomSha1(sha);
        n = snprintf(sIndexHeader, size, "pmdidx 2 %016llx ", (unsigned long long)TableHash());
        for (i = 0; i < 20 && n < (int)size; i++)
            n += snprintf(sIndexHeader + n, size - n, "%02x", sha[i]);
    }
    return sIndexHeader;
}

static void LoadIndex(void)
{
    static char text[4 << 20];
    const char *header;
    long n = Platform_ReadFile("romindex.txt", text, sizeof(text) - 1);
    char *line;
    size_t hits = 0, absent = 0, i;
    if (n <= 0)
        return;
    header = IndexHeader(); /* only now: it hashes the whole ROM */
    text[n] = 0;
    line = strtok(text, "\n");
    if (!line || strcmp(line, header) != 0) {
        Platform_Log("assets: romindex.txt is for another ROM or other data blocks\n");
        return;
    }
    /* "name romAddr" per line ("name -": the scans did not find the block,
     * so it is not scanned again), looked up through a name hash table */
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
            if (a->found || a->absent || strcmp(a->name, line) != 0)
                continue;
            if (sp[1] == '-') {
                a->absent = 1;
                absent++;
            } else if (addr >= 0x08000000ul &&
                       (Verify(a, (uint32_t)(addr - 0x08000000ul)) || TryPartial(a, (uint32_t)(addr - 0x08000000ul))))
                hits++;
            break;
        }
    }
    Platform_Log("assets: %u of %u blocks from romindex.txt, %u known to be absent\n", (unsigned)hits, (unsigned)sAssetCount,
                 (unsigned)absent);
}

/* ---- the start cache (romindex.bin) ----
 *
 * romindex.txt (above) only says where each block is; every start still
 * hashed every block and the whole ROM (SHA-1) to check it - ~4 s on the
 * Vita. romindex.bin is the result of the whole search for one ROM and one
 * build's block table: the state and ROM address of every block in table
 * order, the ROM's SHA-1 and the offsets of the ROM's file archives. It is
 * keyed by a fingerprint of the ROM (size, cartridge header, 512 bytes at
 * every 64 KiB) and a hash of the block table; when both match, the blocks
 * are copied from their places without being hashed again (the same ROM
 * gives the same matches). Little-endian, as the Vita and the test hosts:
 *   u32 magic "PMDC", u32 format, u64 table hash, u64 fingerprint,
 *   u8 sha1[20], u32 rom size, u32 block count, u32 0, u32 archive count,
 *   u32 checksum of the rest of the file (low half of HashWords)
 *   u32 romAddr[blocks], u8 state[blocks] (0 none, 1 exact, 2 partial,
 *   0xff not in this ROM), pad to 4, u32 archive offsets[archives] */
#define CACHE_FILE "romindex.bin"
#define CACHE_MAGIC 0x43444d50u /* "PMDC" */
#define CACHE_FORMAT 1
#define CACHE_HEADER 64
#define CACHE_ABSENT 0xFF
#define ARCHIVES_MAX 64

static uint32_t sArchiveOffs[ARCHIVES_MAX];
static unsigned sArchiveCount;
static int sArchivesKnown; /* sArchiveOffs came from the cache */
static unsigned char *sCacheImage; /* built during a search, written after IndexRomArchives */

/* FNV-1a over 32-bit words (4x fewer multiplications than per byte; the
 * tail bytes one by one) */
static uint64_t HashWords(uint64_t h, const void *data, size_t n)
{
    const unsigned char *p = (const unsigned char *)data;
    while (n >= 4) {
        uint32_t w;
        memcpy(&w, p, 4);
        h ^= w;
        h *= 0x100000001b3ull;
        p += 4;
        n -= 4;
    }
    while (n--) {
        h ^= *p++;
        h *= 0x100000001b3ull;
    }
    return h;
}

/* The block table as romindex.bin is keyed by (the same fields as
 * TableHash, which keys romindex.txt; must run before any search changes
 * the romAddr fields). */
static uint64_t FastTableHash(void)
{
    uint64_t h = 0xcbf29ce484222325ull;
    size_t i;
    uint32_t n = (uint32_t)sAssetCount;
    h = HashWords(h, &n, 4);
    for (i = 0; i < sAssetCount; i++) {
        const struct Asset *a = &sAssets[i];
        uint32_t f[12];
        f[0] = a->size;
        memcpy(&f[1], &a->hash, 8);
        memcpy(&f[3], &a->key1, 8);
        f[5] = (uint32_t)a->keyOff1;
        memcpy(&f[6], &a->key2, 8);
        f[8] = (uint32_t)a->keyOff2;
        f[9] = a->romAddr;
        f[10] = a->holeCount | a->childCount << 20;
        f[11] = a->chunkCount ^ (uint32_t)a->parent << 12;
        h = HashWords(h, a->name, strlen(a->name) + 1);
        h = HashWords(h, f, sizeof(f));
        h = HashWords(h, &a->childCount, 4);
        h = HashWords(h, &a->parent, 4);
        if (a->holes)
            h = HashWords(h, a->holes, (size_t)a->holeCount * 2 * 4);
        /* (the chunk hashes follow from the content and the holes, which
         * a->hash and the hole list cover: only their number is hashed) */
    }
    return h;
}

/* Which ROM is loaded, without reading all of it: the size, the cartridge
 * header and 512 bytes at every 64 KiB (256 KiB in all) plus the last 512.
 * A different dump, version or patch changes some of them; the full SHA-1
 * is stored in the cache when it is built. */
static uint64_t RomFingerprint(void)
{
    uint64_t h = 0xcbf29ce484222325ull;
    uint32_t size = (uint32_t)gHostRomSize, off;
    h = HashWords(h, &size, 4);
    h = HashWords(h, gHostRom, size < 0xC0 ? size : 0xC0);
    for (off = 0; off + 512 <= size; off += 0x10000)
        h = HashWords(h, gHostRom + off, 512);
    if (size >= 512)
        h = HashWords(h, gHostRom + size - 512, 512);
    return h;
}

static uint32_t Get32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void Put32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static void Put64(unsigned char *p, uint64_t v)
{
    Put32(p, (uint32_t)v);
    Put32(p + 4, (uint32_t)(v >> 32));
}

static size_t CacheSize(size_t blocks, unsigned archives)
{
    return CACHE_HEADER + blocks * 4 + ((blocks + 3) & ~(size_t)3) + (size_t)archives * 4;
}

/* Applies romindex.bin when it is for this ROM and this block table: the
 * blocks it lists as found are copied from their places, exactly as
 * LoadIndex/Verify/TryPartial would after hashing them (same order, same
 * bytes, same small-hole translation). 1 when applied, else 0 (nothing
 * changed). */
static int LoadCache(uint64_t tableHash, uint64_t fingerprint)
{
    size_t cap = CacheSize(sAssetCount, ARCHIVES_MAX), i, hits = 0, absent = 0, partial = 0;
    unsigned char *img = (unsigned char *)malloc(cap);
    const unsigned char *addrs, *states;
    unsigned archives;
    long n;
    if (!img)
        return 0;
    n = Platform_ReadFile(CACHE_FILE, img, cap);
    if (n <= 0) {
        free(img);
        return 0;
    }
    if (n < CACHE_HEADER || Get32(img) != CACHE_MAGIC || Get32(img + 4) != CACHE_FORMAT) {
        Platform_Log("assets: " CACHE_FILE " has another format\n");
        free(img);
        return 0;
    }
    archives = Get32(img + 56);
    if (Get32(img + 8) != (uint32_t)tableHash || Get32(img + 12) != (uint32_t)(tableHash >> 32) ||
        Get32(img + 16) != (uint32_t)fingerprint || Get32(img + 20) != (uint32_t)(fingerprint >> 32) ||
        Get32(img + 44) != (uint32_t)gHostRomSize || Get32(img + 48) != (uint32_t)sAssetCount) {
        Platform_Log("assets: " CACHE_FILE " is for another ROM or other data blocks\n");
        free(img);
        return 0;
    }
    if (archives > ARCHIVES_MAX || (size_t)n != CacheSize(sAssetCount, archives)) {
        Platform_Log("assets: " CACHE_FILE " has unexpected sizes (%ld bytes)\n", n);
        free(img);
        return 0;
    }
    if (Get32(img + 60) != (uint32_t)HashWords(0xcbf29ce484222325ull, img + CACHE_HEADER, (size_t)n - CACHE_HEADER)) {
        Platform_Log("assets: " CACHE_FILE " is damaged (checksum)\n");
        free(img);
        return 0;
    }
    addrs = img + CACHE_HEADER;
    states = addrs + sAssetCount * 4;
    /* check everything before touching a block */
    for (i = 0; i < sAssetCount; i++) {
        uint32_t addr = Get32(addrs + i * 4);
        unsigned st = states[i];
        if (st == 1 || st == 2) {
            if (addr < 0x08000000u || (uint64_t)(addr - 0x08000000u) + sAssets[i].size > gHostRomSize)
                break;
        } else if (st != 0 && st != CACHE_ABSENT)
            break;
    }
    if (i < sAssetCount) {
        Platform_Log("assets: " CACHE_FILE " is damaged (block %u)\n", (unsigned)i);
        free(img);
        return 0;
    }
    for (i = 0; i < sAssetCount; i++) {
        struct Asset *a = &sAssets[i];
        unsigned st = states[i];
        uint32_t off;
        if (st == 0 || a->found || a->absent)
            continue;
        if (st == CACHE_ABSENT) {
            a->absent = 1;
            absent++;
            continue;
        }
        off = Get32(addrs + i * 4) - 0x08000000u;
        memcpy(a->dst, gHostRom + off, a->size);
        TranslateSmallHoles(a);
        a->romAddr = off + 0x08000000u;
        a->found = (int)st;
        hits++;
        partial += st == 2;
    }
    for (i = 0; i < archives; i++)
        sArchiveOffs[i] = Get32(states + ((sAssetCount + 3) & ~(size_t)3) + i * 4);
    sArchiveCount = archives;
    sArchivesKnown = 1;
    memcpy(sRomSha, img + 24, 20);
    sRomShaOf = gHostRom;
    sRomShaSize = gHostRomSize;
    free(img);
    Platform_Log("assets: %u of %u blocks from " CACHE_FILE " (%u partly), %u known to be absent\n", (unsigned)hits,
                 (unsigned)sAssetCount, (unsigned)partial, (unsigned)absent);
    return 1;
}

/* The search result, taken after the scans (before
 * PlaceByNeighbours and the ROM files change states); the archive offsets
 * are added and the file written once IndexRomArchives has run. */
static void MakeCache(uint64_t tableHash, uint64_t fingerprint)
{
    size_t i;
    unsigned char *img = (unsigned char *)calloc(1, CacheSize(sAssetCount, ARCHIVES_MAX)), *states;
    if (!img)
        return;
    Put32(img, CACHE_MAGIC);
    Put32(img + 4, CACHE_FORMAT);
    Put64(img + 8, tableHash);
    Put64(img + 16, fingerprint);
    Port_RomSha1(img + 24);
    Put32(img + 44, (uint32_t)gHostRomSize);
    Put32(img + 48, (uint32_t)sAssetCount);
    states = img + CACHE_HEADER + sAssetCount * 4;
    for (i = 0; i < sAssetCount; i++) {
        const struct Asset *a = &sAssets[i];
        if ((a->found == 1 || a->found == 2) && a->romAddr >= 0x08000000u) {
            /* (constant blocks are found without a place: filled again at every start) */
            Put32(img + CACHE_HEADER + i * 4, a->romAddr);
            states[i] = (unsigned char)a->found;
        } else if (a->found == 0)
            states[i] = CACHE_ABSENT; /* as "name -" in romindex.txt */
    }
    free(sCacheImage);
    sCacheImage = img;
}

static void WriteCache(void)
{
    size_t i, blocks = sAssetCount, size;
    unsigned char *arch;
    if (!sCacheImage)
        return;
    size = CacheSize(blocks, sArchiveCount);
    Put32(sCacheImage + 56, sArchiveCount);
    arch = sCacheImage + CACHE_HEADER + blocks * 4 + ((blocks + 3) & ~(size_t)3);
    for (i = 0; i < sArchiveCount; i++)
        Put32(arch + i * 4, sArchiveOffs[i]);
    Put32(sCacheImage + 60, (uint32_t)HashWords(0xcbf29ce484222325ull, sCacheImage + CACHE_HEADER, size - CACHE_HEADER));
    if (Platform_WriteFile(CACHE_FILE, sCacheImage, size) != 0)
        Platform_Log("assets: cannot write " CACHE_FILE "\n");
    else
        Platform_Log("assets: " CACHE_FILE " written (%u bytes)\n", (unsigned)size);
    free(sCacheImage);
    sCacheImage = NULL;
}

/* Start-up timing: one log line with the phases of Port_LoadRom. */
static char sPhaseLog[400];
static size_t sPhaseLen;
static uint64_t sPhaseT;

static void Phase(const char *what)
{
    uint64_t t = Platform_TimeUs();
    if (sPhaseLen < sizeof(sPhaseLog))
        sPhaseLen += (size_t)snprintf(sPhaseLog + sPhaseLen, sizeof(sPhaseLog) - sPhaseLen, " %s %u", what,
                                      (unsigned)((t - sPhaseT) / 1000));
    sPhaseT = t;
}

int gPortAssetsCacheBuilt;

/* ---- loading ---- */

const void *Port_RomPtr(const void *p)
{
    uintptr_t v = (uintptr_t)p;
    if (v >= 0x08000000u && v < 0x08000000u + gHostRomSize && gHostRom)
        return gHostRom + (v - 0x08000000u);
    return p;
}

/* Compressed file contents (AT4PX, AT4PN, ATUPX, PKDPX) hold no pointers;
 * the decomp still wrote some words of them as `.4byte gUnknown_...`
 * because they look like ROM addresses (336 words in 40 files of
 * system_sbin.s, the level curves lvmp*** among them). Such a relocation
 * must not be applied: it destroys the compressed stream and the
 * decompressor runs wild (the freeze when the first dungeon starts). */
int Port_IsCompressedMagic(const unsigned char *p)
{
    return memcmp(p, "AT4P", 4) == 0 || memcmp(p, "ATUP", 4) == 0 || memcmp(p, "PKDP", 4) == 0;
}

/* Is `where` inside the compressed content of a SIRO file of the port's
 * data (the file's header within the first 16 bytes of its block)? */
static int InCompressedContent(const unsigned char *where)
{
    size_t lo = 0, hi = sByAddressCount;
    const unsigned char *dst;
    unsigned off;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (sByAddress[mid]->dst + sByAddress[mid]->size <= where)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo >= sByAddressCount || sByAddress[lo]->dst > where)
        return 0;
    dst = sByAddress[lo]->dst;
    for (off = 0; off + 20 <= sByAddress[lo]->size && off <= 12; off += 4)
        if (memcmp(dst + off, "SIRO", 4) == 0)
            return Port_IsCompressedMagic(dst + off + 16) && where >= dst + off + 16;
    return 0;
}

static void Relocate(void)
{
    unsigned i, skipped = 0;
    for (i = 0; i < gPortRelocCount; i++) {
        uint32_t v = (uint32_t)(uintptr_t)(gPortRelocs[i].target + gPortRelocs[i].addend);
        if (InCompressedContent(gPortRelocs[i].where)) {
            skipped++;
            continue;
        }
        memcpy(gPortRelocs[i].where, &v, 4);
    }
    if (skipped || getenv("PMD_RELOC_DEBUG"))
        Platform_Log("assets: %u relocations inside compressed files left alone (%u relocations, %u blocks by address)\n", skipped, gPortRelocCount, (unsigned)sByAddressCount);
#ifndef __vita__
    if (getenv("PMD_RELOC_DEBUG")) {
        size_t k;
        unsigned siro = 0, comp = 0;
        for (k = 0; k < sByAddressCount; k++) {
            const unsigned char *dst = sByAddress[k]->dst;
            unsigned off;
            for (off = 0; off + 20 <= sByAddress[k]->size && off <= 12; off += 4)
                if (memcmp(dst + off, "SIRO", 4) == 0) {
                    siro++;
                    if (Port_IsCompressedMagic(dst + off + 16))
                        comp++;
                    else if (siro < 6)
                        Platform_Log("assets: %s: SIRO at %u, content %02x%02x%02x%02x\n", sByAddress[k]->name, off, dst[off + 16], dst[off + 17], dst[off + 18], dst[off + 19]);
                    break;
                }
        }
        Platform_Log("assets: %u SIRO blocks, %u compressed\n", siro, comp);
    }
#endif
}

int Port_LoadRom(unsigned char *rom, size_t size)
{
    size_t i, missing = 0, known = 0;
    uint64_t t0 = Platform_TimeUs(), tableHash, fingerprint;
    int fromCache;

    sPhaseT = t0;
    sPhaseLen = 0;
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
    Phase("register");
    tableHash = FastTableHash(); /* before any search changes romAddr */
    fingerprint = RomFingerprint();
    Phase("keys");

    /* 1. constant blocks need no ROM; blocks whose address the decomp states */
    for (i = 0; i < sAssetCount; i++) {
        struct Asset *a = &sAssets[i];
        if (a->keyOff1 == -2) {
            if (HoleOnly(a))
                continue; /* placed next to its neighbours below */
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
    Phase("constants");
    /* 2. the start cache (or the older romindex.txt), 3. scanning the ROM */
    fromCache = LoadCache(tableHash, fingerprint);
    if (!fromCache)
        LoadIndex();
    Phase("index");
    PropagateParents();
    missing = CountUnfound();
    if (missing) {
        fromCache = 0;
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
        Phase("scan");
    }
    gPortAssetsCacheBuilt = !fromCache;
    if (!fromCache) {
        MakeCache(tableHash, fingerprint); /* computes the ROM's SHA-1 if no index did */
        Phase("sha1");
    }
    PlaceByNeighbours();
    Phase("neighbours");
    IndexRomArchives();
    if (!fromCache)
        WriteCache();
    Phase("archives");
    FillArraysFromRomFiles();
    Phase("files");
    missing = 0;
    gPortAssetsPartial = 0;
    for (i = 0; i < sAssetCount; i++) {
        struct Asset *a = &sAssets[i];
        if (a->childCount)
            continue; /* judged by its parts */
        if (a->found == 5)
            gPortAssetsPartial++;
        /* a block of baserom.gba bytes only has a fallback of zeros: it
         * stays "not found", so a file of that name comes from the ROM's
         * own archive (the title menu palettes titlen0p..) */
        if (!a->found && a->fallback && !(HoleOnly(a) && LargeHole(a))) {
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
    BuildAddressIndex(); /* before Relocate: it looks blocks up by address */
    Relocate();
    Phase("relocate");
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
    Platform_Log("[start] assets (ms):%s%s\n", sPhaseLog, fromCache ? " (from " CACHE_FILE ")" : "");
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

/* Does the block that contains p hold baserom.gba data (a hole over 64
 * bytes)? Such a block is unreliable whatever its state (embedded zeros,
 * placed by neighbours, or matched by its few known bytes at a wrong
 * place): a file made of it is better taken from the ROM's own archive. */
int Port_AssetIncompleteAt(const void *p)
{
    size_t lo = 0, hi = sByAddressCount;
    const unsigned char *q = (const unsigned char *)p;
    const struct Asset *a;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (sByAddress[mid]->dst + sByAddress[mid]->size <= q)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo >= sByAddressCount || sByAddress[lo]->dst > q)
        return 0;
    a = sByAddress[lo];
    return LargeHole(a);
}

/* Is any block in [p, p + size) incomplete (see Port_AssetIncompleteAt)?
 * A file of the port's data may consist of many blocks (wmapspr). */
int Port_AssetIncompleteIn(const void *p, unsigned size)
{
    size_t lo = 0, hi = sByAddressCount;
    const unsigned char *q = (const unsigned char *)p, *end = q + size;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (sByAddress[mid]->dst + sByAddress[mid]->size <= q)
            lo = mid + 1;
        else
            hi = mid;
    }
    for (; lo < sByAddressCount && sByAddress[lo]->dst < end; lo++) {
        /* any state: a block that is mostly baserom.gba bytes can be
         * "found" by its few known bytes at a wrong place (wmp2font matched
         * code at 0x0b70e8 of the European ROM) */
        if (LargeHole(sByAddress[lo]))
            return 1;
    }
    return 0;
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

/* Pointer slots (holes of at most 64 bytes: pointers, small pointer
 * tables) of the block that contains p, counted from p on. Large holes are
 * baserom.gba data (compressed files, samples), not pointers. */
unsigned Port_AssetPointersFrom(const void *p)
{
    size_t lo = 0, hi = sByAddressCount;
    const unsigned char *q = (const unsigned char *)p;
    unsigned i, n = 0;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (sByAddress[mid]->dst + sByAddress[mid]->size <= q)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo >= sByAddressCount || sByAddress[lo]->dst > q)
        return 0;
    for (i = 0; i < sByAddress[lo]->holeCount; i++) {
        uint32_t off = sByAddress[lo]->holes[i * 2], len = sByAddress[lo]->holes[i * 2 + 1];
        if (sByAddress[lo]->dst + off >= q && len <= 64)
            n++;
    }
    return n;
}

struct RomFile {
    uint32_t nameOff;
    uint32_t dataOff;
    uint32_t size;
    uint32_t relocated;
    unsigned char *copy; /* relocated copy on the heap, or NULL */
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

/* The files of the "pksdir0" archive at ROM offset p, listed; 0 when p
 * holds no archive header. */
static int ReadArchive(size_t p, size_t *cap)
{
    int32_t count;
    uint32_t entries, i;
    if (p + 16 > gHostRomSize || memcmp(gHostRom + p, "pksdir0", 8) != 0)
        return 0;
    memcpy(&count, gHostRom + p + 8, 4);
    memcpy(&entries, gHostRom + p + 12, 4);
    if (count <= 0 || count > 5000 || entries < 0x08000000u || entries - 0x08000000u + (uint32_t)count * 8 > gHostRomSize)
        return 0;
    entries -= 0x08000000u;
    for (i = 0; i < (uint32_t)count; i++) {
        uint32_t nameAddr, dataAddr;
        memcpy(&nameAddr, gHostRom + entries + i * 8, 4);
        memcpy(&dataAddr, gHostRom + entries + i * 8 + 4, 4);
        if (nameAddr < 0x08000000u || dataAddr < 0x08000000u || nameAddr - 0x08000000u >= gHostRomSize ||
            dataAddr - 0x08000000u >= gHostRomSize)
            break;
        if (sRomFileCount == *cap) {
            *cap = *cap ? *cap * 2 : 1024;
            sRomFiles = (struct RomFile *)realloc(sRomFiles, *cap * sizeof(*sRomFiles));
        }
        sRomFiles[sRomFileCount].nameOff = nameAddr - 0x08000000u;
        sRomFiles[sRomFileCount].dataOff = dataAddr - 0x08000000u;
        sRomFileCount++;
    }
    return 1;
}

/* Finds the ROM's "pksdir0" archives and lists their files by name (the
 * archive offsets come from romindex.bin when it was loaded: the scan of
 * the whole ROM is skipped). */
static void IndexRomArchives(void)
{
    size_t p, cap = 0, archives = 0;
    if (sArchivesKnown) {
        unsigned k;
        for (k = 0; k < sArchiveCount; k++)
            archives += (size_t)ReadArchive(sArchiveOffs[k], &cap);
    } else {
        static const uint32_t magic = 0x64736b70u; /* "pksd" little-endian */
        sArchiveCount = 0;
        for (p = 0; p + 16 <= gHostRomSize; p += 4) {
            uint32_t w;
            memcpy(&w, gHostRom + p, 4);
            if (w != magic || !ReadArchive(p, &cap))
                continue;
            if (sArchiveCount < ARCHIVES_MAX)
                sArchiveOffs[sArchiveCount++] = (uint32_t)p;
            archives++;
        }
        if (archives > ARCHIVES_MAX) {
            free(sCacheImage); /* cannot be cached */
            sCacheImage = NULL;
        }
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
            sRomFiles[i].copy = NULL;
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

/* Files whose pointers the port rewrites are copied to the heap first: the
 * ROM image must keep its GBA pointers, the text lookups (port_text.c) read
 * the pointer tables of the archives (a relocated talk file made the
 * leader's level-up line untranslatable). */
static struct RomFile *sCopied[512];
static unsigned sCopiedCount, sCopiedBytes;

unsigned char *Port_RelocateRomFile(const char *name, unsigned limit)
{
    struct RomFile *f = FindRomFile(name);
    uint32_t off, i, n = 0;
    unsigned char *copy;
    if (f == NULL)
        return NULL;
    if (f->relocated)
        return f->copy;
    copy = (unsigned char *)malloc(f->size ? f->size : 1);
    if (copy == NULL || sCopiedCount >= sizeof(sCopied) / sizeof(sCopied[0])) {
        free(copy);
        Platform_Log("file: no memory for a copy of %s\n", name);
        return NULL;
    }
    off = f->dataOff;
    memcpy(copy, gHostRom + off, f->size);
    if (limit > f->size)
        limit = f->size;
    /* only pointers into the file itself: the archives' files point at
     * their own parts (tables, frames); a word of image data that happens
     * to look like a ROM address points elsewhere (portraits kao***) */
    for (i = 0; i + 4 <= limit; i += 4) {
        uint32_t v;
        memcpy(&v, copy + i, 4);
        if (v >= 0x08000000u + off && v < 0x08000000u + off + f->size) {
            const void *host = copy + (v - 0x08000000u - off);
            memcpy(copy + i, &host, 4);
            n++;
        }
    }
    f->copy = copy;
    f->relocated = 1;
    sCopied[sCopiedCount++] = f;
    sCopiedBytes += f->size;
    Platform_Log("file: %s: %u pointers translated in a copy (%u bytes, %u in all)\n", name, n, f->size,
                 sCopiedBytes);
    return copy;
}

/* The relocated copy of a file, NULL before Port_RelocateRomFile made it. */
const unsigned char *Port_RomFileCopy(const char *name)
{
    struct RomFile *f = FindRomFile(name);
    return f ? f->copy : NULL;
}

/* A pointer into one of those copies as the same place in the ROM image
 * (strings of copied files: the text lookups work on ROM offsets), else
 * NULL. */
const unsigned char *Port_RomCopyToRom(const unsigned char *p)
{
    unsigned i;
    for (i = 0; i < sCopiedCount; i++) {
        const struct RomFile *f = sCopied[i];
        if (p >= f->copy && p < f->copy + f->size)
            return gHostRom + f->dataOff + (p - f->copy);
    }
    return NULL;
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

/* Blocks that are holes only cannot be found by content: they sit at the
 * same place relative to a found neighbour of the same section (the one
 * right before or right after them). The neighbours' order in the array
 * is their order in the section. */
static void PlaceByNeighbours(void)
{
    size_t i, j;
    unsigned n = 0;
    for (i = 0; i < sAssetCount; i++) {
        struct Asset *a = &sAssets[i];
        uint32_t romOff = 0;
        int ok = 0;
        if (a->found || a->childCount || !HoleOnly(a) || a->parent < 0)
            continue;
        if (sAssets[a->parent].found == 1) {
            romOff = sAssets[a->parent].romAddr - 0x08000000u + (uint32_t)(a->dst - sAssets[a->parent].dst);
            ok = 1;
        }
        /* the parts of a block follow it in the table: its neighbours are
         * there (the same ones, in the same order, as in the whole table) */
        for (j = (size_t)a->parent + 1; j <= (size_t)a->parent + sAssets[a->parent].childCount && j < sAssetCount && !ok; j++) {
            const struct Asset *b = &sAssets[j];
            if (b->parent != a->parent || b->found != 1 || b->romAddr < 0x08000000u)
                continue;
            if (b->dst + b->size == a->dst) {
                romOff = b->romAddr - 0x08000000u + b->size;
                ok = 1;
            } else if (a->dst + a->size == b->dst && b->romAddr - 0x08000000u >= a->size) {
                romOff = b->romAddr - 0x08000000u - a->size;
                ok = 1;
            }
        }
        if (!ok || (uint64_t)romOff + a->size > gHostRomSize)
            continue;
        memcpy(a->dst, gHostRom + romOff, a->size);
        TranslateSmallHoles(a);
        a->romAddr = romOff + 0x08000000u;
        a->found = 6;
        n++;
    }
    Platform_Log("assets: %u hole-only blocks placed next to their neighbours\n", n);
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

/* A tileset file (*c) of the user's ROM unpacked into its raw form, kept
 * for the rest of the run (the language variants of the title and warning
 * screens: S02_GERc ...). NULL when the ROM has no such file. */
const unsigned char *Port_RomTilesetFile(const char *name)
{
    static struct { char name[16]; unsigned char *data; } cache[8];
    static unsigned used;
    struct RomFile *f = FindRomFile(name);
    const unsigned char *src;
    unsigned w, h, numTiles, numChunks, raw, got, i;
    unsigned char *dst;
    if (f == NULL)
        return NULL;
    for (i = 0; i < used; i++)
        if (strcmp(cache[i].name, name) == 0)
            return cache[i].data;
    src = gHostRom + f->dataOff;
    if (f->size < 16)
        return src;
    w = src[0] | (src[1] << 8);
    h = src[2] | (src[3] << 8);
    numTiles = src[4] | (src[5] << 8);
    numChunks = src[14] | (src[15] << 8);
    if (w != h || (w != 2 && w != 3) || numTiles == 0 || numChunks == 0)
        return src;
    raw = 16 + (numTiles - 1) * 32 + (numChunks - 1) * w * h * 2;
    dst = (unsigned char *)malloc(raw);
    if (dst == NULL)
        return src;
    got = UnpackEuTiles(dst, raw, src, f->size, Port_RomVersion(gHostRom, gHostRomSize) == 'P');
    if (got == 0) {
        free(dst);
        return src; /* not packed */
    }
    if (used < 8) {
        strncpy(cache[used].name, name, sizeof(cache[used].name) - 1);
        cache[used].data = dst;
        used++;
    }
    Platform_Log("file: %s unpacked from the user's ROM (%u -> %u bytes)\n", name, f->size, raw);
    return dst;
}

const unsigned char *Port_RomFile(const char *name)
{
    struct RomFile *f = FindRomFile(name);
    return f ? gHostRom + f->dataOff : NULL;
}
