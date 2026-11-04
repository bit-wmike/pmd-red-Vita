/*
 * Differential test for the translated sound engine (tools/port/asm2c.py).
 *
 * The same harness is linked three times (see test_m4a_diff.sh):
 *   - with the original src/m4a_1.s, as a static ARM binary run under qemu-arm
 *     (the hand-written code runs natively; the GBA I/O, IWRAM and EWRAM are
 *     mmap'd at their real addresses, the binary is linked at 0x08000000 so
 *     that all data lies where the engine's address checks expect ROM),
 *   - with the generated C on ARM (qemu-arm),
 *   - with the generated C on x86-32.
 * The outputs (per-frame checksums of the mixed PCM and of the engine state,
 * final dump) must be identical.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>
#define DUMPF (f < 2 || (f >= 60 && f < 63))

#include "gba/m4a_internal.h"
#include "m4a.h"

/* Absolute symbols (--defsym): 0x04000000 / 0x03000000 / 0x02000000. */
extern unsigned char gHostIo[], gHostIwram[], gHostEwram[];
extern struct SoundInfo gSoundInfo;
#define SOUND_INFO_PTR_TEST (*(struct SoundInfo **)0x3007FF0)
#define gSoundInfo_chans() (gSoundInfo.chans)

/* BIOS CpuSet / CpuFastSet replacements. */
void CpuSet(const void *src, void *dest, u32 control)
{
    u32 count = control & 0x1FFFFF;
    if (control & (1u << 26)) {
        u32 *d = dest; const u32 *s = src;
        if (control & (1u << 24)) { u32 v = *s; while (count--) *d++ = v; }
        else while (count--) *d++ = *s++;
    } else {
        u16 *d = dest; const u16 *s = src;
        if (control & (1u << 24)) { u16 v = *s; while (count--) *d++ = v; }
        else while (count--) *d++ = *s++;
    }
}
void CpuFastSet(const void *src, void *dest, u32 control)
{
    u32 count = (control & 0x1FFFFF);
    u32 *d = dest; const u32 *s = src;
    if (control & (1u << 24)) { u32 v = *s; while (count--) *d++ = v; }
    else while (count--) *d++ = *s++;
}

static struct MusicPlayerTrack sBgmTracks[6];
static struct MusicPlayerTrack sSeTracks[3];
const struct MusicPlayer gMPlayTable[] = {
    { &gMPlayInfo_BGM, sBgmTracks, 6, 0 },
    { &gMPlayInfo_SE1, sSeTracks, 3, 0 },
};
const struct Song gSongTable[] = { { 0, 0, 0 } };

/* ---- link cable helpers (src/arm_funcs.s) ---------------------------------- */
struct UnkStruct_202DCF8 {
    u8 unk0, unk1, unk2, unk3;
    s32 unk4;
    u8 unk8, unk9, unkA, unkB;
    u8 fillC[0x14 - 0xC];
    s32 unk14;
    s32 unk18[4];
    u16 *unk28;
    u16 *unk2C;
    u16 *unk30[4];
    u16 *unk40[4];
    u16 *unk50[4];
    u16 unk60[12];
    u16 unk78[12];
    u16 unk90[2][3][12];
};
struct UnkStruct_202DCF8 gUnknown_202DCF8;
static u16 sSioArr[12][12];
extern u32 sub_80001E8(void);
extern void sub_8000228(void);

static u32 ptr_id(const void *p)
{
    const u8 *b = p;
    if (b >= (u8 *)&gUnknown_202DCF8 && b < (u8 *)(&gUnknown_202DCF8 + 1))
        return 0x10000 + (u32)(b - (u8 *)&gUnknown_202DCF8);
    if (b >= (u8 *)sSioArr && b < (u8 *)sSioArr + sizeof(sSioArr))
        return 0x20000 + (u32)(b - (u8 *)sSioArr);
    return 0xffffffffu;
}

/* ---- song data, built in the EWRAM window --------------------------------- */
static u8 *ew_ptr;
static void *ew_alloc(u32 n) { void *p = ew_ptr; ew_ptr += (n + 3) & ~3u; memset(p, 0, n); return p; }

static struct WaveData *make_wave(u32 n, int looped, int kind)
{
    struct WaveData *w = ew_alloc(16 + n + 4);
    u32 i;
    w->type = 0;
    w->status = looped ? 0x4000 : 0;
    w->freq = 13379u << 10;
    w->loopStart = looped ? 8 : 0;
    w->size = n;
    for (i = 0; i < n; i++)
        w->data[i] = kind == 0 ? (s8)((i * 4) & 0xff) - 0 : (s8)((i * 37 + (i >> 3) * 11) & 0xff);
    return w;
}

