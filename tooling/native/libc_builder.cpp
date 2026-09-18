/*
 * ps5-native-app-boilerplate - Native clean-room runtime-module builder.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Deterministically emits the libc-compatible loader companion from tracked
 * semantic manifests without managed code or external libraries.
 */

#include "hash.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

using Bytes = std::vector<std::uint8_t>;

struct ApiSymbol
{
    std::string nid;
    std::uint8_t binding;
    std::uint8_t type;
    std::uint64_t size;
};

struct RuntimeImport
{
    std::string name;
    std::string suffix;
    bool plt;
    bool glob_dat;
};

constexpr std::size_t kProgramHeaderOffset = 0x40;
constexpr std::size_t kProgramHeaderSize = 0x38;
constexpr std::size_t kProgramHeaderCount = 14;
constexpr std::size_t kTextFileOffset = 0x4000;
constexpr std::uint64_t kMarkerAddress = 0xCC000;
constexpr std::size_t kMarkerFileOffset = 0xD0000;
constexpr std::size_t kGotFileOffset = 0x113870;
constexpr std::uint64_t kGotAddress = 0x10F870;
constexpr std::size_t kPreinitFileOffset = 0x113CE0;
constexpr std::size_t kModuleParamFileOffset = 0x113CE8;
constexpr std::size_t kMetadataFileOffset = 0x11B810;
constexpr std::uint64_t kMetadataAddress = 0x117810;
constexpr std::size_t kMetadataSize = 0x2A9F8;
constexpr std::size_t kBuildNoteOffset = 0x2A710;
constexpr std::size_t kDynamicOffset = 0x2A738;
constexpr std::size_t kDynamicCount = 44;
constexpr std::size_t kDataFileSize = 0x3B78;
constexpr std::size_t kDataMemorySize = 0x7808;
constexpr std::uint64_t kDataAddress = 0x110000;
constexpr std::size_t kExportCount = 2566;
constexpr std::size_t kRuntimeFileSize = 0x14629A;
constexpr std::size_t kReadOnlyFileSize = 0x3DF80;
constexpr std::size_t kUnwindHeaderFileOffset = 0x108164;
constexpr std::uint64_t kUnwindHeaderAddress = 0x104164;
constexpr std::size_t kUnwindHeaderSize = 0x5E1C;
constexpr std::size_t kCommentFileOffset = 0x146210;
constexpr std::size_t kCommentSize = 0x58;
constexpr std::size_t kTailNoteFileOffset = 0x146268;
constexpr std::size_t kVersionFileOffset = 0x146280;
constexpr std::uint64_t kFiniAddress = 0xC8010;
constexpr std::uint64_t kInitAddress = 0x100;
constexpr std::uint64_t kThreadDtorsAddress = 0x200;
constexpr std::uint64_t kThreadAtexitCountAddress = 0x210;
constexpr std::uint64_t kThreadAtexitReportAddress = 0x220;
constexpr std::uint64_t kHeapApiAddress = 0x110100;
constexpr std::size_t kHeapApiFileOffset = 0x114100;
constexpr std::size_t kHeapApiSize = 0x48;
constexpr std::size_t kObjectStorageOffset = 0x180;
constexpr std::size_t kGotReservedEntries = 3;
constexpr std::size_t kImportCount = 102;
constexpr std::size_t kPltRelocationCount = 100;
constexpr std::size_t kRelativeRelocationCount = 1790;
constexpr std::size_t kTlsRelocationCount = 3;
constexpr std::size_t kGlobDatRelocationCount = 3;
constexpr std::uint64_t kRelativeAnchorSlotAddress = 0x10C008;
constexpr std::uint64_t kRelativeAnchorAddress = 0x230;
constexpr std::size_t kStringTableSize = 0xA7A3;
constexpr std::size_t kSymbolTableOffset = 0xA7A8;
constexpr std::size_t kJumpRelocationOffset = 0x1A1E0;
constexpr std::size_t kRelaOffset = 0x1AB40;
constexpr std::size_t kHashTableOffset = 0x253A0;

constexpr std::int64_t kDtNull = 0;
constexpr std::int64_t kDtNeeded = 1;
constexpr std::int64_t kDtPltRelSz = 2;
constexpr std::int64_t kDtPltGot = 3;
constexpr std::int64_t kDtHash = 4;
constexpr std::int64_t kDtStrTab = 5;
constexpr std::int64_t kDtSymTab = 6;
constexpr std::int64_t kDtRela = 7;
constexpr std::int64_t kDtRelaSz = 8;
constexpr std::int64_t kDtRelaEnt = 9;
constexpr std::int64_t kDtStrSz = 10;
constexpr std::int64_t kDtSymEnt = 11;
constexpr std::int64_t kDtInit = 12;
constexpr std::int64_t kDtFini = 13;
constexpr std::int64_t kDtSoname = 14;
constexpr std::int64_t kDtPltRel = 20;
constexpr std::int64_t kDtJmpRel = 23;
constexpr std::int64_t kDtInitArray = 25;
constexpr std::int64_t kDtFiniArray = 26;
constexpr std::int64_t kDtInitArraySz = 27;
constexpr std::int64_t kDtFiniArraySz = 28;
constexpr std::int64_t kDtPreInitArray = 32;
constexpr std::int64_t kDtPreInitArraySz = 33;
constexpr std::int64_t kDtRelaCount = 0x6FFFFFF9;
constexpr std::int64_t kDtSceModuleAttr = 0x61000011;
constexpr std::int64_t kDtSceExportLibAttr = 0x61000017;
constexpr std::int64_t kDtSceImportLibAttr = 0x61000019;
constexpr std::int64_t kDtSceHashSz = 0x6100003D;
constexpr std::int64_t kDtSceSymTabSz = 0x6100003F;
constexpr std::int64_t kDtSceOrigFilename = 0x61000041;
constexpr std::int64_t kDtSceModuleInfo = 0x61000043;
constexpr std::int64_t kDtSceNeededModule = 0x61000045;
constexpr std::int64_t kDtSceExportLib = 0x61000047;
constexpr std::int64_t kDtSceImportLib = 0x61000049;

