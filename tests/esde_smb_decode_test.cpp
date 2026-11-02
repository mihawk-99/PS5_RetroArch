// Production decoder and frame budgets; transport replaced with a temporary fixture.
#include "SmbImages.h"
#include "SmbMedia.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>

static std::vector<unsigned char> read(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}
std::shared_ptr<unsigned char> SmbMedia::readImage(const std::string& path, size_t& size)
{
    const auto bytes = read(path);
    size = bytes.size();
    if (!size) return {};
    std::shared_ptr<unsigned char> result(new unsigned char[size], std::default_delete<unsigned char[]>());
    std::memcpy(result.get(), bytes.data(), size);
    return result;
}
int main(int argc, char** argv)
{
    assert(argc == 4);
    const auto bytes = read(argv[1]);
    const auto direct = SmbImages::decode(bytes.data(), bytes.size());
    assert(direct && direct->width == 2 && direct->height == 2);
    // Bottom-up BGRA, with premultiplied alpha.
    const std::vector<unsigned char> expected = {
        255,0,0,255, 0,0,0,0, 0,0,255,255, 0,128,0,128};
    assert(direct->pixels == expected);
    const auto large = read(argv[2]);
    const auto resized = SmbImages::decode(large.data(), large.size());
    assert(resized && resized->sourceWidth == 4096 && resized->sourceHeight == 8);
    assert(resized->width == 2048 && resized->height == 4);
    const auto webp = read(argv[3]);
    const auto other = SmbImages::decode(webp.data(), webp.size());
    assert(other && other->width == 3 && other->height == 2);
    assert(!SmbImages::decode(nullptr, 0));
    assert(!SmbImages::decode(reinterpret_cast<const unsigned char*>("bad"), 3));
    SmbImages::beginFrame();
    assert(SmbImages::takeInstall() && SmbImages::takeInstall() && !SmbImages::takeInstall());
    assert(SmbImages::takeUpload() && !SmbImages::takeUpload());
    SmbImages::beginFrame();
    assert(SmbImages::takeInstall() && SmbImages::takeUpload());
    auto loaded = SmbImages::get(argv[1]);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!loaded) {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        loaded = SmbImages::get(argv[1]);
    }
    assert(loaded->pixels == expected);
    SmbImages::stop();
}
