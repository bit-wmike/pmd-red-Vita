#include "global.h"
#ifdef PORT
#include "../port/port_game.h"
#endif
#include <stdlib.h>
#include "globaldata.h"
#include "decompress_sir.h"
#include "file_system.h"

static EWRAM_DATA u32 sFileSystemUnk = { FILESYSTEM_UNK_0 };
EWRAM_DATA OpenedFile gFileCache[64] = { 0 };

static EWRAM_INIT u32 sFileCacheCursorPosition = { 0 };

#ifdef PORT
#include "port/port_assets.h"
#include "port/port_config.h"
#include "port/port_text.h"
void Platform_Log(const char *fmt, ...);
extern unsigned char *gHostRom;
extern size_t gHostRomSize;

/* Port: the game's file as the port's data block (from the ROM of the
 * version this port is made from, matched by content), or, when that block
 * is not in the user's ROM (another version: fonts, texts), the file of the
 * same name straight out of the user's ROM's own archive. The latter is only
 * safe for files without pointers inside; a SIRO header's data pointer is
 * translated. */
static File sPortFiles[64];
static const File *Port_ChooseFile(const File *file, s32 cursor)
{
    unsigned holes = 0, contentHoles = 0;
    int state = Port_AssetStateAt(file->data, &holes);
    int contentState = state;
    const unsigned char *rom;
    const u8 *d = (const u8 *)file->data;

    /* a SIRO header is a tiny block of its own: what matters is its content */
    /* holes: pointer slots only (large holes are baserom.gba data such
     * as compressed files: lvmp001 was "translated" and crashed the game) */
    holes = Port_AssetPointersFrom(d);
    if (d[0] == 'S' && d[1] == 'I' && d[2] == 'R' && d[3] == 'O') {
        contentState = Port_AssetStateAt(((const SiroArchive *)d)->data, &contentHoles);
        contentHoles = Port_AssetPointersFrom(((const SiroArchive *)d)->data);
    }
#ifndef __vita__
    if (getenv("PMD_FILE_DEBUG"))
        Platform_Log("file: open %s: header state %d, content state %d\n", file->name, state, contentState);
#endif
    if (contentState != -1 && contentState != 2 && state != -1 && state != 2)
        return file; /* identical in the user's ROM, or compiled data of the port */
    rom = Port_RomFile(file->name);
    if (rom == NULL) {
        Platform_Log("file: %s is not in the user's ROM; using the port's copy (state %d)\n", file->name, state);
        return file;
    }
    /* pointers inside (charmaps, parameter tables; the port's copy tells
     * whether the file has any): translated by their look, as the GBA
     * format keeps absolute ROM addresses. A SIRO header alone counts as
     * one pointer. */
    if (contentHoles > 0 || holes > ((d[0] == 'S' && d[1] == 'I' && d[2] == 'R' && d[3] == 'O') ? 1u : 0u)) {
        unsigned filled = 0, size = Port_RomFileSize(file->name);
        Port_RelocateRomFile(rom, size);
        if (contentHoles > 0 && rom[0] == 'S' && rom[1] == 'I' && rom[2] == 'R' && rom[3] == 'O') {
            /* the content lies behind the header in both; the port's copy
             * may keep other data between (the US item table does) */
            const unsigned char *portContent = (const unsigned char *)((const SiroArchive *)d)->data;
            unsigned char *romContent = (unsigned char *)((const SiroArchive *)rom)->data;
            if (romContent >= rom && romContent < rom + size)
                filled = Port_FillPointerFields(portContent, romContent, size - (unsigned)(romContent - rom));
        } else {
            filled = Port_FillPointerFields(d, (unsigned char *)rom, size);
        }
        if (filled)
            Platform_Log("file: %s: %u empty pointer fields filled from the port's copy\n", file->name, filled);
    } else if (rom[0] == 'S' && rom[1] == 'I' && rom[2] == 'R' && rom[3] == 'O')
        Port_RelocateRomFile(rom, 16); /* just the header */
    Platform_Log("file: %s taken from the user's ROM (state %d, %u bytes, pointers in the port's copy %u/%u)\n",
                 file->name, state, Port_RomFileSize(file->name), holes, contentHoles);
    sPortFiles[cursor].name = file->name;
    sPortFiles[cursor].data = rom;
    return &sPortFiles[cursor];
}

/* Port: a file that is not in the (other-version) ROM data: the game gets
 * an empty file instead of a null pointer, and the log names it. */
/* The European ROM keeps the title screen (S02), the health warning (S05)
 * and S06 as ground maps per language: S02_GER, S02_GERc, S02_GERm ... The
 * file of the chosen language replaces the English one when the ROM has
 * it. Palettes and maps are plain data, tilesets are unpacked. */