constexpr std::array<std::uint8_t, 16> kNidSuffix{0x51, 0x8D, 0x64, 0xA6, 0x35, 0xDE, 0xD8, 0xC1,
                                                  0xE6, 0xB0, 0x39, 0xB1, 0xC3, 0xE5, 0x52, 0x30};

[[noreturn]] void fail(const std::string &message)
{
    throw std::runtime_error("self-check failed: " + message);
}

void require(bool condition, const std::string &message)
{
    if (!condition)
        fail(message);
}

std::uint16_t read_u16(std::span<const std::uint8_t> data, std::size_t at)
{
    require(at + 2 <= data.size(), "read_u16 bounds");
    return static_cast<std::uint16_t>(data[at]) | static_cast<std::uint16_t>(data[at + 1] << 8);
}

std::uint32_t read_u32(std::span<const std::uint8_t> data, std::size_t at)
{
    require(at + 4 <= data.size(), "read_u32 bounds");
    std::uint32_t value = 0;
    for (int i = 3; i >= 0; --i)
        value = (value << 8) | data[at + i];
    return value;
}

std::uint64_t read_u64(std::span<const std::uint8_t> data, std::size_t at)
{
    require(at + 8 <= data.size(), "read_u64 bounds");
    std::uint64_t value = 0;
    for (int i = 7; i >= 0; --i)
        value = (value << 8) | data[at + i];
    return value;
}

void write_u16(std::span<std::uint8_t> data, std::size_t at, std::uint16_t value)
{
    require(at + 2 <= data.size(), "write_u16 bounds");
    data[at] = static_cast<std::uint8_t>(value);
    data[at + 1] = static_cast<std::uint8_t>(value >> 8);
}

void write_u32(std::span<std::uint8_t> data, std::size_t at, std::uint32_t value)
{
    require(at + 4 <= data.size(), "write_u32 bounds");
    for (std::size_t i = 0; i < 4; ++i)
        data[at + i] = static_cast<std::uint8_t>(value >> (i * 8));
}

void write_u64(std::span<std::uint8_t> data, std::size_t at, std::uint64_t value)
{
    require(at + 8 <= data.size(), "write_u64 bounds");
    for (std::size_t i = 0; i < 8; ++i)
        data[at + i] = static_cast<std::uint8_t>(value >> (i * 8));
}

void write_i64(std::span<std::uint8_t> data, std::size_t at, std::int64_t value)
{
    write_u64(data, at, static_cast<std::uint64_t>(value));
}

void write_i32(std::span<std::uint8_t> data, std::size_t at, std::int32_t value)
{
    write_u32(data, at, static_cast<std::uint32_t>(value));
}

void copy_bytes(std::span<std::uint8_t> target, std::size_t at,
                std::span<const std::uint8_t> source)
{
    require(at + source.size() <= target.size(), "copy bounds");
    std::copy(source.begin(), source.end(), target.begin() + at);
}

template <std::size_t Size>
void copy_bytes(std::span<std::uint8_t> target, std::size_t at,
                const std::array<std::uint8_t, Size> &source)
{
    copy_bytes(target, at, std::span<const std::uint8_t>{source});
}

void copy_ascii(std::span<std::uint8_t> target, std::size_t at, std::string_view source)
{
    copy_bytes(target, at,
               std::span{reinterpret_cast<const std::uint8_t *>(source.data()), source.size()});
}

std::string trim(std::string value)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.pop_back();
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])))
        ++first;
    return value.substr(first);
}

std::vector<std::string> split(std::string_view value, char separator)
{
    std::vector<std::string> parts;
    std::size_t begin = 0;
    while (true)
    {
        const std::size_t end = value.find(separator, begin);
        parts.emplace_back(value.substr(begin, end - begin));
        if (end == std::string_view::npos)
            break;
        begin = end + 1;
    }
    return parts;
}

template <typename T> T parse_integer(std::string_view text, int base)
{
    T value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, base);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        throw std::runtime_error("invalid integer: " + std::string(text));
    return value;
}

bool valid_nid(std::string_view value)
{
    return std::all_of(value.begin(), value.end(),
                       [](unsigned char c) { return std::isalnum(c) || c == '+' || c == '-'; });
}

