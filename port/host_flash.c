/*
 * The cartridge's 1 Mbit flash chip (FLASH1M, 32 sectors of 4 KiB), replaced
 * by a host buffer that the platform stores as a 128 KiB file compatible
 * with GBA emulators. The game only uses src/flash.c's sector functions, so
 * the chip-specific drivers (agb_flash*.c) are not compiled.
 */
#include <string.h>

#include "port/port.h"
#include "gba/types.h"
#include "gba/flash.h"

#define SECTOR_SIZE 0x1000
#define SECTOR_COUNT (PORT_FLASH_SIZE / SECTOR_SIZE)

static int sDirty;
static int sLoaded;

static u16 ProgramSector(u16 sectorNum, u8 *src)
{
    if (sectorNum >= SECTOR_COUNT)
        return 0x80FF;
    memcpy(gHostFlash + sectorNum * SECTOR_SIZE, src, SECTOR_SIZE);
    sDirty = 1;
    return 0;
}

static u16 EraseChip(void)
{
    memset(gHostFlash, 0xFF, PORT_FLASH_SIZE);
    sDirty = 1;
    return 0;
}

static u16 EraseSector(u16 sectorNum)
{
    if (sectorNum >= SECTOR_COUNT)
        return 0x80FF;
    memset(gHostFlash + sectorNum * SECTOR_SIZE, 0xFF, SECTOR_SIZE);
    sDirty = 1;
    return 0;
}

u16 (*ProgramFlashSector)(u16, u8 *) = ProgramSector;
u16 (*EraseFlashChip)(void) = EraseChip;
u16 (*EraseFlashSector)(u16) = EraseSector;

u16 IdentifyFlash(void)
{
    return 0; /* chip recognised */
}

u16 SetFlashTimerIntr(u8 timerNum, FlashIntrFunc *func)
{
    (void)timerNum; (void)func;
    return 0;
}

void ReadFlash(u16 sectorNum, u32 offset, u8 *dest, u32 size)
{
    u32 start = sectorNum * SECTOR_SIZE + offset;
    if (start + size > PORT_FLASH_SIZE)
        return;
    memcpy(dest, gHostFlash + start, size);
}

u32 VerifyFlashSectorNBytes(u16 sectorNum, u8 *src, u32 n)
{
    if (sectorNum >= SECTOR_COUNT || n > SECTOR_SIZE)
        return 1;
    return memcmp(gHostFlash + sectorNum * SECTOR_SIZE, src, n) != 0;
}

u32 VerifyFlashSector(u16 sectorNum, u8 *src)
{
    return VerifyFlashSectorNBytes(sectorNum, src, SECTOR_SIZE);
}

u32 ProgramFlashSectorAndVerify(u16 sectorNum, u8 *src)
{
    if (ProgramSector(sectorNum, src))
        return 1;
    return VerifyFlashSector(sectorNum, src);
}

u32 ProgramFlashSectorAndVerifyNBytes(u16 sectorNum, u8 *src, u32 n)
{
    if (sectorNum >= SECTOR_COUNT || n > SECTOR_SIZE)
        return 1;
    memcpy(gHostFlash + sectorNum * SECTOR_SIZE, src, n);
    sDirty = 1;
    return 0;
}

void HostFlash_Init(void)
{
    if (sLoaded)
        return;
    sLoaded = 1;
    if (Platform_LoadFlash(gHostFlash, PORT_FLASH_SIZE) == 0)
        memset(gHostFlash, 0xFF, PORT_FLASH_SIZE);
    sDirty = 0;
}

/* Called once per frame: writes the file when the game has saved. */
void HostFlash_Sync(void)
{
    if (!sDirty)
        return;
    sDirty = 0;
    Platform_SaveFlash(gHostFlash, PORT_FLASH_SIZE);
}