static const File *Port_VariantFile(const File *file, s32 cursor)
{
    static const char *const sLangTag[PORT_LANG_COUNT] = { "", "_GER", "_FRE", "_ITA", "_SPA" };
    static char sNames[64][16];
    const char *name = file->name;
    int lang = gPortConfig.language;
    const unsigned char *rom;
    size_t n = strlen(name);
    if (lang <= PORT_LANG_EN || lang >= PORT_LANG_COUNT || !Port_TextMultilingual())
        return Port_ChooseFile(file, cursor);
    if (!(n == 3 || n == 4) || name[0] != 'S' || name[1] < '0' || name[1] > '9' || name[2] < '0' || name[2] > '9' ||
        (n == 4 && name[3] != 'c' && name[3] != 'm'))
        return Port_ChooseFile(file, cursor);
    snprintf(sNames[cursor], sizeof(sNames[cursor]), "%.3s%s%s", name, sLangTag[lang], name + 3);
    rom = (n == 4 && name[3] == 'c') ? Port_RomTilesetFile(sNames[cursor]) : Port_RomFile(sNames[cursor]);
    if (rom == NULL)
        return Port_ChooseFile(file, cursor);
    Platform_Log("file: %s taken from the user's ROM in place of %s\n", sNames[cursor], name);
    sPortFiles[cursor].name = sNames[cursor];
    sPortFiles[cursor].data = rom;
    return &sPortFiles[cursor];
}

static OpenedFile *Port_MissingFile(const u8 *filename, const FileArchive *arc, int why)
{
    static const u8 sEmpty[0x20000];
    static File sFile;
    static OpenedFile sOpened;
    Platform_Log("file: not found in the game data: %s (%s; archive magic '%.8s', %d entries, first '%s', last '%s')\n",
                 filename, why ? "name" : "magic", arc->magic, arc->count,
                 arc->count > 0 ? arc->entries[0].name : "", arc->count > 0 ? arc->entries[arc->count - 1].name : "");
    sFile.name = (char *)filename;
    sFile.data = sEmpty;
    sOpened.file = &sFile;
    sOpened.data = sEmpty;
    return &sOpened;
}
#endif

void InitFileSystem(void)
{
    s32 i;

    for (i = 0; i < 64; i++) {
        gFileCache[i].file = NULL;
        gFileCache[i].data = NULL;
    }

    sFileCacheCursorPosition = 0;
    sFileSystemUnk = FILESYSTEM_UNK_1;
}

u32 SetFileSystemUnk(u32 value)
{
    u32 oldValue = sFileSystemUnk;
    sFileSystemUnk = value;
    return oldValue;
}

OpenedFile *OpenFile(const u8 *filename, const FileArchive *arc)
{
    u8 buffer[300];
    s32 left, right;
    s32 cursor;
    s32 i;
    u32 magic = 0;
    bool32 magicFound;
    const File *entries;
    const File *file;
#ifdef PORT
    Port_Crumb("open %s", filename);
#endif

    magic = AreStringsDifferent(arc->magic, sPksDir0);

    magicFound = 0;

    if (!(bool8)magic)
        magicFound = 1;

    if (!magicFound) {
#ifdef PORT
        return Port_MissingFile(filename, arc, 0);
#else
        return NULL;
#endif
    }

    entries = arc->entries;
    left = 0;
    right = arc->count - 1;

    while (left < right) {
        s32 mid = (left + right) / 2;
        s32 result = strcmp(entries[mid].name, filename);
        if (result == 0) {
            left = mid;
            break;
        }

        if (result < 0)
            left = mid + 1;
        else
            right = mid;
    }

    file = &entries[left];

    if (strcmp(file->name, filename)) {
        sprintf(buffer, _("not find file [%s]\n"), filename);
#ifdef PORT
        Platform_Log("file: not in the archive: %s\n", filename);
#endif
        return NULL;
    }

    cursor = sFileCacheCursorPosition;

    for (i = 0; i < 64; i++) {
        cursor++;
        if (cursor > 63)
            cursor = 0;

        if (!gFileCache[cursor].file) {
#ifdef PORT
            gFileCache[cursor].file = Port_VariantFile(file, cursor);
#else
            gFileCache[cursor].file = file;
#endif
            gFileCache[cursor].data = NULL;
            return &gFileCache[cursor];
        }
    }

    return NULL;
}

static const u8 *_GetFileDataPtr(OpenedFile *openedFile)
{
    openedFile->data = openedFile->file->data;
    return openedFile->data;
}

const u8 *GetFileDataPtr(OpenedFile *openedFile, s32 unused)
{
    _GetFileDataPtr(openedFile);
    return GetSiroPtr(openedFile);
}

OpenedFile *OpenFileAndGetFileDataPtr(const u8 *filename, const FileArchive *arc)
{
    OpenedFile *openedFile = OpenFile(filename, arc);
    if (openedFile)
        GetFileDataPtr(openedFile, 0);
    return openedFile;
}

OpenedFile *Call_OpenFileAndGetFileDataPtr(const u8 *filename, const FileArchive *arc)
{
    return OpenFileAndGetFileDataPtr(filename, arc);
}

void CloseFile(OpenedFile *openedFile)
{
    s32 i;

    for (i = 0; i < 64; i++) {
        if (&gFileCache[i] == openedFile) {
            gFileCache[i].file = NULL;
            gFileCache[i].data = NULL;
            sFileCacheCursorPosition = i;
            return;
        }
    }
}