static u8 *trk, *tp;
#define B(x) (*tp++ = (u8)(x))
#define PTR(p) do { void *p_ = (void *)(p); memcpy(tp, &p_, 4); tp += 4; } while (0)
#define W(idx) B(0x80 + (idx))
#define N(idx) B(0xCF + (idx))
enum { FINE = 0xb1, GOTO, PATT, PEND, REPT, MEMACC = 0xb9, PRIO, TEMPO, KEYSH, VOICE, VOL, PAN, BEND, BENDR,
       LFOS, LFODL, MOD, MODT, TUNE = 0xc8, PORT_ = 0xcb, XCMD = 0xcd, EOT, TIE };
static u8 *track_begin(void) { trk = tp = ew_alloc(1024); return trk; }

static struct SongHeader *make_song(u8 ntracks, u8 reverb, struct ToneData *tone, u8 **parts)
{
    struct SongHeader *h = ew_alloc(8 + 4 * ntracks);
    int i;
    h->trackCount = ntracks; h->blockCount = 0; h->priority = 0; h->reverb = reverb; h->tone = tone;
    for (i = 0; i < ntracks; i++) h->part[i] = parts[i];
    return h;
}

static struct SongHeader *sSongA, *sSongB;

static void build_data(void)
{
    struct WaveData *saw = make_wave(64, 1, 0);
    struct WaveData *hit = make_wave(300, 0, 1);
    struct ToneData *groupA = ew_alloc(12 * 8), *groupB = ew_alloc(12 * 2), *groupC = ew_alloc(12 * 40);
    u8 *split = ew_alloc(128);
    u8 *parts[6];
    u8 *loop, *lbl, *pat;
    int i;

    for (i = 0; i < 128; i++) split[i] = i < 64 ? 0 : 1;
    groupA[0] = (struct ToneData){ 0x00, 60, 0, 0, saw, 255, 0, 255, 200 };
    groupA[1] = (struct ToneData){ 0x08, 60, 0, 0, hit, 255, 230, 180, 160 };
    groupA[2] = (struct ToneData){ 0x01, 60, 0, 0x08, (struct WaveData *)2, 0, 7, 15, 3 };
    groupA[3] = (struct ToneData){ 0x40, 0, 0, 0, (struct WaveData *)groupB, 0, 0, 0, 0 };
    memcpy(&groupA[3].attack, &split, 4);
    groupA[4] = (struct ToneData){ 0x80, 0, 0, 0, (struct WaveData *)groupC, 0, 0, 0, 0 };
    groupA[5] = (struct ToneData){ 0x00, 60, 0, 0, saw, 100, 0, 255, 0 };
    groupA[6] = (struct ToneData){ 0x02, 60, 0, 0, (struct WaveData *)1, 0, 5, 12, 4 };
    groupA[7] = (struct ToneData){ 0x00, 60, 0, 0, hit, 255, 0, 255, 100 };
    groupB[0] = (struct ToneData){ 0x00, 60, 0, 0, saw, 255, 0, 255, 220 };
    groupB[1] = (struct ToneData){ 0x00, 60, 0, 0, hit, 255, 0, 255, 220 };
    groupC[36] = (struct ToneData){ 0x00, 48, 0, 0xC0, hit, 255, 200, 0, 0 };
    groupC[37] = (struct ToneData){ 0x08, 60, 0, 0xC0 + 16, hit, 255, 0, 255, 100 };
    groupC[38] = (struct ToneData){ 0x00, 72, 0, 0xC0 - 16, saw, 255, 0, 255, 100 };
    groupC[39] = (struct ToneData){ 0x00, 60, 0, 0x00, saw, 255, 0, 255, 100 };

    /* ---- song A (BGM) ---- */
    parts[0] = track_begin();
    B(TEMPO); B(75); B(VOICE); B(0); B(VOL); B(110); B(PAN); B(0x40); B(PRIO); B(5); B(KEYSH); B(0);
    B(BENDR); B(2); B(LFOS); B(22); B(LFODL); B(8); B(MOD); B(12); B(MODT); B(0);
    loop = tp;
    N(24); B(60); B(100); W(24); N(24); B(64); B(90); W(24);
    B(BEND); B(0x44); N(32); B(67); B(100); W(32); B(BEND); B(0x40); B(TUNE); B(0x42);
    B(PATT); pat = tp; PTR(0);
    lbl = tp; N(6); B(65); B(100); W(6); N(6); B(67); B(100); W(6); B(REPT); B(3); PTR(lbl);
    B(TIE); B(72); B(100); W(24); W(12); B(EOT); B(72);
    N(12); B(60); B(100); W(12); B(62); B(100); W(12);           /* running status */
    B(MEMACC); B(0); B(0); B(5); B(XCMD); B(8); B(40); B(XCMD); B(9); B(30);
    N(24); B(65); B(120); W(24); B(MODT); B(1); N(24); B(60); B(100); W(24); B(MODT); B(0);
    B(PORT_); B(0x02); B(0x11); B(KEYSH); B(2);
    B(GOTO); PTR(loop);
    { u8 *p = tp; N(12); B(60); B(100); W(12); N(12); B(62); B(100); W(12); B(PEND); memcpy(pat, &p, 4); }

    parts[1] = track_begin();
    B(VOICE); B(1); B(VOL); B(90); B(PAN); B(0x30);
    loop = tp;
    N(32); B(60); B(127); W(32); W(32); N(24); B(60); B(100); W(24); W(24); N(48); B(60); B(80); W(48);
    B(GOTO); PTR(loop);

    parts[2] = track_begin();
    B(VOICE); B(2); B(VOL); B(80); B(PAN); B(0x50);
    loop = tp;
    N(24); B(72); B(100); W(24); N(24); B(76); B(100); W(24); N(24); B(79); B(100); W(24); W(24);
    B(GOTO); PTR(loop);

    parts[3] = track_begin();
    B(VOICE); B(3); B(VOL); B(100);
    loop = tp;
    N(24); B(50); B(100); W(24); N(24); B(70); B(100); W(24);
    B(GOTO); PTR(loop);

    parts[4] = track_begin();
    B(VOICE); B(4); B(VOL); B(100);
    loop = tp;
    N(6); B(36); B(100); W(12); N(6); B(37); B(100); W(12); N(6); B(38); B(100); W(12); N(6); B(39); B(100); W(12);
    B(GOTO); PTR(loop);

    parts[5] = track_begin();
    B(VOICE); B(7); B(VOL); B(100); B(PAN); B(0x40);
    loop = tp;
    N(48); B(60); B(100); W(48); W(48); N(48); B(48); B(100); W(48); W(48);
    B(VOICE); B(5); N(32); B(62); B(100); W(32); W(32); B(VOICE); B(7);
    B(GOTO); PTR(loop);
    sSongA = make_song(6, 0x80 | 40, groupA, parts);

    /* ---- song B (SE) ---- */
    parts[0] = track_begin();
    B(VOICE); B(1); B(VOL); B(127); N(24); B(60); B(127); W(24); B(FINE);
    parts[1] = track_begin();
    B(VOICE); B(6); B(VOL); B(100); N(24); B(80); B(100); W(24); N(12); B(84); B(100); W(12); B(FINE);
    parts[2] = track_begin();
    B(VOICE); B(0); B(PRIO); B(20); N(32); B(55); B(100); W(32); B(FINE);
    sSongB = make_song(3, 0, groupA, parts);
}

