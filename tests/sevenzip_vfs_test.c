/* Test the real 7zip file adapter against an injectable, read-only URL backend.
 * The backend is a test double, not an SMB server. No ROMs or credentials needed.
 */
#include <7zFile.h>
#include <streams/file_stream.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct RFILE { FILE *file; };
static const char *fixture;
static const char *url = "smb://fixture/archive.7z";
static int fault;
static unsigned opens;

RFILE *filestream_open(const char *path, unsigned mode, unsigned hints)
{
    RFILE *stream;
    (void)hints;
    ++opens;
    if (fault == 1) { errno = 0; return NULL; }
    if (!strncmp(path, "smb://", 6)) {
        if (strcmp(path, url) || mode != RETRO_VFS_FILE_ACCESS_READ) {
            errno = EACCES;
            return NULL;
        }
        path = fixture;
    }
    stream = malloc(sizeof(*stream));
    assert(stream);
    stream->file = fopen(path, mode == RETRO_VFS_FILE_ACCESS_READ ? "rb" : "wb+");
    if (!stream->file) { free(stream); return NULL; }
    return stream;
}

int filestream_close(RFILE *stream)
{
    int result = fclose(stream->file);
    free(stream);
    return result;
}

int64_t filestream_read(RFILE *stream, void *data, int64_t size)
{
    return fault == 2 ? -1 : (int64_t)fread(data, 1, size, stream->file);
}

int64_t filestream_write(RFILE *stream, const void *data, int64_t size)
{
    return fault == 6 ? -1 : (int64_t)fwrite(data, 1, size, stream->file);
}

int64_t filestream_seek(RFILE *stream, int64_t at, int origin)
{
    if (fault == 3 || fseek(stream->file, at, origin)) return -1;
    return ftell(stream->file);
}

int64_t filestream_tell(RFILE *stream)
{
    return fault == 4 ? -1 : ftell(stream->file);
}

int64_t filestream_get_size(RFILE *stream)
{
    long at = ftell(stream->file), size;
    if (fault == 5) return -1;
    assert(fseek(stream->file, 0, SEEK_END) == 0);
    size = ftell(stream->file);
    assert(fseek(stream->file, at, SEEK_SET) == 0);
    return size;
}

static void read_contract(const char *path)
{
    CSzFile file;
    char data[8];
    size_t size = sizeof(data);
    uint64_t length = 0;
    int64_t at = 2;
    File_Construct(&file);
    assert(InFile_Open(&file, path) == 0);
    assert(File_Seek(&file, &at, SZ_SEEK_SET) == 0 && at == 2);
    assert(File_GetLength(&file, &length) == 0 && length == 8);
    at = 0;
    assert(File_Seek(&file, &at, SZ_SEEK_CUR) == 0 && at == 2);
    assert(File_Read(&file, data, &size) == 0 && size == 6);
    assert(memcmp(data, "cdefgh", 6) == 0);
    size = sizeof(data);
    assert(File_Read(&file, data, &size) == 0 && size == 0);
    at = -2;
    assert(File_Seek(&file, &at, SZ_SEEK_END) == 0 && at == 6);
    assert(File_Close(&file) == 0 && File_Close(&file) == 0);
}

int main(int argc, char **argv)
{
    CSzFile file;
    assert(argc == 3);
    fixture = argv[1];
    read_contract(fixture);
    File_Construct(&file);
#ifdef __PROSPERO__
    read_contract(url);
    assert(opens == 2);
    assert(OutFile_Open(&file, url) != 0);
    fault = 1;
    assert(InFile_Open(&file, url) != 0); /* errno may be unset by a VFS. */
    fault = 0;
    assert(InFile_Open(&file, url) == 0);
    char data[4];
    size_t size = sizeof(data);
    fault = 2;
    assert(File_Read(&file, data, &size) != 0 && size == 0);
    assert(File_Read(&file, data, &size) == 0); /* zero-length request */
    for (fault = 3; fault <= 4; ++fault) {
        int64_t at = 1;
        assert(File_Seek(&file, &at, SZ_SEEK_SET) != 0 && at == 1);
    }
    uint64_t length = 42;
    fault = 5;
    assert(File_GetLength(&file, &length) != 0 && length == 42);
    fault = 0;
    assert(File_Close(&file) == 0);
#else
    assert(InFile_Open(&file, url) != 0);
    assert(opens == 0); /* The non-PS5 build still uses stdio. */
#endif
    assert(OutFile_Open(&file, argv[2]) == 0);
    size_t count = 2;
    assert(File_Write(&file, "ok", &count) == 0 && count == 2);
#ifdef __PROSPERO__
    fault = 6;
    assert(File_Write(&file, "no", &count) != 0 && count == 0);
    fault = 0;
#endif
    assert(File_Close(&file) == 0);
    puts("PASS: file I/O contract");
    return 0;
}
