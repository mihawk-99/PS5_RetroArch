#include "SmbImages.h"
#include "SmbMedia.h"
#include <FreeImage.h>
#include <cmath>
#include <cstring>
#include <cstdio>

namespace {
SmbImages::Cache& cache()
{
    static SmbImages::Cache value([](const std::string& path) -> SmbImages::Cache::Ptr {
        const auto start = std::chrono::steady_clock::now();
        size_t size = 0;
        const auto bytes = SmbMedia::readImage(path, size);
        if (!bytes || !size) return {};
        auto image = SmbImages::decode(bytes.get(), size);
        if (!image) return {};
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        std::printf("[ES-DE SMB] image decoded %zu bytes %zux%zu in %lld ms: %s\n",
                    size, image->width, image->height, static_cast<long long>(ms), path.c_str());
        return image;
    });
    return value;
}
// Main-thread-only frame budgets: uploads never run in the decoding worker.
unsigned installs = 0, uploads = 0;
bool windowUsed = false, hadWindow = false;
}
SmbImages::Cache::Ptr SmbImages::decode(const unsigned char* data, size_t size)
{
    if (!data || !size || size > 64 * 1024 * 1024) return {};
    std::unique_ptr<FIMEMORY, decltype(&FreeImage_CloseMemory)> memory(
        FreeImage_OpenMemory(const_cast<BYTE*>(data), static_cast<DWORD>(size)), FreeImage_CloseMemory);
    if (!memory) return {};
    const auto format = FreeImage_GetFileTypeFromMemory(memory.get());
    if (format == FIF_UNKNOWN || !FreeImage_FIFSupportsReading(format)) return {};
    std::unique_ptr<FIBITMAP, decltype(&FreeImage_Unload)> bitmap(
        FreeImage_LoadFromMemory(format, memory.get()), FreeImage_Unload);
    if (!bitmap) return {};
    if (FreeImage_GetBPP(bitmap.get()) != 32) bitmap.reset(FreeImage_ConvertTo32Bits(bitmap.get()));
    if (!bitmap) return {};
    auto image = std::make_shared<Image>();
    image->sourceWidth = FreeImage_GetWidth(bitmap.get());
    image->sourceHeight = FreeImage_GetHeight(bitmap.get());
    if (!image->sourceWidth || !image->sourceHeight) return {};
    // Keep files intact. Oversized scans need not become 4K GPU textures in a 1080p UI.
    const auto largest = std::max(image->sourceWidth, image->sourceHeight);
    if (largest > 2048) {
        const double scale = 2048.0 / largest;
        auto* scaled = FreeImage_Rescale(bitmap.get(),
            std::max(1, static_cast<int>(std::round(image->sourceWidth * scale))),
            std::max(1, static_cast<int>(std::round(image->sourceHeight * scale))), FILTER_BICUBIC);
        if (!scaled) return {};
        bitmap.reset(scaled);
    }
    FreeImage_PreMultiplyWithAlpha(bitmap.get());
    image->width = FreeImage_GetWidth(bitmap.get()); image->height = FreeImage_GetHeight(bitmap.get());
    image->pixels.resize(image->width * image->height * 4);
    for (size_t y = 0; y < image->height; ++y)
        std::memcpy(image->pixels.data() + y * image->width * 4,
                    FreeImage_GetScanLine(bitmap.get(), static_cast<int>(y)), image->width * 4);
    return image;
}
SmbImages::Cache::Ptr SmbImages::get(const std::string& path, bool foreground) { return cache().get(path, foreground ? 0 : 1); }
void SmbImages::window(const std::vector<std::string>& paths, size_t visible) {
    windowUsed = true; hadWindow = !paths.empty(); cache().window(paths, visible);
}
void SmbImages::beginFrame() {
    if (!windowUsed && hadWindow) { cache().window({}, 0); hadWindow = false; }
    windowUsed = false; installs = 2; uploads = 1;
}
bool SmbImages::takeInstall() { if (!installs) return false; --installs; return true; }
bool SmbImages::takeUpload() { if (!uploads) return false; --uploads; return true; }
void SmbImages::stop() { cache().stop(); }