/* ---- state hashing ---------------------------------------------------------- */
static u32 fnv(u32 h, const void *p, u32 n)
{
    const u8 *b = p;
    while (n--) h = (h ^ *b++) * 16777619u;
    return h;
}

static u32 hash_chan(u32 h, const struct SoundChannel *c)
{
    h = fnv(h, c, 24);                       /* statusFlags .. rhythmPan, d3 */
    h = fnv(h, &c->count, 12);               /* count, fw, frequency */
    h = fnv(h, &c->xpi, 4);
    if (c->wav)
        h = fnv(h, &c->wav->size, 4);
    if (c->currentPointer && c->wav) {
        u32 off = (u32)(c->currentPointer - c->wav->data);
        h = fnv(h, &off, 4);
    }
    return h;
}

/* Channel index: 0..11 DirectSound, 100.. CGB (the pointers differ between builds). */
static int chan_index(const void *chan)
{
    if (!chan) return -1;
    if ((const u8 *)chan >= (const u8 *)gCgbChans && (const u8 *)chan < (const u8 *)(gCgbChans + 4))
        return 100 + (int)((const struct CgbChannel *)chan - gCgbChans);
    return (int)((const struct SoundChannel *)chan - gSoundInfo.chans);
}

static u32 hash_player(u32 h, const struct MusicPlayerInfo *m, const struct MusicPlayerTrack *base)
{
    int i;
    h = fnv(h, &m->status, 4); h = fnv(h, &m->trackCount, 4); h = fnv(h, &m->clock, 4);
    h = fnv(h, &m->tempoD, 14);
    for (i = 0; i < m->trackCount; i++) {
        const struct MusicPlayerTrack *t = &m->tracks[i];
        u32 off;
        h = fnv(h, t, 32);                   /* flags .. pseudoEchoLength */
        h = fnv(h, &t->tone, 4);             /* type, key, length, pan_sweep */
        off = (u32)chan_index(t->chan);
        h = fnv(h, &off, 4);
        off = t->cmdPtr ? (u32)(t->cmdPtr - gHostEwram) : 0xffffffffu;
        h = fnv(h, &off, 4);
    }
    return h;
}