std::vector<ApiSymbol> read_api_surface(const std::filesystem::path &path)
{
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("cannot open API manifest: " + path.string());
    std::vector<ApiSymbol> symbols;
    std::string source;
    while (std::getline(input, source))
    {
        const std::string line = trim(source);
        if (line.empty() || line.front() == '#')
            continue;
        const auto parts = split(line, '|');
        if (parts.size() != 4 || parts[0].size() != 11 || !valid_nid(parts[0]) ||
            parts[3].size() < 3 || parts[3].substr(0, 2) != "0x")
            throw std::runtime_error("invalid API-surface line: " + source);
        const auto binding = parse_integer<unsigned>(parts[1], 10);
        const auto type = parse_integer<unsigned>(parts[2], 10);
        const auto size = parse_integer<std::uint64_t>(parts[3].substr(2), 16);
        if ((binding != 1 && binding != 2) || (type != 1 && type != 2 && type != 6))
            throw std::runtime_error("unsupported binding/type: " + source);
        symbols.push_back(
            {parts[0], static_cast<std::uint8_t>(binding), static_cast<std::uint8_t>(type), size});
    }
    require(symbols.size() == kExportCount,
            "API export count (" + std::to_string(symbols.size()) + ")");
    std::set<std::string> nids;
    std::size_t objects = 0;
    std::size_t functions = 0;
    std::size_t tls = 0;
    bool marker = false;
    bool longjmp = false;
    bool setjmp = false;
    for (const auto &symbol : symbols)
    {
        nids.insert(symbol.nid);
        objects += symbol.type == 1;
        functions += symbol.type == 2;
        tls += symbol.type == 6;
        marker |= symbol.nid == "P330P3dFF68" && symbol.binding == 1 && symbol.type == 1 &&
                  symbol.size == 4;
        longjmp |= symbol.nid == "+F+9hhi6k9Q" && symbol.binding == 2 && symbol.type == 2;
        setjmp |= symbol.nid == "sjpkrhugvVI" && symbol.binding == 1 && symbol.type == 2;
    }
    require(nids.size() == symbols.size(), "unique API NIDs");
    require(objects == 688, "API object count");
    require(functions == 1874, "API function count");
    require(tls == 4, "API TLS count");
    require(marker, "Need_sceLibc API record");
    require(longjmp, "_longjmp API record");
    require(setjmp, "_setjmp API record");
    return symbols;
}

std::vector<RuntimeImport> read_imports(const std::filesystem::path &path)
{
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("cannot open import manifest: " + path.string());
    std::vector<RuntimeImport> imports;
    std::string source;
    while (std::getline(input, source))
    {
        const std::string line = trim(source);
        if (line.empty() || line.front() == '#')
            continue;
        const auto parts = split(line, '|');
        if (parts.size() != 4 || (parts[1] != "#A#B" && parts[1] != "#B#C" && parts[1] != "#C#D") ||
            (parts[2] != "0" && parts[2] != "1") || (parts[3] != "0" && parts[3] != "1"))
            throw std::runtime_error("invalid import line: " + source);
        imports.push_back({parts[0], parts[1], parts[2] == "1", parts[3] == "1"});
    }
    require(imports.size() == kImportCount,
            "runtime import count (" + std::to_string(imports.size()) + ")");
    std::set<std::string> names;
    std::size_t plt = 0;
    std::size_t glob_dat = 0;
    for (const auto &entry : imports)
    {
        names.insert(entry.name);
        plt += entry.plt;
        glob_dat += entry.glob_dat;
    }
    require(names.size() == imports.size(), "unique import names");
    require(plt == kPltRelocationCount, "runtime PLT import count");
    require(glob_dat == kGlobDatRelocationCount, "runtime GLOB_DAT import count");
    for (std::string_view required :
         {"_sceKernelSetThreadDtors", "_sceKernelSetThreadAtexitCount",
          "_sceKernelSetThreadAtexitReport", "_sceKernelRtldSetApplicationHeapAPI", "malloc",
          "free", "posix_memalign", "__progname", "__stack_chk_guard", "__pthread_cxa_finalize"})
    {
        require(std::any_of(imports.begin(), imports.end(),
                            [&](const auto &entry) { return entry.name == required; }),
                "runtime required import " + std::string(required));
    }
    return imports;
}

std::string base64(std::span<const std::uint8_t> input)
{
    constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    for (std::size_t i = 0; i < input.size(); i += 3)
    {
        const std::uint32_t a = input[i];
        const std::uint32_t b = i + 1 < input.size() ? input[i + 1] : 0;
        const std::uint32_t c = i + 2 < input.size() ? input[i + 2] : 0;
        const std::uint32_t value = (a << 16) | (b << 8) | c;
        result.push_back(alphabet[(value >> 18) & 63]);
        result.push_back(alphabet[(value >> 12) & 63]);
        result.push_back(i + 1 < input.size() ? alphabet[(value >> 6) & 63] : '=');
        result.push_back(i + 2 < input.size() ? alphabet[value & 63] : '=');
    }
    return result;
}

std::string compute_nid(std::string_view name)
{
    Bytes input(name.begin(), name.end());
    input.insert(input.end(), kNidSuffix.begin(), kNidSuffix.end());
    const auto digest = ps5::crypto::sha1(input);
    std::array<std::uint8_t, 8> value{};
    std::reverse_copy(digest.begin(), digest.begin() + 8, value.begin());
    std::string result = base64(value).substr(0, 11);
    std::replace(result.begin(), result.end(), '/', '-');
    return result;
}

std::string hex(std::span<const std::uint8_t> value)
{
    std::ostringstream result;
    result << std::hex << std::setfill('0');
    for (const auto byte : value)
        result << std::setw(2) << static_cast<unsigned>(byte);
    return result.str();
}

std::uint32_t elf_hash(std::string_view name)
{
    std::uint32_t hash = 0;
    for (const unsigned char c : name)
    {
        hash = (hash << 4) + c;
        const std::uint32_t carry = hash & 0xF0000000U;
        if (carry)
            hash ^= carry >> 24;
        hash &= ~carry;
    }
    return hash;
}

Bytes build_sysv_hash(const std::vector<std::string> &names)
{
    const std::size_t count = names.size();
    Bytes table(8 + count * 8);
    write_u32(table, 0, static_cast<std::uint32_t>(count));
    write_u32(table, 4, static_cast<std::uint32_t>(count));
    const std::size_t chain = 8 + count * 4;
    for (std::size_t i = count - 1; i >= 1; --i)
    {
        const std::size_t bucket = elf_hash(names[i]) % count;
        write_u32(table, chain + i * 4, read_u32(table, 8 + bucket * 4));
        write_u32(table, 8 + bucket * 4, static_cast<std::uint32_t>(i));
    }
    return table;
}

