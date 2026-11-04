/*
 * GBA memory regions for the host / PS Vita port.
 *
 * The game addresses I/O registers, palette RAM, VRAM, OAM and a few fixed
 * IWRAM words through include/gba/*.h; in the port those bases point here.
 * The game's own variables (EWRAM_DATA, IWRAM_DATA, EWRAM_INIT, ...) are
 * ordinary host variables in the sections the decomp names; the linker
 * provides __start_/__stop_ symbols for them.
 */
#include <stdlib.h>
#include <string.h>

#include "port/port.h"

#define ALIGN64 __attribute__((aligned(64)))

unsigned char gHostEwram[0x40000] ALIGN64;
unsigned char gHostIwram[0x8000] ALIGN64;
unsigned char gHostIo[0x400] ALIGN64;
unsigned char gHostPalram[0x400] ALIGN64;
unsigned char gHostVram[0x18000] ALIGN64;
unsigned char gHostOam[0x400] ALIGN64;
unsigned char gHostFlash[0x20000] ALIGN64;

unsigned char *gHostRom;
size_t gHostRomSize;

/* Linker-defined values the sound library reads as addresses (ld_script). */
__asm__(
    ".globl gNumMusicPlayers\n.set gNumMusicPlayers, 8\n"
    ".globl gMaxLines\n.set gMaxLines, 0\n"
    /* IWRAM word the game keeps at 0x3004000 + 0x2090 (sub_80001E0). */
    ".globl gUnknown_3004000\n.set gUnknown_3004000, gHostIwram + 0x2090\n"
);

/* The GBA interrupt dispatcher that the game copies to EWRAM: nothing to run. */
uint32_t IntrMain[0x48];

extern unsigned char __start_ewram_data[], __stop_ewram_data[];
extern unsigned char __start_ewram_lib[], __stop_ewram_lib[];
extern unsigned char __start_iwram_data[], __stop_iwram_data[];
extern unsigned char __start_ewram_init[], __stop_ewram_init[];
extern unsigned char __start_iwram_init[], __stop_iwram_init[];

/* The game's linker symbols; only their difference matters (main.c). */
unsigned char ewram_start[1], ewram_end[1], ewram_init_start[1], ewram_init_end[1];
unsigned char iwram_start[1], iwram_end[1], iwram_init_start[1], iwram_init_end[1];
const unsigned char EWRAM_INIT_ROM_START[1], IWRAM_INIT_ROM_START[1];

static unsigned char *sEwramInitCopy, *sIwramInitCopy;

/* Remembers the initial values of the initialised sections (called once,
 * before the game runs). */
void Port_SaveInitData(void)
{
    size_t n = (size_t)(__stop_ewram_init - __start_ewram_init);
    sEwramInitCopy = (unsigned char *)malloc(n ? n : 1);
    memcpy(sEwramInitCopy, __start_ewram_init, n);
    n = (size_t)(__stop_iwram_init - __start_iwram_init);
    sIwramInitCopy = (unsigned char *)malloc(n ? n : 1);
    memcpy(sIwramInitCopy, __start_iwram_init, n);
}

/* What the GBA start-up code does: clears the RAM sections and copies the
 * initialised ones from the ROM. */
void Port_ResetGameRam(void)
{
    if (!sEwramInitCopy)
        Port_SaveInitData();
    memset(__start_ewram_data, 0, (size_t)(__stop_ewram_data - __start_ewram_data));
    memset(__start_ewram_lib, 0, (size_t)(__stop_ewram_lib - __start_ewram_lib));
    memset(__start_iwram_data, 0, (size_t)(__stop_iwram_data - __start_iwram_data));
    memcpy(__start_ewram_init, sEwramInitCopy, (size_t)(__stop_ewram_init - __start_ewram_init));
    memcpy(__start_iwram_init, sIwramInitCopy, (size_t)(__stop_iwram_init - __start_iwram_init));
    memset(gHostEwram, 0, sizeof(gHostEwram));
    memset(gHostIwram, 0, sizeof(gHostIwram));
}

size_t Port_GameRamRegions(unsigned char **ptr, size_t *size, size_t max)
{
    size_t n = 0;
    if (max < 11)
        return 0;
    ptr[n] = gHostEwram; size[n++] = sizeof(gHostEwram);
    ptr[n] = gHostIwram; size[n++] = sizeof(gHostIwram);
    ptr[n] = gHostIo; size[n++] = sizeof(gHostIo);
    ptr[n] = gHostPalram; size[n++] = sizeof(gHostPalram);
    ptr[n] = gHostVram; size[n++] = sizeof(gHostVram);
    ptr[n] = gHostOam; size[n++] = sizeof(gHostOam);
    ptr[n] = __start_ewram_data; size[n++] = (size_t)(__stop_ewram_data - __start_ewram_data);
    ptr[n] = __start_ewram_lib; size[n++] = (size_t)(__stop_ewram_lib - __start_ewram_lib);
    ptr[n] = __start_iwram_data; size[n++] = (size_t)(__stop_iwram_data - __start_iwram_data);
    ptr[n] = __start_ewram_init; size[n++] = (size_t)(__stop_ewram_init - __start_ewram_init);
    ptr[n] = __start_iwram_init; size[n++] = (size_t)(__stop_iwram_init - __start_iwram_init);
    return n;
}
