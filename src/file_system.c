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
    if (d[0] == 'S' && d[1] == 'I' && d[2] == 'R' && d[3] == 'O')
        contentState = Port_AssetStateAt(((const SiroArchive *)d)->data, &contentHoles);
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
    if (contentHoles > 0 || holes > ((d[0] == 'S' && d[1] == 'I' && d[2] == 'R' && d[3] == 'O') ? 1u : 0u))
        Port_RelocateRomFile(rom, Port_RomFileSize(file->name));
    else if (rom[0] == 'S' && rom[1] == 'I' && rom[2] == 'R' && rom[3] == 'O')
        Port_RelocateRomFile(rom, 16); /* just the header */
    Platform_Log("file: %s taken from the user's ROM (state %d, %u bytes, pointers in the port's copy %u/%u)\n",
                 file->name, state, Port_RomFileSize(file->name), holes, contentHoles);
    sPortFiles[cursor].name = file->name;
    sPortFiles[cursor].data = rom;
    return &sPortFiles[cursor];
}

/* Port: a file that is not in the (other-version) ROM data: the game gets
 * an empty file instead of a null pointer, and the log names it. */
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
            gFileCache[cursor].file = Port_ChooseFile(file, cursor);
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
