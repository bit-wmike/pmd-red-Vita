/* Game text in the other languages of the European ROM. See port_text.h.
 *
 * Two storage schemes in the European ROM:
 *  - groups: the English string is followed by the German, French, Italian
 *    and Spanish one (scripts, menus, most sentences in the binary). Only
 *    the English string is referenced by a pointer.
 *  - parallel tables: a table of string pointers per language, either the
 *    five tables in one piece (monster names) or table, strings, next
 *    table, strings ... (items, moves, descriptions, battle messages).
 * The English string is located by content (an index of every string of the
 * ROM), then the scheme is decided from the bytes around it and from the
 * pointer sites that reference it. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "port.h"
#include "port_config.h"
#include "port_text.h"
#include "port_game.h"
#ifdef TEXT_TIMING
#include <time.h>
#define TLOG(what) Platform_Log("text: %s at %.2f s\n", what, (double)clock() / CLOCKS_PER_SEC)
#else
#define TLOG(what) ((void)0)
#endif

#define INDEX_BITS 20    /* strings of the ROM (~140k unique text strings) */
#define SITE_BITS 18     /* pointer sites (~100k targets) */
#define CACHE_BITS 16
#define RESULT_BITS 16
#define MAX_STR 1024
#define RUN_MIN 8        /* smallest pointer table taken as a table */
#define RUN_GAP 0x40000  /* largest distance between parallel tables */

struct Entry {
    uint32_t key;
    uint32_t val; /* offset + 1, 0 = empty */
};

static struct Entry *sIndex, *sSites, *sCache, *sResults;
static int sInitDone, sEnabled, sCacheLang, sDebug;
static unsigned sGroups, sTables, sPools_, sMisses, sCalls;

static uint32_t Fnv32(const unsigned char *p, size_t n)
{
    uint32_t h = 0x811c9dc5u;
    while (n--) {
        h ^= *p++;
        h *= 0x01000193u;
    }
    return h;
}

/* Game text carries control bytes (colours, pauses), so a string is any
 * run of non-NUL bytes; the end (NUL) of the string at off, -1 if too long. */
static long StrEnd(uint32_t off)
{
    uint32_t i, lim = off + MAX_STR;
    if (lim > gHostRomSize)
        lim = (uint32_t)gHostRomSize;
    const unsigned char *e;
    if (off >= lim)
        return -1;
    e = (const unsigned char *)memchr(gHostRom + off, 0, lim - off);
    return e ? (long)(e - gHostRom) : -1;
}

/* A string start: after a NUL, or right after a ROM pointer's high byte. */
static int StringStart(uint32_t off)
{
    unsigned b;
    if (off == 0 || off >= gHostRomSize)
        return 0;
    b = gHostRom[off - 1];
    return b == 0 || b == 0x08 || b == 0x09;
}

/* Mostly letters: 70 % of the bytes are ASCII or Latin-1 letters. */
static int Texty(uint32_t off)
{
    uint32_t i, good = 0;
    for (i = off; gHostRom[i]; i++) {
        unsigned c = gHostRom[i];
        if ((c >= 0x20 && c < 0x7f) || c >= 0xc0)
            good++;
    }
    return i > off && good * 10 >= (i - off) * 7;
}

static unsigned sFill[4];

/* Open addressing; a table is left alone when 7/8 full (never loops). */
static void Put(struct Entry *t, int bits, uint32_t key, uint32_t val, int keepFirst)
{
    uint32_t mask = (1u << bits) - 1, i = (key * 2654435761u) & mask;
    unsigned *fill = t == sIndex ? &sFill[0] : t == sSites ? &sFill[1] : t == sCache ? &sFill[2] : &sFill[3];
    if (*fill >= (mask + 1) / 8 * 7)
        return;
    for (;;) {
        if (t[i].val == 0) {
            t[i].key = key;
            t[i].val = val + 1;
            (*fill)++;
            return;
        }
        if (t[i].key == key) {
            if (!keepFirst)
                t[i].val = val + 1;
            return;
        }
        i = (i + 1) & mask;
    }
}