std::size_t align_up(std::size_t value, std::size_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

void put_string(std::span<std::uint8_t> target, std::size_t at, std::string_view value)
{
    require(at + value.size() < target.size(),
            "dynamic string '" + std::string(value) + "' fits table");
    copy_ascii(target, at, value);
    target[at + value.size()] = 0;
}

void append_string(std::span<std::uint8_t> target, std::size_t &at, std::string_view value)
{
    put_string(target, static_cast<std::size_t>(at), value);
    at += value.size() + 1;
}

void write_symbol(std::span<std::uint8_t> symbols, std::size_t index, std::uint32_t name,
                  std::uint8_t info, std::uint8_t other, std::uint16_t section, std::uint64_t value,
                  std::uint64_t size)
{
    const std::size_t at = index * 24;
    require(at + 24 <= symbols.size(), "symbol bounds");
    write_u32(symbols, at, name);
    symbols[at + 4] = info;
    symbols[at + 5] = other;
    write_u16(symbols, at + 6, section);
    write_u64(symbols, at + 8, value);
    write_u64(symbols, at + 16, size);
}

void write_program_header(Bytes &file, std::size_t index, std::uint32_t type, std::uint32_t flags,
                          std::uint64_t offset, std::uint64_t address, std::uint64_t file_size,
                          std::uint64_t memory_size, std::uint64_t alignment)
{
    const std::size_t at = kProgramHeaderOffset + index * kProgramHeaderSize;
    write_u32(file, at, type);
    write_u32(file, at + 4, flags);
    write_u64(file, at + 8, offset);
    write_u64(file, at + 16, address);
    write_u64(file, at + 24, address);
    write_u64(file, at + 32, file_size);
    write_u64(file, at + 40, memory_size);
    write_u64(file, at + 48, alignment);
}

void build_elf_header(Bytes &file)
{
    copy_ascii(file, 0,
               "\x7f"
               "ELF");
    file[4] = 2;
    file[5] = 1;
    file[6] = 1;
    file[7] = 9;
    file[8] = 2;
    write_u16(file, 0x10, 0xFE18);
    write_u16(file, 0x12, 62);
    write_u32(file, 0x14, 1);
    write_u64(file, 0x20, kProgramHeaderOffset);
    write_u16(file, 0x34, 0x40);
    write_u16(file, 0x36, kProgramHeaderSize);
    write_u16(file, 0x38, kProgramHeaderCount);
}

void build_program_headers(Bytes &file)
{
    write_program_header(file, 0, 0x00000001, 0x1, 0x004000, 0x000000, 0xC8092, 0xC8092, 0x4000);
    write_program_header(file, 1, 0x00000001, 0x4, 0x0D0000, 0x0CC000, kReadOnlyFileSize,
                         kReadOnlyFileSize, 0x4000);
    write_program_header(file, 2, 0x00000001, 0x6, 0x110000, 0x10C000, 0x3EA0, 0x3EA0, 0x4000);
    write_program_header(file, 3, 0x6474E552, 0x4, 0x110000, 0x10C000, 0x3EA0, 0x4000, 1);
    write_program_header(file, 4, 0x00000001, 0x6, 0x114000, 0x110000, kDataFileSize,
                         kDataMemorySize, 0x4000);
    write_program_header(file, 5, 0x61000002, 0x4, kModuleParamFileOffset, 0x10FCE8, 0x20, 0x20, 8);
    write_program_header(file, 6, 0x00000002, 0x6, kMetadataFileOffset + kDynamicOffset,
                         kMetadataAddress + kDynamicOffset, 0x2C0, 0x2C0, 8);
    write_program_header(file, 7, 0x00000007, 0x4, 0x113D20, 0x10FD20, 0x180, 0x468, 0x10);
    write_program_header(file, 8, 0x6474E550, 0x4, kUnwindHeaderFileOffset, kUnwindHeaderAddress,
                         kUnwindHeaderSize, kUnwindHeaderSize, 4);
    write_program_header(file, 9, 0x00000001, 0, kMetadataFileOffset, kMetadataAddress,
                         kMetadataSize, kMetadataSize, 0x4000);
    write_program_header(file, 10, 0x6FFFFF00, 0, kCommentFileOffset, 0, kCommentSize, 0, 0x10);
    write_program_header(file, 11, 0x6FFFFF01, 0, kVersionFileOffset, 0, 0x1A, 0x20, 0x10);
    write_program_header(file, 12, 0x00000004, 0, kMetadataFileOffset + kBuildNoteOffset,
                         kMetadataAddress + kBuildNoteOffset, 0x24, 0x24, 4);
    write_program_header(file, 13, 0x00000004, 0, kTailNoteFileOffset, 0, 0x18, 0, 4);
}

void build_unwind_header(Bytes &file)
{
    auto header = std::span(file).subspan(kUnwindHeaderFileOffset, kUnwindHeaderSize);
    std::fill(header.begin(), header.end(), 0);
    header[0] = 1;
    header[1] = 0x1B;
    header[2] = 0x03;
    header[3] = 0x3B;
    write_u32(header, 4, 8);
    write_u32(header, 8, 0);
}

void build_module_param(Bytes &file)
{
    const std::size_t at = kModuleParamFileOffset;
    write_u64(file, at, 0x20);
    write_u32(file, at + 8, 0x3C13F4BF);
    write_u32(file, at + 0x0C, 3);
    write_u32(file, at + 0x10, 0x08050001);
    write_u32(file, at + 0x14, 0x02000009);
    write_u32(file, at + 0x18, 1);
}

std::uint64_t import_slot(const std::vector<RuntimeImport> &imports, std::string_view name)
{
    std::size_t slot = 0;
    for (const auto &entry : imports)
    {
        if (entry.name == name)
        {
            require(entry.plt, "runtime import " + std::string(name) + " has a PLT slot");
            return kGotAddress + (kGotReservedEntries + slot) * 8;
        }
        if (entry.plt)
            ++slot;
    }
    throw std::runtime_error("missing PLT import " + std::string(name));
}

void write_code(Bytes &file, std::uint64_t &address, std::span<const std::uint8_t> code)
{
    const std::size_t at = kTextFileOffset + static_cast<std::size_t>(address);
    require(at >= kTextFileOffset && at + code.size() <= kTextFileOffset + 0xC8092,
            "runtime code lies inside executable load");
    copy_bytes(file, at, code);
    address += code.size();
}

template <std::size_t N>
void write_code(Bytes &file, std::uint64_t &address, const std::array<std::uint8_t, N> &code)
{
    write_code(file, address, std::span<const std::uint8_t>{code});
}

void write_rip_relative_code(Bytes &file, std::uint64_t &address,
                             std::span<const std::uint8_t> opcode, std::uint64_t target)
{
    const std::size_t at = kTextFileOffset + static_cast<std::size_t>(address);
    const std::int64_t next = static_cast<std::int64_t>(address + opcode.size() + 4);
    const std::int64_t displacement = static_cast<std::int64_t>(target) - next;
    require(displacement >= std::numeric_limits<std::int32_t>::min() &&
                displacement <= std::numeric_limits<std::int32_t>::max(),
            "runtime RIP-relative displacement");
    copy_bytes(file, at, opcode);
    write_i32(file, at + opcode.size(), static_cast<std::int32_t>(displacement));
    address += opcode.size() + 4;
}

template <std::size_t N>
void write_rip_relative_code(Bytes &file, std::uint64_t &address,
                             const std::array<std::uint8_t, N> &opcode, std::uint64_t target)
{
    write_rip_relative_code(file, address, std::span<const std::uint8_t>{opcode}, target);
}

void build_code(Bytes &file, const std::vector<RuntimeImport> &imports)
{
    std::uint64_t cursor = 0x10;
    write_code(file, cursor, std::array<std::uint8_t, 4>{0x48, 0x83, 0xEC, 0x08});
    write_rip_relative_code(file, cursor, std::array<std::uint8_t, 3>{0x48, 0x8B, 0x05}, 0x10FCE0);
    write_code(file, cursor,
               std::array<std::uint8_t, 12>{0x48, 0x85, 0xC0, 0x74, 0x02, 0xFF, 0xD0, 0x48, 0x83,
                                            0xC4, 0x08, 0xC3});

    cursor = kInitAddress;
    write_code(file, cursor, std::array<std::uint8_t, 4>{0x48, 0x83, 0xEC, 0x08});
    write_rip_relative_code(file, cursor, std::array<std::uint8_t, 3>{0x48, 0x8D, 0x3D},
                            kThreadDtorsAddress);
    write_rip_relative_code(file, cursor, std::array<std::uint8_t, 2>{0xFF, 0x15},
                            import_slot(imports, "_sceKernelSetThreadDtors"));
    write_rip_relative_code(file, cursor, std::array<std::uint8_t, 3>{0x48, 0x8D, 0x3D},
                            kThreadAtexitCountAddress);
    write_rip_relative_code(file, cursor, std::array<std::uint8_t, 2>{0xFF, 0x15},
                            import_slot(imports, "_sceKernelSetThreadAtexitCount"));
    write_rip_relative_code(file, cursor, std::array<std::uint8_t, 3>{0x48, 0x8D, 0x3D},
                            kThreadAtexitReportAddress);
    write_rip_relative_code(file, cursor, std::array<std::uint8_t, 2>{0xFF, 0x15},
                            import_slot(imports, "_sceKernelSetThreadAtexitReport"));

    write_rip_relative_code(file, cursor, std::array<std::uint8_t, 3>{0x48, 0x8B, 0x05},
                            import_slot(imports, "malloc"));
    write_rip_relative_code(file, cursor, std::array<std::uint8_t, 3>{0x48, 0x89, 0x05},
                            kHeapApiAddress);
    write_rip_relative_code(file, cursor, std::array<std::uint8_t, 3>{0x48, 0x8B, 0x05},
                            import_slot(imports, "free"));
    write_rip_relative_code(file, cursor, std::array<std::uint8_t, 3>{0x48, 0x89, 0x05},
                            kHeapApiAddress + 8);
    write_rip_relative_code(file, cursor, std::array<std::uint8_t, 3>{0x48, 0x8B, 0x05},
                            import_slot(imports, "posix_memalign"));
    write_rip_relative_code(file, cursor, std::array<std::uint8_t, 3>{0x48, 0x89, 0x05},
                            kHeapApiAddress + 0x30);
    write_rip_relative_code(file, cursor, std::array<std::uint8_t, 3>{0x48, 0x8D, 0x3D},
                            kHeapApiAddress);
    write_rip_relative_code(file, cursor, std::array<std::uint8_t, 2>{0xFF, 0x15},
                            import_slot(imports, "_sceKernelRtldSetApplicationHeapAPI"));
    write_code(file, cursor, std::array<std::uint8_t, 7>{0x48, 0x83, 0xC4, 0x08, 0x31, 0xC0, 0xC3});
    require(cursor < kThreadDtorsAddress, "runtime initializer fits before callbacks");

    file[kTextFileOffset + kThreadDtorsAddress] = 0xC3;
    copy_bytes(file, kTextFileOffset + kThreadAtexitCountAddress,
               std::array<std::uint8_t, 3>{0x31, 0xC0, 0xC3});
    file[kTextFileOffset + kThreadAtexitReportAddress] = 0xC3;
    file[kTextFileOffset + kRelativeAnchorAddress] = 0xC3;
    copy_bytes(file, kTextFileOffset + 0x50,
               std::array<std::uint8_t, 7>{0x66, 0x0F, 0xEF, 0xC0, 0x31, 0xC0, 0xC3});
    copy_bytes(file, kTextFileOffset + 0x30, std::array<std::uint8_t, 3>{0x31, 0xC0, 0xC3});
    copy_bytes(file, kTextFileOffset + 0x40, std::array<std::uint8_t, 3>{0x31, 0xC0, 0xC3});
    file[kTextFileOffset + kFiniAddress] = 0xC3;
}

void build_export_symbols(std::span<std::uint8_t> symbols, const std::vector<ApiSymbol> &api,
                          const std::vector<std::size_t> &name_offsets)
{
    std::size_t object_cursor = kObjectStorageOffset;
    std::uint64_t tls_cursor = 0;
    for (std::size_t i = 0; i < api.size(); ++i)
    {
        const auto &symbol = api[i];
        std::uint64_t value = 0;
        std::uint16_t section = 0;
        std::uint8_t other = 0;
        if (symbol.type == 2)
        {
            value = symbol.nid == "+F+9hhi6k9Q" ? 0x30 : symbol.nid == "sjpkrhugvVI" ? 0x40 : 0x50;
            section = 3;
        }
        else if (symbol.type == 1 && symbol.nid == "P330P3dFF68")
        {
            value = kMarkerAddress;
            section = 6;
            other = 3;
        }
        else if (symbol.type == 1)
        {
            object_cursor = align_up(object_cursor, 8);
            value = kDataAddress + object_cursor;
            section = 6;
            object_cursor += static_cast<std::size_t>(std::max<std::uint64_t>(symbol.size, 1));
        }
        else if (symbol.type == 6)
        {
            tls_cursor = (tls_cursor + 7) & ~std::uint64_t{7};
            value = tls_cursor;
            section = 17;
            tls_cursor += std::max<std::uint64_t>(symbol.size, 1);
        }
        else
        {
            throw std::runtime_error("unsupported API symbol type");
        }
        write_symbol(symbols, i + 1, static_cast<std::uint32_t>(name_offsets[i]),
                     static_cast<std::uint8_t>((symbol.binding << 4) | symbol.type), other, section,
                     value, symbol.size);
    }
    require(object_cursor <= kDataMemorySize, "API object storage fits mapped data/BSS");
    require(tls_cursor <= 0x468, "API TLS storage fits PT_TLS");
}

std::uint64_t pack_name_version_id(std::uint32_t name, std::uint16_t version, std::uint16_t id)
{
    return name | (static_cast<std::uint64_t>(version) << 32) |
           (static_cast<std::uint64_t>(id) << 48);
}

std::uint64_t pack_attribute(std::uint16_t id, std::uint8_t attribute)
{
    return (static_cast<std::uint64_t>(id) << 48) | attribute;
}

void build_dynamic(std::span<std::uint8_t> dynamic, std::size_t symbol_count)
{
    const std::vector<std::pair<std::int64_t, std::uint64_t>> entries{
        {kDtNeeded, 0xA6C1},
        {kDtSceNeededModule, pack_name_version_id(0xA6CF, 0x0101, 1)},
        {kDtSceImportLib, pack_name_version_id(0xA6CF, 0x0001, 0)},
        {kDtSceImportLibAttr, pack_attribute(0, 0x09)},
        {kDtNeeded, 0xA6D9},
        {kDtSceNeededModule, pack_name_version_id(0xA6F0, 0x0101, 2)},
        {kDtSceImportLib, pack_name_version_id(0xA703, 0x0001, 1)},
        {kDtSceImportLibAttr, pack_attribute(1, 0x09)},
        {kDtNeeded, 0xA719},
        {kDtSceNeededModule, pack_name_version_id(0xA72D, 0x0101, 3)},
        {kDtSceImportLib, pack_name_version_id(0xA72D, 0x0001, 2)},
        {kDtSceImportLibAttr, pack_attribute(2, 0x09)},
        {kDtSoname, 0xA73D},
        {kDtSceModuleInfo, pack_name_version_id(0xA746, 0x0101, 0)},
        {kDtSceModuleAttr, 0},
        {kDtSceOrigFilename, 0xA74B},
        {kDtSceExportLib, pack_name_version_id(0xA746, 0x0001, 3)},
        {kDtSceExportLibAttr, pack_attribute(3, 0x01)},
        {kDtSceExportLib, pack_name_version_id(0xA797, 0x0001, 4)},
        {kDtSceExportLibAttr, pack_attribute(4, 0x01)},
        {kDtRela, kMetadataAddress + kRelaOffset},
        {kDtRelaSz,
         (kRelativeRelocationCount + kTlsRelocationCount + kGlobDatRelocationCount) * 24},
        {kDtRelaEnt, 24},
        {kDtRelaCount, kRelativeRelocationCount},
        {kDtJmpRel, kMetadataAddress + kJumpRelocationOffset},
        {kDtPltRelSz, kPltRelocationCount * 24},
        {kDtPltGot, kGotAddress},
        {kDtPltRel, 7},
        {kDtSymTab, kMetadataAddress + kSymbolTableOffset},
        {kDtSymEnt, 24},
        {kDtStrTab, kMetadataAddress},
        {kDtStrSz, kStringTableSize},
        {kDtHash, kMetadataAddress + kHashTableOffset},
        {kDtPreInitArray, 0x10FCE0},
        {kDtPreInitArraySz, 8},
        {kDtInitArray, 0},
        {kDtInitArraySz, 0},
        {kDtFiniArray, 0},
        {kDtFiniArraySz, 0},
        {kDtInit, 0x10},
        {kDtFini, kFiniAddress},
        {kDtSceSymTabSz, symbol_count * 24},
        {kDtSceHashSz, 0x5370},
        {kDtNull, 0}};
    require(entries.size() == kDynamicCount, "runtime dynamic entry count");
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        write_i64(dynamic, i * 16, entries[i].first);
        write_u64(dynamic, i * 16 + 8, entries[i].second);
    }
}

