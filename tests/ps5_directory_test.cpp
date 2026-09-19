/* Clock-free SDK getdents fixture: actual adapter, including malformed records. */
#include <cassert>
#include <string>
#include <vector>
#include "../src/ps5_directory.cpp"

namespace
{
std::vector<std::vector<char>> batches;
size_t batch = 0;
int closed = 0;
std::vector<char> record(uint32_t inode, unsigned type, const std::string &name)
{
    uint16_t length = uint16_t((8 + name.size() + 1 + 3) & ~3u);
    std::vector<char> bytes(length);
    std::memcpy(bytes.data(), &inode, 4);
    std::memcpy(bytes.data() + 4, &length, 2);
    bytes[6] = char(type);
    bytes[7] = char(name.size());
    std::memcpy(bytes.data() + 8, name.c_str(), name.size() + 1);
    return bytes;
}
} // namespace
extern "C" int __wrap_open(const char *path, int flags, ...)
{
    assert(flags == (O_RDONLY | O_DIRECTORY));
    if (std::strcmp(path, "/denied") == 0)
    {
        errno = EACCES;
        return -1;
    }
    return 0; // fd zero is valid and must be closed too.
}
extern "C" int __wrap_close(int fd)
{
    assert(fd == 0);
    ++closed;
    return 0;
}
extern "C" int getdents(int fd, char *buffer, int size)
{
    assert(fd == 0);
    // Model the mounted filesystem that rejects small directory reads.
    if (size < 64 * 1024)
    {
        errno = EINVAL;
        return -1;
    }
    if (batch == batches.size())
        return 0;
    const auto &data = batches[batch++];
    assert(data.size() <= size_t(size));
    std::memcpy(buffer, data.data(), data.size());
    return int(data.size());
}
int main()
{
    assert(ps5_opendir("/denied") == nullptr && errno == EACCES);
    assert(ps5_opendir(nullptr) == nullptr && errno == ENOENT);
    auto first = record(0, DT_REG, "deleted");
    auto folder = record(2, DT_DIR, "cores");
    first.insert(first.end(), folder.begin(), folder.end());
    batches = {first, record(3, DT_REG, "Example ROM.bin")};
    DIR *dir = ps5_opendir("/app0");
    assert(dir);
    auto *entry = ps5_readdir(dir);
    assert(entry && entry->d_type == DT_DIR && std::string(entry->d_name) == "cores");
    entry = ps5_readdir(dir);
    assert(entry && entry->d_type == DT_REG && std::string(entry->d_name) == "Example ROM.bin");
    errno = 0;
    assert(!ps5_readdir(dir) && errno == 0 && !ps5_readdir(dir));
    assert(ps5_closedir(dir) == 0 && closed == 1);
    // Truncated headers, oversized/zero records and missing name terminators fail closed.
    auto oversized = record(1, DT_DIR, "x");
    oversized[4] = char(255);
    auto zero = record(1, DT_DIR, "x");
    zero[4] = 0;
    auto unterminated = record(1, DT_DIR, "x");
    unterminated[9] = 'x';
    for (const auto &bad : {std::vector<char>(7), oversized, zero, unterminated})
    {
        batches = {bad};
        batch = 0;
        dir = ps5_opendir("/app0");
        errno = 0;
        assert(!ps5_readdir(dir) && errno == EIO);
        assert(!ps5_readdir(dir));
        ps5_closedir(dir);
    }
    assert(closed == 5);
    std::puts(
        "ps5_directory: SDK record parsing, multiple batches, EOF, denial and corruption PASS");
}