static long Get(const struct Entry *t, int bits, uint32_t key)
{
    uint32_t mask = (1u << bits) - 1, i = (key * 2654435761u) & mask;
    for (;;) {
        if (t[i].val == 0)
            return -1;
        if (t[i].key == key)
            return (long)t[i].val - 1;
        i = (i + 1) & mask;
    }
}

/* 1 when a 4-aligned word points at a string start of the ROM (any string,
 * also an empty or a placeholder one: a table keeps such entries). */
static int StringPtr(uint32_t site, uint32_t *target)
{
    uint32_t v;
    if (site + 4 > gHostRomSize)
        return 0;
    memcpy(&v, gHostRom + site, 4);
    if (v < 0x08000000u || v - 0x08000000u >= gHostRomSize)
        return 0;
    v -= 0x08000000u;
    if (!StringStart(v) || StrEnd(v) < 0)
        return 0;
    *target = v;
    return 1;
}

/* ... and the string is text. */
static int TextPtr(uint32_t site, uint32_t *target)
{
    return StringPtr(site, target) && gHostRom[*target] && Texty(*target);
}

static void BuildPools(void);
static void BuildRegions(void);

static void BuildIndex(void)
{
    uint32_t i, n = (uint32_t)gHostRomSize, strings = 0, sites = 0;
    sIndex = (struct Entry *)calloc(1u << INDEX_BITS, sizeof(struct Entry));
    sSites = (struct Entry *)calloc(1u << SITE_BITS, sizeof(struct Entry));
    sCache = (struct Entry *)calloc(1u << CACHE_BITS, sizeof(struct Entry));
    sResults = (struct Entry *)calloc(1u << RESULT_BITS, sizeof(struct Entry));
    if (!sIndex || !sSites || !sCache || !sResults) {
        Platform_Log("text: no memory for the string index\n");
        sEnabled = 0;
        return;
    }
    for (i = 1; i < n; i++) {
        long e;
        if (gHostRom[i] == 0 || !StringStart(i))
            continue;
        e = StrEnd(i);
        if (e < 0)
            continue;
        if (e - i >= 2 && Texty(i)) {
            Put(sIndex, INDEX_BITS, Fnv32(gHostRom + i, (size_t)(e - i)), i, 1);
            strings++;
        }
    }
    TLOG("strings indexed");
    for (i = 0; i + 4 <= n; i += 4) {
        uint32_t t;
        if (TextPtr(i, &t)) {
            Put(sSites, SITE_BITS, t, i, 1);
            sites++;
        }
    }
    Platform_Log("text: %u strings and %u string pointers indexed\n", strings, sites);
    TLOG("pointers indexed");
    BuildPools();
    BuildRegions();
    TLOG("pools built");
}

static int HasSite(uint32_t off)
{
    return Get(sSites, SITE_BITS, off) >= 0;
}

/* Words shared by two strings, as a share of the first one's words. */
static int WordOverlap(uint32_t a, uint32_t b)
{
    const unsigned char *s = gHostRom + a, *t = gHostRom + b;
    int words = 0, shared = 0;
    while (*s) {
        const unsigned char *w = s;
        size_t len;
        while (*s && *s != ' ' && *s != '\n' && *s != '\r')
            s++;
        len = (size_t)(s - w);
        while (*s == ' ' || *s == '\n' || *s == '\r')
            s++;
        if (len < 3)
            continue;
        words++;
        {
            const unsigned char *u = t;
            while (*u) {
                if (memcmp(u, w, len) == 0 && (u[len] == 0 || u[len] == ' ' || u[len] == '\n')) {
                    shared++;
                    break;
                }
                while (*u && *u != ' ' && *u != '\n')
                    u++;
                while (*u == ' ' || *u == '\n')
                    u++;
            }
        }
    }
    if (words < 2)
        return 0;
    return shared * 100 / words;
}