void build_gnu_note(std::span<std::uint8_t> note)
{
    write_u32(note, 0, 4);
    write_u32(note, 4, 0x14);
    write_u32(note, 8, 3);
    copy_ascii(note, 12, "GNU");
    constexpr std::string_view identity = "ps5-native-app-boilerplate clean-room runtime";
    const auto digest = ps5::crypto::sha256(
        std::span{reinterpret_cast<const std::uint8_t *>(identity.data()), identity.size()});
    copy_bytes(note, 16, std::span<const std::uint8_t>{digest}.first(20));
}

void build_metadata(Bytes &file, const std::vector<ApiSymbol> &api,
                    const std::vector<RuntimeImport> &imports)
{
    auto metadata = std::span(file).subspan(kMetadataFileOffset, kMetadataSize);
    std::size_t string_cursor = 1;
    std::vector<std::string> names{std::string{}};
    names.reserve(1 + api.size() + imports.size());
    std::vector<std::size_t> export_name_offsets(api.size());
    for (std::size_t i = 0; i < api.size(); ++i)
    {
        const std::string suffix =
            api[i].nid == "+F+9hhi6k9Q" || api[i].nid == "sjpkrhugvVI" ? "#E#A" : "#D#A";
        const std::string name = api[i].nid + suffix;
        export_name_offsets[i] = string_cursor;
        append_string(metadata, string_cursor, name);
        names.push_back(name);
    }
    std::vector<std::size_t> import_name_offsets(imports.size());
    for (std::size_t i = 0; i < imports.size(); ++i)
    {
        const std::string name = compute_nid(imports[i].name) + imports[i].suffix;
        import_name_offsets[i] = string_cursor;
        append_string(metadata, string_cursor, name);
        names.push_back(name);
    }
    require(string_cursor == 0xA6C1, "runtime symbol-name string extent");

    put_string(metadata, 0xA6C1, "libkernel.prx");
    put_string(metadata, 0xA6CF, "libkernel");
    put_string(metadata, 0xA6D9, "libSceLibcInternal.prx");
    put_string(metadata, 0xA6F0, "libSceLibcInternal");
    put_string(metadata, 0xA703, "libSceLibcInternalExt");
    put_string(metadata, 0xA719, "libSceSysmodule.prx");
    put_string(metadata, 0xA72D, "libSceSysmodule");
    put_string(metadata, 0xA73D, "libc.prx");
    put_string(metadata, 0xA746, "libc");
    put_string(metadata, 0xA74B, "libc.prx by BlackBearReloaded");
    put_string(metadata, 0xA797, "libc_setjmp");

    const std::size_t symbol_count = names.size();
    const std::size_t symbol_table_size = symbol_count * 24;
    require(symbol_table_size == 0xFA38, "runtime symbol-table size");
    auto symbols = metadata.subspan(kSymbolTableOffset, symbol_table_size);
    build_export_symbols(symbols, api, export_name_offsets);
    for (std::size_t i = 0; i < imports.size(); ++i)
    {
        const std::uint8_t type = imports[i].plt ? 2 : 1;
        write_symbol(symbols, api.size() + 1 + i,
                     static_cast<std::uint32_t>(import_name_offsets[i]),
                     static_cast<std::uint8_t>(0x10 | type), 0, 0, 0, 0);
    }

    auto jumps = metadata.subspan(kJumpRelocationOffset, kPltRelocationCount * 24);
    std::size_t jump = 0;
    for (std::size_t i = 0; i < imports.size(); ++i)
    {
        if (!imports[i].plt)
            continue;
        const std::size_t at = jump * 24;
        write_u64(jumps, at, kGotAddress + (kGotReservedEntries + jump) * 8);
        write_u64(jumps, at + 8, ((api.size() + 1 + i) << 32) | 7);
        ++jump;
    }
    require(jump == kPltRelocationCount, "runtime emitted PLT relocations");

    constexpr std::size_t relocation_count =
        kRelativeRelocationCount + kTlsRelocationCount + kGlobDatRelocationCount;
    auto rela = metadata.subspan(kRelaOffset, relocation_count * 24);
    std::size_t relocation = 0;
    for (std::size_t i = 0; i < kRelativeRelocationCount - 1; ++i)
    {
        const std::size_t at = relocation++ * 24;
        write_u64(rela, at, kRelativeAnchorSlotAddress + i * 8);
        write_u64(rela, at + 8, 8);
        write_u64(rela, at + 16, i == 0 ? kRelativeAnchorAddress : 0x50);
    }
    {
        const std::size_t at = relocation++ * 24;
        write_u64(rela, at, 0x10FCE0);
        write_u64(rela, at + 8, 8);
        write_u64(rela, at + 16, kInitAddress);
    }
    for (const std::uint64_t target : {0x10F818ULL, 0x10F828ULL, 0x10F838ULL})
    {
        const std::size_t at = relocation++ * 24;
        write_u64(rela, at, target);
        write_u64(rela, at + 8, 16);
    }
    for (const auto &[name, target] :
         std::array{std::pair<std::string_view, std::uint64_t>{"__stack_chk_guard", 0x10F800},
                    std::pair<std::string_view, std::uint64_t>{"__pthread_cxa_finalize", 0x10F848},
                    std::pair<std::string_view, std::uint64_t>{"__progname", 0x10F850}})
    {
        std::size_t symbol_index = imports.size();
        for (std::size_t i = 0; i < imports.size(); ++i)
            if (imports[i].name == name && imports[i].glob_dat)
                symbol_index = i;
        require(symbol_index < imports.size(), "runtime GLOB_DAT import " + std::string(name));
        const std::size_t at = relocation++ * 24;
        write_u64(rela, at, target);
        write_u64(rela, at + 8, ((api.size() + 1 + symbol_index) << 32) | 6);
    }
    require(relocation == relocation_count, "runtime emitted dynamic relocations");

    const Bytes hash = build_sysv_hash(names);
    require(hash.size() == 0x5370, "runtime SysV hash size");
    copy_bytes(metadata, kHashTableOffset, hash);
    require(kHashTableOffset + hash.size() == kBuildNoteOffset, "runtime tables end at build note");
    build_gnu_note(metadata.subspan(kBuildNoteOffset, 0x24));
    build_dynamic(metadata.subspan(kDynamicOffset, kDynamicCount * 16), symbol_count);

    write_u64(file, kGotFileOffset, kMetadataAddress + kDynamicOffset);
    std::fill(
        file.begin() + kGotFileOffset + 8,
        file.begin() + kGotFileOffset + 8 + (kGotReservedEntries - 1 + kPltRelocationCount) * 8, 0);
    std::fill(file.begin() + kPreinitFileOffset, file.begin() + kPreinitFileOffset + 8, 0);
    std::fill(file.begin() + kHeapApiFileOffset, file.begin() + kHeapApiFileOffset + kHeapApiSize,
              0);
}

