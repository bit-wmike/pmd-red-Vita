/*
 * Frame loop of the host / PS Vita port: stands in for the GBA's display
 * timing and interrupts.
 *
 * The game runs its logic until it waits for VBlank (VBlankIntrWait, or the
 * Halt syscall). At that point the port
 *   1. renders the 160 visible scanlines with the software PPU, running the
 *      HBlank DMA and the VCount interrupt between scanlines,
 *   2. shows the frame and raises the VBlank interrupt (the game's handler
 *      runs the sound engine),
 *   3. takes the frame's audio and reads the controls.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "port/port.h"
#include "port/port_view.h"
#include "port/port_overlay.h"
#include "port/port_config.h"
#include "port/port_game.h"
#include "port/ppu/include/virtuappu.h"
#include "port/ppu/include/cpu/mode1.h"

#include "gba/types.h"
#include "gba/io_reg.h"

void AgbMain(void);
extern void (*gIntrTable[6])(void);

static int sRendering;
static uint32_t sFrameCounter;
static uint32_t sFrameBuffer[PORT_VIEW_MAX_W * PORT_VIEW_MAX_H];
static int sMainLoopHalt;
static int sResetRequested;

#define IO16(off) (*(volatile u16 *)(gHostIo + (off)))

/* ---------------------------------------------------------------------- */

void *HostVcountPtr(void)
{
    if (!sRendering) {
        /* Outside of rendering, every read lets the beam move on one line,
         * so the game's VCOUNT polling loops finish. */
        u16 vc = IO16(0x006);
        IO16(0x006) = (u16)((vc + 1) % 228);
    }
    return gHostIo + 0x006;
}

/* Interrupt dispatch order of the game's IntrMain (src/crt0.s). */
static int IrqIndex(u16 flag)
{
    if (flag & (INTR_FLAG_SERIAL | INTR_FLAG_TIMER3)) return 0;
    if (flag & INTR_FLAG_VBLANK) return 1;
    if (flag & INTR_FLAG_VCOUNT) return 2;
    if (flag & INTR_FLAG_TIMER2) return 3;
    if (flag & INTR_FLAG_GAMEPAK) return 5;
    return -1;
}

static void RaiseIrq(u16 flag)
{
    int index;
    if (!(IO16(0x208) & 1) || !(IO16(0x200) & flag))
        return;
    index = IrqIndex(flag);
    if (index < 0 || gIntrTable[index] == NULL)
        return;
    gIntrTable[index]();
}

void HostRunFrame(void);

/* Software interrupts the game issues inline. */
void HostSyscall(int num)
{
    switch (num) {
        case 0: /* SoftReset */
            sResetRequested = 1;
            /* fall through: the game loop is left by the reset check */
        case 2: /* Halt: wait for the next interrupt, which is VBlank */
            sMainLoopHalt = 1;
            HostRunFrame();
            sMainLoopHalt = 0;
            break;
        default: /* Stop (sleep) and others: return immediately */
            break;
    }
}

void VBlankIntrWait(void)
{
    HostRunFrame();
}

/* ---- display ---------------------------------------------------------- */

static void PreLine(int line)
{
    u16 dispstat = IO16(0x004);
    int prev = line == 0 ? 227 : line - 1;

    /* HBlank of the previous scanline */
    IO16(0x006) = (u16)prev;
    if (line != 0)
        HostDma_HBlank();
    if (dispstat & DISPSTAT_HBLANK_INTR)
        RaiseIrq(INTR_FLAG_HBLANK);

    /* Start of this scanline */
    IO16(0x006) = (u16)line;
    if ((dispstat & DISPSTAT_VCOUNT_INTR) && (dispstat >> 8) == line)
        RaiseIrq(INTR_FLAG_VCOUNT);
}

/* A frame that is computed but not drawn: the scanline events still happen. */
static void SkipFrame(void)
{
    int line;
    sRendering = 1;
    for (line = 0; line < 160; line++)
        PreLine(line);
    sRendering = 0;
}