/* Scheme 1: the translations follow the English string. */
static long GroupLookup(uint32_t r, int lang)
{
    uint32_t s[PORT_LANG_COUNT];
    long e;
    int k;
    /* the string before must not be a referenced one (then r is not the
     * first of its group but a translation itself) */
    if (r >= 2 && gHostRom[r - 1] == 0) {
        uint32_t j = r - 1, lim = r > MAX_STR ? r - MAX_STR : 0;
        while (j > lim && gHostRom[j - 1] != 0)
            j--;
        if (j < r - 1 && HasSite(j))
            return -1;
    }
    s[0] = r;
    e = StrEnd(r);
    if (e < 0 || !Texty(r))
        return -1;
    for (k = 1; k < PORT_LANG_COUNT; k++) {
        s[k] = (uint32_t)e + 1;
        if (s[k] >= gHostRomSize || gHostRom[s[k]] == 0 || HasSite(s[k]) || !Texty(s[k]))
            return -1;
        e = StrEnd(s[k]);
        if (e < 0)
            return -1;
    }
    if (WordOverlap(s[0], s[1]) >= 50)
        return -1; /* the next entry of a list, not a translation */
    return s[lang];
}

/* The pointer run (4-byte stride) around a site: start and count. */
static uint32_t RunAround(uint32_t site, uint32_t *count)
{
    uint32_t t, s = site, e = site;
    while (s >= 4 && StringPtr(s - 4, &t))
        s -= 4;
    while (StringPtr(e + 4, &t))
        e += 4;
    *count = (e - s) / 4 + 1;
    return s;
}

static uint32_t Target(uint32_t site)
{
    uint32_t v;
    memcpy(&v, gHostRom + site, 4);
    return v - 0x08000000u;
}

static int SameString(uint32_t a, uint32_t b)
{
    return strcmp((const char *)gHostRom + a, (const char *)gHostRom + b) == 0;
}

/* Is there a table parallel to the run (start, count) at distance d: a
 * table of exactly count string pointers (bounded by non-pointers), mostly
 * text? */
static int ParallelAt(uint32_t start, uint32_t count, uint32_t idx, long d)
{
    uint32_t j, text = 0, t;
    long first = (long)start + d, last = first + 4 * (long)(count - 1);
    (void)idx;
    if (first < 4 || (uint64_t)last + 8 > gHostRomSize)
        return 0;
    if (StringPtr((uint32_t)first - 4, &t) || StringPtr((uint32_t)last + 4, &t))
        return 0;
    for (j = 0; j < count; j++) {
        if (!StringPtr((uint32_t)first + 4 * j, &t))
            return 0;
        if (gHostRom[t] && Texty(t))
            text++;
    }
    return text * 2 >= count;
}

static const char *const sLangWords[PORT_LANG_COUNT][6] = {
    {" the ", " you ", " and ", " is ", " to ", " with "},
    {" der ", " die ", " das ", " und ", " nicht ", " ist "},
    {" les ", " est ", " vous ", " pas ", " une ", " des "},
    {" il ", " che ", " non ", " di ", " una ", " per "},
    {" el ", " que ", " los ", " las ", " es ", " con "},
};

/* Letters that mark a language (Latin-1); e is left out: Pokémon. */
static const char *const sLangLetters[PORT_LANG_COUNT] = {
    "", "\xe4\xf6\xfc\xdf\xc4\xd6\xdc", "\xe8\xea\xe7\xe2\xee\xf4\xfb\xc9", "\xec\xf2\xe0\xf9", "\xf1\xbf\xa1\xe1\xed\xf3\xfa\xd1"
};

/* The language of a table's strings (every step-th entry), -1 if unclear:
 * common words, else the letters of the language (names have neither). */