void build_comment(Bytes &file)
{
    auto comment = std::span(file).subspan(kCommentFileOffset, kCommentSize);
    copy_ascii(comment, 0, "PATH");
    write_u32(comment, 4, 0x50);
    constexpr std::string_view value = "libc.prx by BlackBearReloaded";
    write_u32(comment, 8, value.size() + 1);
    copy_ascii(comment, 12, value);
}

void build_version(Bytes &file)
{
    constexpr std::array<std::uint8_t, 26> version{
        0x00, 0x00, 0x16, 0x00, 0x08, 'l',  'i',  'b',  'c',  ':',  0x02, 0x00, 0x00,
        0x09, 0x00, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x01};
    copy_bytes(file, kVersionFileOffset, version);
}

Bytes build_runtime(const std::vector<ApiSymbol> &api, const std::vector<RuntimeImport> &imports)
{
    Bytes file(kRuntimeFileSize);
    build_elf_header(file);
    write_u64(file, 0x28, 0x194F18);
    write_u16(file, 0x3A, 0x40);
    write_u16(file, 0x3C, 0x26);
    write_u16(file, 0x3E, 0x23);
    build_program_headers(file);
    build_code(file, imports);
    write_u32(file, kMarkerFileOffset, 1);
    build_unwind_header(file);
    build_module_param(file);
    build_metadata(file, api, imports);
    build_comment(file);
    build_version(file);
    constexpr std::string_view attribution =
        "ps5-native-app-boilerplate clean-room libc by BlackBearReloaded";
    copy_ascii(file, kMarkerFileOffset + 0x100, attribution);
    return file;
}

