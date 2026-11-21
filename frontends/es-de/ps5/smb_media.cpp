// SMB images are decoded from RAM; video uses seekable AVIO, no on-disk cache.
#include "SmbMedia.h"
#include "ps5_smb_vfs.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <new>
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/mem.h>
}

namespace {
struct Stream { void* file; int64_t size; };
int readPacket(void* opaque, uint8_t* buffer, int length) {
    auto* stream = static_cast<Stream*>(opaque);
    const int64_t got = ps5_smb_read(stream->file, buffer, length);
    return got > 0 ? static_cast<int>(got) : got == 0 ? AVERROR_EOF : AVERROR(EIO);
}
int64_t seek(void* opaque, int64_t offset, int whence) {
    auto* stream = static_cast<Stream*>(opaque);
    if (whence == AVSEEK_SIZE) return stream->size;
    whence &= ~AVSEEK_FORCE;
    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) return AVERROR(EINVAL);
    if (ps5_smb_seek(stream->file, offset, whence) < 0) return AVERROR(EIO);
    return ps5_smb_tell(stream->file);
}
void release(AVIOContext* io) {
    if (!io) return;
    auto* stream = static_cast<Stream*>(io->opaque);
    if (stream) { ps5_smb_close(stream->file); delete stream; }
    av_freep(&io->buffer);
    avio_context_free(&io);
}
}

std::shared_ptr<unsigned char> SmbMedia::readImage(const std::string& path, size_t& size)
{
    size = 0;
    int64_t length = 0;
    void* file = ps5_smb_open(path.c_str(), 1, &length);
    if (!file) return {};
    // Bound malformed/oversized image allocations without a persistent cache.
    if (length <= 0 || length > 64 * 1024 * 1024) { ps5_smb_close(file); return {}; }
    std::shared_ptr<unsigned char> data(new (std::nothrow) unsigned char[length],
                                      [](unsigned char* p) { delete[] p; });
    const auto got = data ? ps5_smb_read(file, data.get(), length) : -1;
    ps5_smb_close(file);
    if (got != length) return {};
    size = static_cast<size_t>(length);
    std::fprintf(stderr, "[ES-DE SMB] image read %zu bytes: %s\n", size, path.c_str());
    return data;
}

int SmbMedia::openVideo(AVFormatContext** output, const char* url,
                       const AVInputFormat* format, AVDictionary** options)
{
    if (std::strncmp(url, "smb://", 6)) return avformat_open_input(output, url, format, options);
    int64_t size = 0;
    void* file = ps5_smb_open(url, 1, &size);
    if (!file) return AVERROR(EIO);
    auto* stream = new (std::nothrow) Stream{file, size};
    auto* buffer = static_cast<unsigned char*>(av_malloc(128 * 1024));
    auto* context = avformat_alloc_context();
    if (!stream || !buffer || !context) {
        ps5_smb_close(file); delete stream; av_free(buffer); avformat_free_context(context);
        return AVERROR(ENOMEM);
    }
    auto* io = avio_alloc_context(buffer, 128 * 1024, 0, stream, readPacket, nullptr, seek);
    if (!io) {
        ps5_smb_close(file); delete stream; av_free(buffer); avformat_free_context(context);
        return AVERROR(ENOMEM);
    }
    context->pb = io;
    context->flags |= AVFMT_FLAG_CUSTOM_IO;
    // An SMB preview is a single self-contained media file. No nested external URLs.
    context->protocol_whitelist = av_strdup("file");
    const int result = avformat_open_input(&context, url, format, options);
    if (result < 0) { avformat_free_context(context); release(io); *output = nullptr; return result; }
    *output = context;
    std::fprintf(stderr, "[ES-DE SMB] video stream opened %lld bytes: %s\n",
                 static_cast<long long>(size), url);
    return 0;
}

void SmbMedia::closeVideo(AVFormatContext** context)
{
    AVIOContext* io = context && *context && (*context)->pb &&
                     (*context)->pb->read_packet == readPacket ? (*context)->pb : nullptr;
    avformat_close_input(context);
    release(io);
}
