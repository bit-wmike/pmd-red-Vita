/*
 * Host / PS Vita port of Pokémon Mystery Dungeon: Red Rescue Team - shared
 * port interface.
 *
 * The game code is compiled unchanged (apart from a few #ifdef PORT spots);
 * this layer stands in for the GBA hardware: memory regions, BIOS calls, DMA,
 * interrupts, the PPU (software renderer in port/ppu), the flash save chip
 * and the sound hardware. A platform (platform/host, platform/vita) supplies
 * video, audio, input and files through the Platform* functions below.
 */
#ifndef PORT_PORT_H
#define PORT_PORT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

extern unsigned char gHostEwram[0x40000];
extern unsigned char gHostIwram[0x8000];
extern unsigned char gHostIo[0x400];
extern unsigned char gHostPalram[0x400];
extern unsigned char gHostVram[0x18000];
extern unsigned char gHostOam[0x400];
extern unsigned char gHostFlash[0x20000];

/* The user's ROM, kept in memory (pointers inside unextracted data). */
extern unsigned char *gHostRom;
extern size_t gHostRomSize;

#define PORT_GBA_W 240
#define PORT_GBA_H 160
#define PORT_ROM_SIZE (32u * 1024u * 1024u)
#define PORT_FLASH_SIZE 0x20000

/* ---- game data (port/host_assets.c) ---- */
/* True if the image is the ROM this build was made for (US 1.0). */
int Port_RomMatches(const void *rom, size_t size);
/* 'E' (USA), 'P' (Europe), 'J' (Japan) from the cartridge header, 0 if the
 * image is not a Red Rescue Team ROM of the right size. */
int Port_RomVersion(const void *rom, size_t size);
/* Takes ownership of the ROM buffer; copies the game's data out of it and
 * resolves the pointers. Returns 0 on success. */
int Port_LoadRom(unsigned char *rom, size_t size);
void Port_Sha1(const void *data, size_t len, unsigned char out[20]);
/* Translates a GBA ROM address found inside unextracted data. */
const void *Port_RomPtr(const void *p);

/* ---- core (port/host_core.c) ---- */
void Port_Init(void);
/* Runs the game forever (AgbMain, restarted on soft reset). */
void Port_RunGame(void);
uint32_t Port_FrameCounter(void);
void HostSyscall(int num);
void *HostVcountPtr(void);

/* ---- memory (port/host_mem.c) ---- */
void Port_ResetGameRam(void);
void Port_SaveInitData(void);
/* Game RAM sections for the quick save: returns the number of regions. */
size_t Port_GameRamRegions(unsigned char **ptr, size_t *size, size_t max);

/* HBlank DMA (port/host_dma.c) */
void HostDmaSet(int channel, const void *src, void *dst, unsigned int control);
void HostDma_HBlank(void);
void HostDma_Reset(void);

/* ---- flash save (port/host_flash.c) ---- */
void HostFlash_Init(void);
void HostFlash_Sync(void);

/* ---- audio (port/host_audio.c) ---- */
#define PORT_AUDIO_RATE 48000
void HostAudio_Init(void);
/* Ends the frame: takes the PCM mixed by the sound engine, adds the PSG
 * channels, resamples and hands the samples to the platform. */
void HostAudio_EndFrame(void);

/* ---- profiling (host_core.c) ---- */
enum { PORT_PROF_RENDER, PORT_PROF_COUNT };
void Port_ProfileBegin(int what);
void Port_ProfileEnd(int what);
unsigned Port_ProfileAverageUs(int what);
uint64_t Platform_TimeUs(void);

/* ---- platform interface ---- */
/* Shows a finished frame (ABGR8888, `pitch` pixels per row), scaled so that
 * `zoomHeight` lines fill the screen height (smaller frames get a border). */
void Platform_Present(const uint32_t *pixels, int width, int height, int pitch, int zoomHeight);
/* Returns pressed GBA keys (KEY_A ... KEY_L bits), polled once per frame. */
uint16_t Platform_PollKeys(void);
/* Receives interleaved stereo s16 samples at PORT_AUDIO_RATE. */
void Platform_SubmitAudio(const int16_t *samples, int frames);
/* Persistent storage for the 128 KiB flash image. Return 0 if nothing saved. */
int Platform_LoadFlash(void *data, size_t size);
void Platform_SaveFlash(const void *data, size_t size);
void Platform_Log(const char *fmt, ...);
/* Small files in the port's data folder (settings, quick save, ROM index). */
long Platform_ReadFile(const char *name, void *data, size_t size);
int Platform_WriteFile(const char *name, const void *data, size_t size);
/* Frame pacing: 0 when the frame should only be computed, not drawn. */
int Platform_PaceAllowsRender(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_PORT_H */
