/*
 * GBA sound hardware for the host / PS Vita port.
 *
 *  - Direct sound: the game's m4a engine mixes one frame of 8-bit samples
 *    per VBlank into SoundInfo.pcmBuffer (two halves: A = right, B = left),
 *    which the sound FIFO DMA would play during the next frame. The port
 *    takes that block straight from the buffer once per frame.
 *  - PSG: the four Game Boy channels (two squares, wave, noise) are emulated
 *    from the sound registers the game writes.
 *
 * Once per frame the result is resampled to PORT_AUDIO_RATE and passed to
 * the platform.
 */
#include <string.h>
#include <stdlib.h>

#include "port/port.h"
#include "gba/types.h"
#include "gba/m4a_internal.h"

#define FRAME_OUT (PORT_AUDIO_RATE / 60)
#define MAX_FIFO 1024

#define IO8(off) (*(volatile u8 *)(gHostIo + (off)))
#define IO16(off) (*(volatile u16 *)(gHostIo + (off)))

static int8_t sFifoA[MAX_FIFO], sFifoB[MAX_FIFO];
static int sFifoCount;
static int sLastA, sLastB;
static int16_t sOut[FRAME_OUT * 2];

struct Envelope {
    int volume;
    int dir;
    int period;
    int timer;
};

struct Square {
    int on;
    u32 phase; /* one waveform period = 2^32 */
    int duty;
    struct Envelope env;
    int lengthTimer; /* in 1/256 s ticks remaining, -1 = off */
    int freqReg;
    int sweepPeriod, sweepDir, sweepShift, sweepTimer;
};

struct Wave {
    int on;
    u32 phase;
    int lengthTimer;
};

struct Noise {
    int on;
    u64 phase; /* LFSR clocks, 32.32 fixed point */
    u16 lfsr;
    struct Envelope env;
    int lengthTimer;
};

static struct Square sSq[2];
static struct Wave sWave;
static struct Noise sNoise;
static u32 sFrameSeqAcc; /* 512 Hz frame sequencer */
static int sFrameSeqStep;
static int sLastFreq1;

static const u8 sDuty[4][8] = {
    {0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 1, 1, 1},
    {0, 1, 1, 1, 1, 1, 1, 0},
};

void HostAudio_Init(void)
{
    memset(sSq, 0, sizeof(sSq));
    memset(&sWave, 0, sizeof(sWave));
    memset(&sNoise, 0, sizeof(sNoise));
    sFifoCount = 0;
    sLastA = sLastB = 0;
}

/* The block the sound engine mixed since the last VBlank (SoundMain writes
 * block period - (counter - 1), or 0 when the counter is <= 1). */
static void TakePcmBlock(void)
{
    struct SoundInfo *si = SOUND_INFO_PTR;
    int n, block, c;
    const int8_t *a, *b;

    sFifoCount = 0;
#ifndef __vita__
    {
        static int debug = -1;
        if (debug < 0)
            debug = getenv("PMD_AUDIO_DEBUG") != NULL;
        if (debug && (Port_FrameCounter() % 61) == 0) {
            int sum = 0, k;
            if (si && si->pcmDmaPeriod) {
                int cc = si->pcmDmaCounter, nn = si->pcmSamplesPerVBlank;
                int bb = (cc <= 1) ? 0 : si->pcmDmaPeriod - (cc - 1);
                for (k = 0; k < nn && k < 1024; k++)
                    sum += abs(si->pcmBuffer[bb * nn + k]);
            }
            Platform_Log("audio: frame %u ident=%08x counter=%d period=%d spv=%d cntH=%04x cntX=%02x snd1=%04x snd2=%04x blockabs=%d\n",
                         (unsigned)Port_FrameCounter(), si ? (unsigned)si->ident : 0, si ? si->pcmDmaCounter : -1,
                         si ? si->pcmDmaPeriod : -1, si ? (int)si->pcmSamplesPerVBlank : -1, IO16(0x82), IO8(0x84),
                         IO16(0x62), IO16(0x68), sum);
        }
    }
#endif
    if (si == NULL || (si->ident != ID_NUMBER && si->ident != ID_NUMBER + 1))
        return;
    n = si->pcmSamplesPerVBlank;
    if (n <= 0 || n > MAX_FIFO || si->pcmDmaPeriod == 0)
        return;
    c = si->pcmDmaCounter;
    block = (c <= 1) ? 0 : si->pcmDmaPeriod - (c - 1);
    if (block < 0 || (block + 1) * n > PCM_DMA_BUF_SIZE)
        return;
    a = (const int8_t *)si->pcmBuffer + block * n;
    b = (const int8_t *)si->pcmBuffer + PCM_DMA_BUF_SIZE + block * n;
    memcpy(sFifoA, a, (size_t)n);
    memcpy(sFifoB, b, (size_t)n);
    sFifoCount = n;
}

