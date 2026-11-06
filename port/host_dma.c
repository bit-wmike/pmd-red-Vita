/*
 * DMA for the host / PS Vita port.
 *
 * The game's DmaSet macro (include/gba/macro.h) calls HostDmaSet. Immediate
 * transfers are plain copies; the HBlank-timed transfer on channel 0 (the
 * per-scanline WIN0H/WIN1H table for text windows and the dungeon darkness)
 * is remembered and replayed by the renderer once per visible scanline.
 * Sound FIFO DMA (channels 1/2) is emulated by port/host_audio.c directly
 * from the sound engine's mixing buffer, so those channels are ignored here.
 */
#include <string.h>

#include "port/port.h"
#include "gba/types.h"
#include "gba/io_reg.h"

struct HostHdma {
    int active;
    const u8 *src;
    u8 *dst;
    u32 units;
    u32 unitSize;
    u32 control;
};

static struct HostHdma sHdma[4];

static void Transfer(const u8 *src, u8 *dst, u32 units, u32 unitSize, u32 control)
{
    int srcStep = (int)unitSize, dstStep = (int)unitSize;
    u32 i;

    switch ((control >> 7) & 3) { /* source control */
        case 1: srcStep = -(int)unitSize; break;
        case 2: srcStep = 0; break;
    }
    switch ((control >> 5) & 3) { /* destination control */
        case 1: dstStep = -(int)unitSize; break;
        case 2: dstStep = 0; break;
        /* 3 = increment + reload: increments during the transfer */
    }

    if (srcStep == (int)unitSize && dstStep == (int)unitSize) {
        memmove(dst, src, (size_t)units * unitSize);
        return;
    }
    for (i = 0; i < units; i++) {
        if (unitSize == 4)
            memcpy(dst, src, 4);
        else
            memcpy(dst, src, 2);
        src += srcStep;
        dst += dstStep;
    }
}

void HostDmaSet(int channel, const void *src, void *dst, unsigned int cnt)
{
    u32 control = cnt >> 16;
    u32 units = cnt & 0xFFFF;
    u32 unitSize = (control & DMA_32BIT) ? 4 : 2;

    if (channel < 0 || channel > 3)
        return;

    if (!(control & DMA_ENABLE) || src == NULL) {
        sHdma[channel].active = 0;
        return;
    }
    if (channel == 1 || channel == 2)
        return; /* sound FIFO */
    if (units == 0)
        units = (channel == 3) ? 0x10000 : 0x4000;

    switch (control & DMA_START_MASK) {
        case DMA_START_NOW:
            Transfer((const u8 *)src, (u8 *)dst, units, unitSize, control);
            sHdma[channel].active = 0;
            break;

        case DMA_START_HBLANK:
            sHdma[channel].active = 1;
            sHdma[channel].src = (const u8 *)src;
            sHdma[channel].dst = (u8 *)dst;
            sHdma[channel].units = units;
            sHdma[channel].unitSize = unitSize;
            sHdma[channel].control = control;
            break;

        default:
            break;
    }
}

void Port_MapOverlayHBlank(void); /* port/port_cheats.c (map_overlay) */

/* One HBlank: every armed HBlank DMA moves one block. */
void HostDma_HBlank(void)
{
    int ch;
    Port_MapOverlayHBlank();
    for (ch = 0; ch < 4; ch++) {
        struct HostHdma *d = &sHdma[ch];
        if (!d->active)
            continue;
        Transfer(d->src, d->dst, d->units, d->unitSize, d->control);
        if (((d->control >> 7) & 3) == 0)
            d->src += d->units * d->unitSize;
        if (((d->control >> 5) & 3) == 0)
            d->dst += d->units * d->unitSize;
        if (!(d->control & DMA_REPEAT))
            d->active = 0;
    }
}

void HostDma_Reset(void)
{
    memset(sHdma, 0, sizeof(sHdma));
}