int main(void)
{
    struct SoundInfo *si;
    int f, i;
    u32 h;

    setvbuf(stdout, NULL, _IONBF, 0);
#ifdef HANG_DEBUG
    alarm(10);
#endif

    if (mmap((void *)0x02000000, 0x40000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED
        || mmap((void *)0x03000000, 0x8000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED
        || mmap((void *)0x04000000, 0x1000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
#ifdef REF_BUILD
    {
        extern char SoundMainRAM_Buffer[];
        uintptr_t a = (uintptr_t)SoundMainRAM_Buffer & ~4095u;
        if (mprotect((void *)a, 8192, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
            perror("mprotect");
            return 1;
        }
    }
#endif
    ew_ptr = gHostEwram + 0x1000;
    build_data();

    m4aSoundInit();
    si = SOUND_INFO_PTR_TEST;
    printf("init: ident=%08x freq=%u spv=%d period=%u pcmFreq=%d divFreq=%d maxChans=%u reverb=%u\n",
           si->ident, si->freq, si->pcmSamplesPerVBlank, si->pcmDmaPeriod, si->pcmFreq, si->divFreq, si->maxChans, si->reverb);
    printf("jumptable null entries:");
    for (i = 0; i < 36; i++) if (gMPlayJumpTable[i] == 0) printf(" %d", i);
    printf("\n");

    for (f = 0; f < 420; f++) {
        if (f == 0) MPlayStart(&gMPlayInfo_BGM, sSongA);
        if (f == 60 || f == 120) MPlayStart(&gMPlayInfo_SE1, sSongB);
        if (f == 200) m4aMPlayFadeOut(&gMPlayInfo_BGM, 3);
        if (f == 300) MPlayStart(&gMPlayInfo_BGM, sSongA);
        if (f == 350) m4aMPlayStop(&gMPlayInfo_SE1);
        if (f == 380) m4aMPlayFadeIn(&gMPlayInfo_BGM, 2);
        m4aSoundVSync();
        m4aSoundMain();
        h = fnv(2166136261u, si->pcmBuffer, sizeof(si->pcmBuffer));
        if (DUMPF) printf("  pcm %08x\n", h);
        h = fnv(h, &si->pcmDmaCounter, 1);
        h = fnv(h, &si->c15, 1);
        for (i = 0; i < MAX_DIRECTSOUND_CHANNELS; i++) { h = hash_chan(h, &si->chans[i]); if (DUMPF) printf("  chan%d %08x\n", i, h); }
        for (i = 0; i < 4; i++) {          /* CgbChannel without its track pointer */
            const struct MusicPlayerTrack *t = gCgbChans[i].track;   /* track id independent of the layout */
            u32 off = !t ? 0xffffffffu : (t >= sBgmTracks && t < sBgmTracks + 6) ? (u32)(t - sBgmTracks) : 100 + (u32)(t - sSeTracks);
            h = fnv(h, &gCgbChans[i], 0x24);
            h = fnv(h, &gCgbChans[i].wavePointer, 8);
            h = fnv(h, &off, 4);
        }
        if (DUMPF) printf("  cgb %08x\n", h);
        h = hash_player(h, &gMPlayInfo_BGM, sBgmTracks);
        if (DUMPF) {
            const struct MusicPlayerInfo *m = &gMPlayInfo_SE1;
            int k;
            printf("  bgm %08x st=%08x tc=%u pr=%u cmd=%u ub=%u clk=%u tempo=%u,%u,%u,%u fade=%u,%u,%u\n", h, m->status, m->trackCount,
                   m->priority, m->cmd, m->unk_B, m->clock, m->tempoD, m->tempoU, m->tempoI, m->tempoC, m->fadeOI, m->fadeOC, m->fadeOV);
            for (k = 0; k < m->trackCount; k++) {
                const u8 *b = (const u8 *)&m->tracks[k];
                printf("   t%d ", k);
                for (i = 0; i < 32; i++) printf("%02x", b[i]);
                printf(" tone=%02x%02x%02x%02x chan=%d cmd=%x\n", b[36], b[37], b[38], b[39],
                       chan_index(m->tracks[k].chan),
                       m->tracks[k].cmdPtr ? (unsigned)(m->tracks[k].cmdPtr - gHostEwram) : 0);
            }
        }
        h = hash_player(h, &gMPlayInfo_SE1, sSeTracks);
        if (DUMPF) printf("  se %08x\n", h);
        h = fnv(h, gHostIo + 0x60, 0x50);
        h = fnv(h, gHostIo + 0xC4, 4);      /* DMA1CNT, DMA2CNT (the SAD registers hold a host pointer) */
        h = fnv(h, gHostIo + 0xD0, 4);
        printf("%3d %08x cnt=%u st=%08x/%08x tc=%u clk=%u fv=%u\n", f, h, si->pcmDmaCounter,
               gMPlayInfo_BGM.status, gMPlayInfo_SE1.status, gMPlayInfo_BGM.tempoC, gMPlayInfo_BGM.clock, gMPlayInfo_BGM.fadeOV);
    }
    printf("chans:");
    for (i = 0; i < MAX_DIRECTSOUND_CHANNELS; i++)
        printf(" [%02x %02x %u %u]", si->chans[i].statusFlags, si->chans[i].envelopeVolume, si->chans[i].count, si->chans[i].fw);
    printf("\n");
    for (i = 0; i < PCM_DMA_BUF_SIZE * 2; i++) {
        printf("%02x", (u8)si->pcmBuffer[i]);
        if ((i & 63) == 63) printf("\n");
    }
    /* ---- link cable helpers ---- */
    {
        struct UnkStruct_202DCF8 *g = &gUnknown_202DCF8;
        int k;
        memset(g, 0, sizeof(*g));
        g->unk14 = -1;
        g->unk28 = g->unk60;
        g->unk2C = g->unk78;
        for (i = 0; i < 4; i++) {
            g->unk30[i] = sSioArr[i];
            g->unk40[i] = sSioArr[4 + i];
            g->unk50[i] = sSioArr[8 + i];
            g->unk18[i] = i * 3;
        }
        for (i = 0; i < 12; i++) { g->unk60[i] = 0x1000 + i; g->unk78[i] = 0x2000 + i * 7; }
        for (k = 0; k < 60; k++) {
            u32 ids[14];
            *(u32 *)(gHostIo + 0x120) = 0x01010101u * (u32)k + 0x00fe0000u * (k & 1);
            *(u32 *)(gHostIo + 0x124) = (k % 7 == 0) ? 0xfefefefeu : 0x12345678u + (u32)k * 0x1001u;
            *(u16 *)(gHostIo + 0x128) = (k & 2) ? 0x40 : 0;
            g->unk0 = (u8)(k % 3);
            g->unk4 = k * 5;
            sub_8000228();
            if (k % 4 == 0)
                printf("sio %d: sub_80001E8 -> %u\n", k, sub_80001E8());
            ids[0] = ptr_id(g->unk28); ids[1] = ptr_id(g->unk2C);
            for (i = 0; i < 4; i++) { ids[2 + i] = ptr_id(g->unk30[i]); ids[6 + i] = ptr_id(g->unk40[i]); ids[10 + i] = ptr_id(g->unk50[i]); }
            h = fnv(2166136261u, g, 0x28);
            h = fnv(h, ids, sizeof(ids));
            h = fnv(h, g->unk60, sizeof(*g) - 0x60);
            h = fnv(h, sSioArr, sizeof(sSioArr));
            h = fnv(h, gHostIo + 0x100, 0x30);
            h = fnv(h, gHostIo + 0x208, 2);
            printf("sio %d: %08x unk14=%d unk4=%d unk9=%u io12a=%04x tm3=%04x\n", k, h, g->unk14, g->unk4, g->unk9,
                   *(u16 *)(gHostIo + 0x12A), *(u16 *)(gHostIo + 0x10E));
        }
    }
    return 0;
}