std::string read_ascii_z(std::span<const std::uint8_t> data, std::size_t at)
{
    std::size_t end = at;
    while (end < data.size() && data[end] != 0)
        ++end;
    return {reinterpret_cast<const char *>(data.data() + at), end - at};
}

void verify_runtime(const Bytes &file, const std::vector<ApiSymbol> &api,
                    const std::vector<RuntimeImport> &imports)
{
    require(file.size() == kRuntimeFileSize, "runtime raw file size");
    require(read_u32(file, 0) == 0x464C457F, "runtime ELF magic");
    require(read_u16(file, 0x10) == 0xFE18, "runtime module type");
    require(read_u16(file, 0x38) == kProgramHeaderCount, "runtime program-header count");
    require(read_u32(file, kMarkerFileOffset) == 1, "runtime Need_sceLibc marker");
    require(read_u64(file, kGotFileOffset) == kMetadataAddress + kDynamicOffset,
            "runtime GOT dynamic pointer");
    require(read_ascii_z(file, kMetadataFileOffset + 0xA74B) == "libc.prx by BlackBearReloaded",
            "runtime clean original filename");
    require(api.size() + imports.size() + 1 == 2669, "runtime dynamic symbol count");
    const std::string ascii(reinterpret_cast<const char *>(file.data()), file.size());
    require(ascii.find("BlackBearReloaded") != std::string::npos, "runtime attribution marker");
    for (std::string_view forbidden :
         {"W:/Build", "W:\\Build", "J013", "Prospero_Release", "sys/internal"})
        require(ascii.find(forbidden) == std::string::npos,
                "runtime forbidden reference text: " + std::string(forbidden));
}

void write_file(const std::filesystem::path &path, std::span<const std::uint8_t> data)
{
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        throw std::runtime_error("cannot create output: " + path.string());
    output.write(reinterpret_cast<const char *>(data.data()), data.size());
    if (!output)
        throw std::runtime_error("cannot write output: " + path.string());
}

} // namespace

int main(int argc, char **argv)
{
    try
    {
        if (argc != 4)
        {
            std::cerr << "usage: libc-builder <api-manifest> <import-manifest> <output.elf>\n";
            return 2;
        }
        const auto api = read_api_surface(std::filesystem::absolute(argv[1]));
        const auto imports = read_imports(std::filesystem::absolute(argv[2]));
        const Bytes image = build_runtime(api, imports);
        verify_runtime(image, api, imports);
        const auto output = std::filesystem::absolute(argv[3]);
        write_file(output, image);
        std::cout << "built clean-room runtime\n"
                  << "wrote " << image.size() << " bytes: " << output.string() << '\n'
                  << "sha256 " << hex(ps5::crypto::sha256(image)) << '\n';
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