static void EnvTrigger(struct Envelope *e, u16 reg)
{
    e->volume = (reg >> 12) & 0xF;
    e->dir = (reg >> 11) & 1;
    e->period = (reg >> 8) & 7;
    e->timer = e->period;
}

static void EnvTick(struct Envelope *e)
{
    if (e->period == 0)
        return;
    if (--e->timer > 0)
        return;
    e->timer = e->period;
    if (e->dir && e->volume < 15)
        e->volume++;
    else if (!e->dir && e->volume > 0)
        e->volume--;
}

static void ProcessTriggers(void)
{
    u16 r;

    r = IO16(0x64);
    if (r & 0x8000) {
        u16 duty = IO16(0x62);
        u16 sweep = IO16(0x60);
        struct Square *s = &sSq[0];
        s->on = 1;
        EnvTrigger(&s->env, duty);
        s->lengthTimer = (r & 0x4000) ? 64 - (duty & 0x3F) : -1;
        s->sweepPeriod = (sweep >> 4) & 7;
        s->sweepDir = (sweep >> 3) & 1;
        s->sweepShift = sweep & 7;
        s->sweepTimer = s->sweepPeriod;
        s->freqReg = r & 0x7FF;
        sLastFreq1 = s->freqReg;
        IO16(0x64) = r & 0x7FFF;
    }
    sSq[0].duty = (IO16(0x62) >> 6) & 3;
    if ((IO16(0x64) & 0x7FF) != sLastFreq1) {
        sLastFreq1 = IO16(0x64) & 0x7FF;
        sSq[0].freqReg = sLastFreq1;
    }

    r = IO16(0x6C);
    if (r & 0x8000) {
        u16 duty = IO16(0x68);
        struct Square *s = &sSq[1];
        s->on = 1;
        EnvTrigger(&s->env, duty);
        s->lengthTimer = (r & 0x4000) ? 64 - (duty & 0x3F) : -1;
        s->sweepPeriod = 0;
        IO16(0x6C) = r & 0x7FFF;
    }
    sSq[1].duty = (IO16(0x68) >> 6) & 3;
    sSq[1].freqReg = IO16(0x6C) & 0x7FF;

    r = IO16(0x74);
    if (r & 0x8000) {
        sWave.on = 1;
        sWave.phase = 0;
        sWave.lengthTimer = (r & 0x4000) ? 256 - (IO16(0x72) & 0xFF) : -1;
        IO16(0x74) = r & 0x7FFF;
    }
    if (!(IO8(0x70) & 0x80))
        sWave.on = 0;

    r = IO16(0x7C);
    if (r & 0x8000) {
        u16 env = IO16(0x78);
        sNoise.on = 1;
        EnvTrigger(&sNoise.env, env);
        sNoise.lengthTimer = (r & 0x4000) ? 64 - (env & 0x3F) : -1;
        sNoise.lfsr = 0x7FFF;
        IO16(0x7C) = r & 0x7FFF;
    }
}

static void FrameSequencerTick(void)
{
    int step = sFrameSeqStep++ & 7;
    int i;

    if ((step & 1) == 0) {
        for (i = 0; i < 2; i++)
            if (sSq[i].lengthTimer > 0 && --sSq[i].lengthTimer == 0)
                sSq[i].on = 0;
        if (sWave.lengthTimer > 0 && --sWave.lengthTimer == 0)
            sWave.on = 0;
        if (sNoise.lengthTimer > 0 && --sNoise.lengthTimer == 0)
            sNoise.on = 0;
    }
    if (step == 2 || step == 6) {
        struct Square *s = &sSq[0];
        if (s->on && s->sweepPeriod && --s->sweepTimer <= 0) {
            int delta = s->freqReg >> s->sweepShift;
            s->sweepTimer = s->sweepPeriod;
            if (s->sweepShift) {
                int f = s->sweepDir ? s->freqReg - delta : s->freqReg + delta;
                if (f > 2047)
                    s->on = 0;
                else if (f >= 0)
                    s->freqReg = f;
            }
        }
    }
    if (step == 7) {
        EnvTick(&sSq[0].env);
        EnvTick(&sSq[1].env);
        EnvTick(&sNoise.env);
    }
}

static u32 PhaseInc(u64 numerator, u64 denominator)
{
    return (u32)((numerator << 32) / (denominator * PORT_AUDIO_RATE));
}

/* The phase steps only change with the frequency registers: computed once
 * per change instead of a 64-bit division per sample (slow on ARM). */
static u32 SquareInc(int ch, int freqReg)
{
    static int key[2] = {-1, -1};
    static u32 inc[2];
    if (freqReg != key[ch]) {
        key[ch] = freqReg;
        inc[ch] = PhaseInc(131072, 2048 - freqReg);
    }
    return inc[ch];
}

static u32 WaveInc(int freqReg)
{
    static int key = -1;
    static u32 inc;
    if (freqReg != key) {
        key = freqReg;
        inc = PhaseInc(2097152, (u64)(2048 - freqReg) * 32);
    }
    return inc;
}