static int TableLanguage(uint32_t start, uint32_t n, uint32_t step)
{
    unsigned score[PORT_LANG_COUNT] = {0}, letters[PORT_LANG_COUNT] = {0}, foreign = 0;
    uint32_t j;
    int k, w, best = -1;
    for (j = 0; j < n; j += step) {
        uint32_t p = Target(start + 4 * j);
        for (; gHostRom[p]; p++) {
            unsigned c = gHostRom[p];
            if (c >= 0xc0 && c != 0xe9) {
                foreign++;
                for (k = 1; k < PORT_LANG_COUNT; k++)
                    if (strchr(sLangLetters[k], (int)c))
                        letters[k]++;
            }
            if (c != ' ')
                continue;
            for (k = 0; k < PORT_LANG_COUNT; k++)
                for (w = 0; w < 6; w++) {
                    const char *word = sLangWords[k][w];
                    size_t len = strlen(word);
                    if (p + len < gHostRomSize && memcmp(gHostRom + p, word, len) == 0)
                        score[k]++;
                }
        }
    }
    for (k = 0; k < PORT_LANG_COUNT; k++)
        if (score[k] >= 3 && (best < 0 || score[k] > score[best]))
            best = k;
    if (best >= 0)
        return best;
    for (k = 1; k < PORT_LANG_COUNT; k++)
        if (letters[k] >= 2 && (best < 0 || letters[k] > letters[best]))
            best = k;
    (void)foreign;
    return best;
}

/* Scheme 2: parallel pointer tables. */
static long TableLookup(uint32_t r, int lang)
{
    long site = Get(sSites, SITE_BITS, r), d;
    uint32_t start, count, idx, sub, j, same = 0, tested = 0, step, end;
    int k;
    if (site < 0)
        return -1;
    start = RunAround((uint32_t)site, &count);
    if (count < RUN_MIN)
        return -1;
    idx = ((uint32_t)site - start) / 4;
    end = start + count * 4;
    /* five tables in one piece: the Italian/Spanish names often equal the
     * English ones, unrelated tables share nothing */
    sub = (count + 2) / PORT_LANG_COUNT; /* a run may lose its last entries */
    if (sub >= RUN_MIN) {
        step = sub / 64 ? sub / 64 : 1;
        for (j = 1; j + 1 < sub && j + 3 * sub < count; j += step) {
            uint32_t a = Target(start + 4 * j), b = Target(start + 4 * (j + 3 * sub)), c = Target(start + 4 * (j + sub));
            /* a repeated default entry (battle messages) says nothing */
            if (gHostRom[a] == 0 || SameString(a, Target(start + 4 * (j - 1))) || SameString(a, Target(start + 4 * (j + 1))))
                continue;
            tested++;
            if (SameString(a, b) || SameString(a, c))
                same++;
        }
        /* sentences: the sub-tables read as the languages in order */
        if (!(tested >= 4 && same * 10 >= tested) && ((uint32_t)lang + 1) * sub <= count &&
            TableLanguage(start, sub, step) == PORT_LANG_EN && TableLanguage(start + 4 * (uint32_t)lang * sub, sub, step) == lang) {
            tested = 1;
            same = 1;
        }
        if (tested >= 4 && same * 10 >= tested) {
            if (idx >= sub || idx + (uint32_t)lang * sub >= count)
                return -1; /* not in the English table */
            return (long)Target(start + 4 * (idx + (uint32_t)lang * sub));
        }
    }
    /* table, strings, table, strings: the parallel tables after this one,
     * the wanted one by its language (English first in every layout seen) */
    {
        int own = TableLanguage(start, count, count / 512 + 1);
        long cand[PORT_LANG_COUNT];
        int ncand = 0, c;
        if (own != PORT_LANG_EN && own != -1)
            return -1; /* a translation itself */
        if (own == -1 && Get(sResults, RESULT_BITS, r) >= 0)
            return -1; /* a translation handed out before */
        d = (long)(end - start);
        while (ncand < PORT_LANG_COUNT - 1 && d < (long)(end - start) + (long)RUN_GAP) {
            if ((uint64_t)start + d + count * 4 > gHostRomSize)
                break;
            if (ParallelAt(start, count, idx, d)) {
                cand[ncand++] = d;
                d += (long)count * 4;
            } else
                d += 4;
        }
        {
            int langs[PORT_LANG_COUNT];
            for (c = 0; c < ncand; c++) {
                langs[c] = TableLanguage((uint32_t)((long)start + cand[c]), count, count / 512 + 1);
                if (langs[c] == lang)
                    return (long)Target((uint32_t)((long)start + 4 * idx + cand[c]));
            }
            for (c = 0; c < ncand; c++)
                if (langs[c] != -1 && langs[c] != c + 1)
                    return -1; /* not the order English, German, French, Italian, Spanish */
        }
        /* the language not readable (short names), the others in the usual
         * order: take the position */
        if (ncand >= lang)
            return (long)Target((uint32_t)((long)start + 4 * idx + cand[lang - 1]));
        return -1;
    }
}