static void RenderFrame(void)
{
    static int bound;
    PPUMemory ppu;
    u16 dispcnt = IO16(0x000);
    int width, height;

    if (!bound) {
        VirtuaPPUMode1GbaMemory mem = {
            gHostIo, gHostVram, (uint16_t *)gHostPalram, (uint16_t *)(gHostPalram + 0x200), (uint16_t *)gHostOam,
        };
        virtuappu_mode1_bind_gba_memory(&mem);
        virtuappu_mode1_set_old3ds_profile(true);
        virtuappu_mode1_set_output_buffer(sFrameBuffer, PORT_VIEW_MAX_W);
        bound = 1;
    }

    Port_ViewBeforeRender(&width, &height);

    memset(&ppu, 0, sizeof(ppu));
    ppu.frame_width = (uint16_t)width;
    ppu.frame_height = (uint16_t)height;
    ppu.frame_pitch = PORT_VIEW_MAX_W;
    ppu.mode = ((dispcnt & 7) == 0) ? 1 : 2;

    virtuappu_mode1_pre_line_callback = PreLine;
    sRendering = 1;
    Port_ProfileBegin(PORT_PROF_RENDER);
    virtuappu_mode1_render_frame(&ppu);
    Port_ProfileEnd(PORT_PROF_RENDER);
    sRendering = 0;
    {
        int top, bottom, y;
        Port_ViewBlankRows(&top, &bottom);
#ifndef __vita__
        if (getenv("PMD_DARK_DEBUG") && sFrameCounter % 300 == 0)
            Platform_Log("blankrows: top %d bottom %d width %d height %d\n", top, bottom, width, height);
#endif
        if (bottom > top) {
            for (y = 0; y < top && y < height; y++)
                memset(sFrameBuffer + y * PORT_VIEW_MAX_W, 0, (size_t)width * 4);
            for (y = bottom; y < height; y++)
                memset(sFrameBuffer + y * PORT_VIEW_MAX_W, 0, (size_t)width * 4);
        }
    }

    /* port overlay (HP bars, clock) on top of the finished frame */
    Port_OverlayDraw(sFrameBuffer, width, height, PORT_VIEW_MAX_W);

    Platform_Present(sFrameBuffer, width, height, PORT_VIEW_MAX_W, Port_ViewZoomHeight());
}

/* ---- frame ------------------------------------------------------------ */

void HostRunFrame(void)
{
    u16 keys;

    Port_ViewBeforeLogicEnd();
    if ((gPortConfig.frameRate30 && (sFrameCounter & 1)) || !Platform_PaceAllowsRender())
        SkipFrame();
    else
        RenderFrame();

    /* VBlank */
    IO16(0x006) = 160;
    IO16(0x004) |= DISPSTAT_VBLANK;
    RaiseIrq(INTR_FLAG_VBLANK);
    Port_ViewOnVBlank();
    Port_OverlayOnVBlank();
    virtuappu_mode1_commit_obj_metadata();
    IO16(0x004) &= ~DISPSTAT_VBLANK;

    HostAudio_EndFrame();

    keys = Port_TranslateKeys(Platform_PollKeys());
    IO16(0x130) = (u16)(~keys & 0x3FF);

    Port_GameFrame(sMainLoopHalt);
    HostFlash_Sync();
    sFrameCounter++;
}

static uint64_t sProfStart[PORT_PROF_COUNT], sProfSum[PORT_PROF_COUNT], sProfMax[PORT_PROF_COUNT];
static unsigned sProfCount[PORT_PROF_COUNT];

void Port_ProfileBegin(int what)
{
    sProfStart[what] = Platform_TimeUs();
}

void Port_ProfileEnd(int what)
{
    uint64_t t = Platform_TimeUs() - sProfStart[what];
    sProfSum[what] += t;
    if (t > sProfMax[what])
        sProfMax[what] = t;
    sProfCount[what]++;
}

unsigned Port_ProfileAverageUs(int what)
{
    unsigned avg = sProfCount[what] ? (unsigned)(sProfSum[what] / sProfCount[what]) : 0;
    sProfSum[what] = sProfMax[what] = 0;
    sProfCount[what] = 0;
    return avg;
}

uint32_t Port_FrameCounter(void)
{
    return sFrameCounter;
}

/* Settings (settings.ini in the data folder). */
void Port_ConfigLoad(void)
{
    static char text[4096];
    long n;
    Port_ConfigDefaults();
    n = Platform_ReadFile("settings.ini", text, sizeof(text) - 1);
    if (n > 0) {
        text[n] = 0;
        Port_ConfigParse(text);
        Port_ConfigMigrate();
    }
    gPortViewMode = gPortConfig.viewMode;
}

void Port_ConfigSave(void)
{
    static char text[4096];
    int n = Port_ConfigWrite(text, sizeof(text));
    gPortViewMode = gPortConfig.viewMode;
    if (Platform_WriteFile("settings.ini", text, (size_t)n) != 0)
        Platform_Log("config: cannot write settings.ini\n");
}

void Port_Init(void)
{
    Port_SaveInitData();
    Port_ConfigLoad();
    HostAudio_Init();
    HostFlash_Init();
    IO16(0x130) = 0x3FF;
}

void Port_RunGame(void)
{
    for (;;) {
        sResetRequested = 0;
        HostDma_Reset();
        memset(gHostIo, 0, sizeof(gHostIo));
        IO16(0x130) = 0x3FF;
        /* AgbMain never returns on the GBA (it ends in Hang); a soft reset
         * from the game restarts it. */
        AgbMain();
    }
}

int Port_ResetRequested(void)
{
    return sResetRequested;
}