static u64 NoiseInc(u16 r)
{
    static int key = -1;
    static u64 inc;
    if ((r & 0xF7) != key) {
        int shift = (r >> 4) & 0xF;
        u64 num = 1048576;
        u64 den = (u64)((r & 7) ? (r & 7) * 2 : 1) << (shift + 1);
        key = r & 0xF7;
        inc = (num << 32) / (den * PORT_AUDIO_RATE);
    }
    return inc;
}

static void PsgSample(int *left, int *right)
{
    int ch[4] = {0, 0, 0, 0};
    u16 cntL = IO16(0x80);
    int i;

    sFrameSeqAcc += 512;
    while (sFrameSeqAcc >= PORT_AUDIO_RATE) {
        sFrameSeqAcc -= PORT_AUDIO_RATE;
        FrameSequencerTick();
    }

    for (i = 0; i < 2; i++) {
        struct Square *s = &sSq[i];
        if (s->on && s->env.volume) {
            s->phase += SquareInc(i, s->freqReg);
            ch[i] = sDuty[s->duty][s->phase >> 29] ? s->env.volume : -s->env.volume;
        }
    }

    if (sWave.on) {
        u16 cntH = IO16(0x72);
        int freqReg = IO16(0x74) & 0x7FF;
        int pos, nib, vol;
        sWave.phase += WaveInc(freqReg);
        pos = (int)(sWave.phase >> 27);
        nib = (gHostIo[0x90 + pos / 2] >> ((pos & 1) ? 0 : 4)) & 0xF;
        vol = (cntH >> 13) & 3;
        nib = nib * 2 - 15;
        if (cntH & 0x8000)
            ch[2] = nib * 3 / 4;
        else if (vol == 0)
            ch[2] = 0;
        else
            ch[2] = nib >> (vol - 1);
    }

    if (sNoise.on && sNoise.env.volume) {
        u16 r = IO16(0x7C);
        sNoise.phase += NoiseInc(r);
        while (sNoise.phase >= ((u64)1 << 32)) {
            int bit = (sNoise.lfsr ^ (sNoise.lfsr >> 1)) & 1;
            sNoise.phase -= (u64)1 << 32;
            sNoise.lfsr = (sNoise.lfsr >> 1) | (bit << 14);
            if (r & 8)
                sNoise.lfsr = (sNoise.lfsr & ~0x40) | (bit << 6);
        }
        ch[3] = (sNoise.lfsr & 1) ? -sNoise.env.volume : sNoise.env.volume;
    }

    *left = *right = 0;
    for (i = 0; i < 4; i++) {
        if (cntL & (0x1000 << i))
            *left += ch[i];
        if (cntL & (0x100 << i))
            *right += ch[i];
    }
    *left *= ((cntL >> 4) & 7) + 1;
    *right *= (cntL & 7) + 1;
}

void HostAudio_EndFrame(void)
{
    u16 cntH = IO16(0x82);
    int psgShift = 2 - (cntH & 3);
    int dmaAShift = (cntH & 4) ? 2 : 1;
    int dmaBShift = (cntH & 8) ? 2 : 1;
    int master = IO8(0x84) & 0x80;
    int i;

    TakePcmBlock();
    ProcessTriggers();

    for (i = 0; i < FRAME_OUT; i++) {
        int a, b, l = 0, r = 0, pl, pr;
        if (sFifoCount > 0) {
            /* i * count * 256 < 2^31 (count <= MAX_FIFO): 32-bit division by a constant */
            int fixed = (i * sFifoCount << 8) / FRAME_OUT;
            int idx = fixed >> 8, frac = fixed & 0xFF;
            int a0 = idx == 0 ? sLastA : sFifoA[idx - 1];
            int b0 = idx == 0 ? sLastB : sFifoB[idx - 1];
            a = (a0 * (256 - frac) + sFifoA[idx] * frac) >> 8;
            b = (b0 * (256 - frac) + sFifoB[idx] * frac) >> 8;
        } else {
            a = b = 0;
        }
        if (cntH & 0x0100) r += a << dmaAShift;
        if (cntH & 0x0200) l += a << dmaAShift;
        if (cntH & 0x1000) r += b << dmaBShift;
        if (cntH & 0x2000) l += b << dmaBShift;

        PsgSample(&pl, &pr);
        if (psgShift >= 0) {
            l += pl >> psgShift;
            r += pr >> psgShift;
        }
        if (!master)
            l = r = 0;

        l *= 28;
        r *= 28;
        if (l > 32767) l = 32767;
        if (l < -32768) l = -32768;
        if (r > 32767) r = 32767;
        if (r < -32768) r = -32768;
        sOut[i * 2] = (int16_t)l;
        sOut[i * 2 + 1] = (int16_t)r;
    }

    if (sFifoCount > 0) {
        sLastA = sFifoA[sFifoCount - 1];
        sLastB = sFifoB[sFifoCount - 1];
    }
    sFifoCount = 0;
    Platform_SubmitAudio(sOut, FRAME_OUT);
}