/* Scheme 3: pools. The strings of the code (menus, quiz, system messages)
 * lie in pools of entries { u16 width; char text[]; '\0'; pad to even },
 * one pool per language with the entries in the same order. A pool is
 * recognised by its size and its language (common words). */
#define POOL_MIN 20
#define POOL_MAX 128

struct Pool {
    uint32_t start, end, count;
    int lang;
    int kind; /* 0: u16 header, pad to 2; 1: ROM pointer header, pad to 4 */
};
static struct Pool sPools[POOL_MAX];
static int sPoolCount;

/* The next entry after the one at p, or 0 when p is not an entry. */
static uint32_t PoolEntryNext(uint32_t p, int kind)
{
    uint32_t meta, q, align = kind ? 4 : 2;
    long e;
    if (p + 5 > gHostRomSize)
        return 0;
    if (kind) {
        /* code data: a few pointer words (a table of variants), then text */
        int words = 0;
        q = p;
        for (;;) {
            if (q + 4 > gHostRomSize)
                return 0;
            memcpy(&meta, gHostRom + q, 4);
            if (meta < 0x08000000u || meta - 0x08000000u >= gHostRomSize)
                break;
            q += 4;
            if (++words > 16)
                return 0;
        }
        if (words == 0 && p != 0 && gHostRom[p - 1] != 0)
            return 0;
    } else {
        meta = gHostRom[p] | (gHostRom[p + 1] << 8);
        if (meta > 2000)
            return 0;
        q = p + 2;
    }
    if (gHostRom[q] == 0)
        return 0;
    /* plain text only (a stricter rule than StrEnd: the entry boundaries
     * must come out the same in every language) */
    for (e = q; e < (long)gHostRomSize && gHostRom[e]; e++) {
        unsigned c = gHostRom[e];
        if (c < 0x20 && c != '\t' && c != '\n' && c != '\r')
            return 0;
        if (e - q >= MAX_STR)
            return 0;
    }
    if (e >= (long)gHostRomSize)
        return 0;
    q = (uint32_t)e + 1;
    while (q & (align - 1)) {
        if (q >= gHostRomSize || gHostRom[q] != 0)
            return 0;
        q++;
    }
    return q;
}


/* The language of the text between start and end by its common words. */
static int DetectLanguage(uint32_t start, uint32_t end)
{
    unsigned score[PORT_LANG_COUNT] = {0};
    uint32_t p;
    int k, w, best = -1;
    for (p = start; p + 6 < end; p++) {
        if (gHostRom[p] != ' ')
            continue;
        for (k = 0; k < PORT_LANG_COUNT; k++)
            for (w = 0; w < 6; w++) {
                const char *word = sLangWords[k][w];
                size_t len = strlen(word);
                if (p + len <= end && memcmp(gHostRom + p, word, len) == 0)
                    score[k]++;
            }
    }
    for (k = 0; k < PORT_LANG_COUNT; k++)
        if (score[k] >= 3 && (best < 0 || score[k] > score[best]))
            best = k;
    return best;
}

