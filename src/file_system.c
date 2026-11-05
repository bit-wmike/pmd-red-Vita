#include "global.h"
#include "globaldata.h"
#include "decompress_sir.h"
#include "file_system.h"

static EWRAM_DATA u32 sFileSystemUnk = { FILESYSTEM_UNK_0 };
EWRAM_DATA OpenedFile gFileCache[64] = { 0 };

static EWRAM_INIT u32 sFileCacheCursorPosition = { 0 };

#ifdef PORT
/* Port: a file that is not in the (other-version) ROM data: the game gets
 * an empty file instead of a null pointer, and the log names it. */
void Platform_Log(const char *fmt, ...);
static OpenedFile *Port_MissingFile(const u8 *filename)
{
    static const u8 sEmpty[0x20000];
    static File sFile;
    static OpenedFile sOpened;
    Platform_Log("file: not found in the game data: %s\n", filename);
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

    magic = AreStringsDifferent(arc->magic, sPksDir0);

    magicFound = 0;

    if (!(bool8)magic)
        magicFound = 1;

    if (!magicFound) {
#ifdef PORT
        return Port_MissingFile(filename);
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
        return Port_MissingFile(filename);
#else
        return NULL;
#endif
    }

    cursor = sFileCacheCursorPosition;

    for (i = 0; i < 64; i++) {
        cursor++;
        if (cursor > 63)
            cursor = 0;

        if (!gFileCache[cursor].file) {
            gFileCache[cursor].file = file;
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