static void AddPool(uint32_t start, uint32_t end, uint32_t count, int lang, int kind)
{
    if (lang < 0 || sPoolCount >= POOL_MAX)
        return;
    sPools[sPoolCount].start = start;
    sPools[sPoolCount].end = end;
    sPools[sPoolCount].count = count;
    sPools[sPoolCount].lang = lang;
    sPools[sPoolCount].kind = kind;
    sPoolCount++;
}

/* The languages present among the pools of a size, as a bit mask. */
static unsigned PoolLangs(uint32_t count, int except)
{
    unsigned m = 0;
    int i;
    for (i = 0; i < sPoolCount; i++)
        if (i != except && sPools[i].count == count && sPools[i].kind == sPools[except].kind)
            m |= 1u << sPools[i].lang;
    return m;
}

/* Splits pool i into a first piece of a entries and the rest when the two
 * pieces complete the pools of those sizes with the languages they lack. */
static int TrySplit(int i, uint32_t a)
{
    uint32_t rest = sPools[i].count - a, q, m, end = sPools[i].end;
    unsigned have1 = PoolLangs(a, i), have2 = PoolLangs(rest, i);
    int l1, l2;
    if (have1 == 0 || have2 == 0 || have1 == 0x1f || have2 == 0x1f)
        return 0;
    for (q = sPools[i].start, m = 0; m < a; m++)
        q = PoolEntryNext(q, sPools[i].kind);
    l1 = DetectLanguage(sPools[i].start, q);
    l2 = DetectLanguage(q, end);
    if (l1 < 0 || l2 < 0 || (have1 & (1u << l1)) || (have2 & (1u << l2)))
        return 0;
    sPools[i].end = q;
    sPools[i].count = a;
    sPools[i].lang = l1;
    AddPool(q, end, rest, l2, sPools[i].kind);
    return 1;
}

static void BuildPools(void)
{
    uint32_t n = (uint32_t)gHostRomSize;
    int i, j, k, kind;
    for (kind = 0; kind < 2; kind++) {
        uint32_t p = 0, step = kind ? 4 : 2;
        while (p + 8 < n) {
            uint32_t q = PoolEntryNext(p, kind), start = p, count = 0;
            if (q == 0) {
                p += step;
                continue;
            }
            while (q) {
                count++;
                p = q;
                q = PoolEntryNext(p, kind);
            }
            if (count >= POOL_MIN)
                AddPool(start, p, count, DetectLanguage(start, p), kind);
            p += step;
        }
    }
    /* a pool that runs straight into the next language's first pool: split
     * it where the sizes of the other pools say */
    for (i = 0; i < sPoolCount; i++) {
        if (PoolLangs(sPools[i].count, i) != 0)
            continue; /* a size shared with other pools: complete as is */
        for (j = 0; j < sPoolCount; j++) {
            uint32_t a = sPools[j].count;
            if (j == i || a >= sPools[i].count)
                continue;
            for (k = 0; k < sPoolCount; k++)
                if (k != i && k != j && sPools[k].count == sPools[i].count - a)
                    break;
            if (k < sPoolCount && (TrySplit(i, a) || TrySplit(i, sPools[i].count - a)))
                break;
        }
    }
    for (i = 0; i < sPoolCount; i++)
        Platform_Log("text: pool %08x..%08x %u entries, language %d, kind %d\n", sPools[i].start, sPools[i].end, sPools[i].count, sPools[i].lang, sPools[i].kind);
}

/* The entry of r in an English pool and the same entry of the language. */
static long PoolLookup(uint32_t r, int lang)
{
    int i, j;
    for (i = 0; i < sPoolCount; i++) {
        uint32_t p, idx = 0, q, hdr;
        if (sPools[i].kind || sPools[i].lang != PORT_LANG_EN || r < sPools[i].start || r >= sPools[i].end)
            continue;
        hdr = sPools[i].kind ? 4 : 2;
        for (p = sPools[i].start; p && p + hdr < r; p = PoolEntryNext(p, sPools[i].kind))
            idx++;
        if (p + hdr != r)
            return -1; /* inside an entry */
        for (j = 0; j < sPoolCount; j++) {
            if (j == i || sPools[j].count != sPools[i].count || sPools[j].lang != lang || sPools[j].kind != sPools[i].kind)
                continue;
            for (q = sPools[j].start; q && idx > 0; idx--)
                q = PoolEntryNext(q, sPools[j].kind);
            return q ? (long)q + hdr : -1;
        }
        return -1;
    }
    return -1;
}

/* Scheme 4: regions of code data (dungeon messages): small tables of
 * variant pointers and their strings, one region per language with the
 * string pointers in the same order. Found from the pointer-headed pools,
 * merged across small gaps; the k-th string pointer of the English region
 * matches the k-th of the other language. */
#define REGION_MAX 32
#define REGION_GAP 0x1000

struct Region {
    uint32_t start, end, count;
    int lang;
};
static struct Region sRegions[REGION_MAX];
static int sRegionCount;

/* A 4-aligned word pointing at a (possibly empty) string start. */
static int SitePtr(uint32_t site, uint32_t *target)
{
    uint32_t t;
    if (!StringPtr(site, &t))
        return 0;
    if (gHostRom[t] && !Texty(t))
        return 0;
    *target = t;
    return 1;
}

static uint32_t CountSites(uint32_t start, uint32_t end)
{
    uint32_t p, t, n = 0;
    for (p = (start + 3) & ~3u; p + 4 <= end; p += 4)
        if (SitePtr(p, &t))
            n++;
    return n;
}

static void BuildRegions(void)
{
    int i;
    uint32_t start = 0, end = 0;
    for (i = 0; i <= sPoolCount; i++) {
        int more = i < sPoolCount && sPools[i].kind == 1;
        if (more && end && sPools[i].start < end + REGION_GAP && sPools[i].start >= end) {
            end = sPools[i].end;
            continue;
        }
        if (end && sRegionCount < REGION_MAX) {
            sRegions[sRegionCount].start = start;
            sRegions[sRegionCount].end = end;
            sRegions[sRegionCount].count = CountSites(start, end);
            sRegions[sRegionCount].lang = DetectLanguage(start, end);
            Platform_Log("text: region %08x..%08x %u pointers, language %d\n", start, end, sRegions[sRegionCount].count, sRegions[sRegionCount].lang);
            sRegionCount++;
        }
        start = more ? sPools[i].start : 0;
        end = more ? sPools[i].end : 0;
    }
}

static long RegionLookup(uint32_t r, int lang)
{
    int i, j;
    for (i = 0; i < sRegionCount; i++) {
        uint32_t p, t, k = 0, found = 0;
        if (sRegions[i].lang != PORT_LANG_EN || r < sRegions[i].start || r >= sRegions[i].end)
            continue;
        for (p = (sRegions[i].start + 3) & ~3u; p + 4 <= sRegions[i].end; p += 4) {
            if (!SitePtr(p, &t))
                continue;
            if (t == r) {
                found = 1;
                break;
            }
            k++;
        }
        if (!found)
            return -1;
        for (j = 0; j < sRegionCount; j++) {
            if (j == i || sRegions[j].lang != lang || sRegions[j].count != sRegions[i].count)
                continue;
            for (p = (sRegions[j].start + 3) & ~3u; p + 4 <= sRegions[j].end; p += 4) {
                if (!SitePtr(p, &t))
                    continue;
                if (k == 0)
                    return (long)t;
                k--;
            }
            return -1;
        }
        return -1;
    }
    return -1;
}

int Port_TextMultilingual(void)
{
    return gHostRom != NULL && Port_RomVersion(gHostRom, gHostRomSize) == 'P';
}

static const unsigned char *PageOf(const unsigned char *s);

static long Translate(uint32_t r, int lang)
{
    long res = Get(sCache, CACHE_BITS, r); /* 0 = none, else offset + 1 */
    if (res >= 0)
        return res - 1;
    res = PoolLookup(r, lang);
    if (res < 0)
        res = RegionLookup(r, lang);
    if (res >= 0)
        sPools_++;
    else if ((res = GroupLookup(r, lang)) >= 0)
        sGroups++;
    else {
        res = TableLookup(r, lang);
        if (res >= 0)
            sTables++;
        else
            sMisses++;
    }
    Put(sCache, CACHE_BITS, r, res < 0 ? 0 : (uint32_t)res + 1, 0);
    if (res >= 0)
        Put(sResults, RESULT_BITS, (uint32_t)res, 1, 1);
    return res;
}

const unsigned char *Port_Text(const unsigned char *s)
{
    int lang = gPortConfig.language;
    size_t n;
    long r, res;
    if (s == NULL || lang <= PORT_LANG_EN || lang >= PORT_LANG_COUNT)
        return s;
    if (!sInitDone) {
        sInitDone = 1;
        sDebug = getenv("PMD_TEXT_DEBUG") != NULL || gPortConfig.textDebug;
        sEnabled = Port_TextMultilingual();
        if (sEnabled)
            BuildIndex();
        sCacheLang = lang;
    }
    if (!sEnabled)
        return s;
    if (lang != sCacheLang) {
        memset(sCache, 0, sizeof(struct Entry) << CACHE_BITS);
        sFill[2] = 0;
        sCacheLang = lang;
    }
    sCalls++;
    if ((sCalls & 7) == 0 || sCalls < 64)
        Port_Crumb("text %.40s", s);
    if (s >= gHostRom && s < gHostRom + gHostRomSize) {
        r = s - gHostRom;
        if (r == 0 || gHostRom[r - 1] != 0)
            return PageOf(s); /* inside a string */
    } else {
        for (n = 0; n < MAX_STR && s[n]; n++)
            ;
        if (n < 2 || n >= MAX_STR)
            return s;
        r = Get(sIndex, INDEX_BITS, Fnv32(s, n));
        if (r < 0 || strcmp((const char *)gHostRom + r, (const char *)s) != 0) {
            const unsigned char *t = PageOf(s);
            if (sDebug && t == s && (s[-1] != 'P' || s[-2] != '#'))
                Platform_Log("text: not in the ROM: [%.60s]\n", s);
            return t;
        }
    }
    if (Get(sResults, RESULT_BITS, (uint32_t)r) >= 0)
        return s; /* already a translation */
    res = Translate((uint32_t)r, lang);
    if (res < 0 || gHostRom[res] == 0) {
        if (sDebug && res < 0)
            Platform_Log("text: no translation at %06lx: [%.60s]\n", r, s);
        return s;
    }
    return gHostRom + res;
}

/* A pointer into a string: the dialogue engine continues after a page
 * break ("#P") with a pointer into the original string. The whole string
 * is translated and the same page of the translation returned. */
static const unsigned char *PageOf(const unsigned char *s)
{
    const unsigned char *start = s, *t, *p;
    int pages = 0, k;
    if (s[-1] != 'P' || s[-2] != '#')
        return s;
    while (s - start < MAX_STR && start[-1] != 0)
        start--;
    if (start == s || s - start >= MAX_STR)
        return s;
    for (p = start; p < s - 1; p++)
        if (p[0] == '#' && p[1] == 'P')
            pages++;
    t = Port_Text(start);
    if (t == start)
        return s;
    for (p = t, k = 0; *p; p++) {
        if (p[0] == '#' && p[1] == 'P') {
            if (++k == pages)
                return p + 2;
        }
    }
    return s;
}

void Port_TextLogStats(void)
{
    if (sInitDone && sEnabled)
        Platform_Log("text: %u lookups: %u pools, %u groups, %u tables, %u without translation\n", sCalls, sPools_, sGroups, sTables, sMisses);
}
