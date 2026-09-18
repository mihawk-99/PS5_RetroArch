/*
 * ps5-native-app-boilerplate - Native PS5 dynamic-module writer.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Converts an ordinary PIE produced by LLVM lld into the loader-visible PS5
 * executable shape. LLVM remains responsible for C/C++ linking, archives,
 * COMDAT, TLS, unwind data, and ordinary x86-64 relocations.
 */

#include "sce_module_writer.hpp"

#include "hash.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ps5::module
{
namespace
{

using elf::Bytes;
using elf::Image;
using elf::Section;
using elf::Stub;

constexpr std::uint64_t kPage = 0x4000;
constexpr std::uint16_t kTypeDynamicExecutable = 0xfe10;
constexpr std::uint32_t kProgramLoad = 1;
constexpr std::uint32_t kProgramDynamic = 2;
constexpr std::uint32_t kProgramNote = 4;
constexpr std::uint32_t kProgramTls = 7;
constexpr std::uint32_t kProgramProcParam = 0x61000001;
constexpr std::uint32_t kProgramGnuEhFrame = 0x6474e550;
constexpr std::uint32_t kProgramGnuRelro = 0x6474e552;
constexpr std::uint32_t kProgramComment = 0x6fffff00;
constexpr std::uint32_t kProgramVersion = 0x6fffff01;
constexpr std::uint32_t kFlagExecute = 1;
constexpr std::uint32_t kFlagWrite = 2;
constexpr std::uint32_t kFlagRead = 4;

constexpr std::int64_t kDynamicNeeded = 1;
constexpr std::int64_t kDynamicPltRelSize = 2;
constexpr std::int64_t kDynamicPltGot = 3;
constexpr std::int64_t kDynamicHash = 4;
constexpr std::int64_t kDynamicStringTable = 5;
constexpr std::int64_t kDynamicSymbolTable = 6;
constexpr std::int64_t kDynamicRela = 7;
constexpr std::int64_t kDynamicRelaSize = 8;
constexpr std::int64_t kDynamicRelaEntry = 9;
constexpr std::int64_t kDynamicStringSize = 10;
constexpr std::int64_t kDynamicSymbolEntry = 11;
constexpr std::int64_t kDynamicInit = 12;
constexpr std::int64_t kDynamicFini = 13;
constexpr std::int64_t kDynamicPltRel = 20;
constexpr std::int64_t kDynamicDebug = 21;
constexpr std::int64_t kDynamicJumpRela = 23;
constexpr std::int64_t kDynamicInitArray = 25;
constexpr std::int64_t kDynamicFiniArray = 26;
constexpr std::int64_t kDynamicInitArraySize = 27;
constexpr std::int64_t kDynamicFiniArraySize = 28;
constexpr std::int64_t kDynamicPreinitArray = 32;
constexpr std::int64_t kDynamicPreinitArraySize = 33;
constexpr std::int64_t kDynamicRelaCount = 0x6ffffff9;
constexpr std::int64_t kDynamicModuleAttributes = 0x61000011;
constexpr std::int64_t kDynamicImportLibraryAttributes = 0x61000019;
constexpr std::int64_t kDynamicHashSize = 0x6100003d;
constexpr std::int64_t kDynamicSymbolTableSize = 0x6100003f;
constexpr std::int64_t kDynamicOriginalFilename = 0x61000041;
constexpr std::int64_t kDynamicModuleInfo = 0x61000043;
constexpr std::int64_t kDynamicNeededModule = 0x61000045;
constexpr std::int64_t kDynamicImportLibrary = 0x61000049;
constexpr std::uint32_t kRelRelative = 8;

void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

std::uint64_t align_up(std::uint64_t value, std::uint64_t alignment)
{
    return alignment <= 1 ? value : (value + alignment - 1) / alignment * alignment;
}

std::uint64_t congruent_offset(std::uint64_t minimum, std::uint64_t address)
{
    const std::uint64_t residue = address % kPage;
    if (minimum <= residue)
        return residue;
    return align_up(minimum - residue, kPage) + residue;
}

void write_u16(std::span<std::uint8_t> data, std::size_t at, std::uint16_t value)
{
    require(at + 2 <= data.size(), "u16 write outside output");
    data[at] = static_cast<std::uint8_t>(value);
    data[at + 1] = static_cast<std::uint8_t>(value >> 8);
}

void write_u32(std::span<std::uint8_t> data, std::size_t at, std::uint32_t value)
{
    require(at + 4 <= data.size(), "u32 write outside output");
    for (std::size_t i = 0; i < 4; ++i)
        data[at + i] = static_cast<std::uint8_t>(value >> (i * 8));
}

void write_u64(std::span<std::uint8_t> data, std::size_t at, std::uint64_t value)
{
    require(at + 8 <= data.size(), "u64 write outside output");
    for (std::size_t i = 0; i < 8; ++i)
        data[at + i] = static_cast<std::uint8_t>(value >> (i * 8));
}

std::uint32_t read_u32(std::span<const std::uint8_t> data, std::size_t at)
{
    require(at + 4 <= data.size(), "u32 read outside input");
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i)
        value |= static_cast<std::uint32_t>(data[at + i]) << (i * 8);
    return value;
}

std::uint64_t read_u64(std::span<const std::uint8_t> data, std::size_t at)
{
    require(at + 8 <= data.size(), "u64 read outside input");
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i)
        value |= static_cast<std::uint64_t>(data[at + i]) << (i * 8);
    return value;
}

void copy_bytes(std::span<std::uint8_t> target, std::uint64_t at,
                std::span<const std::uint8_t> source)
{
    require(at <= target.size() && source.size() <= target.size() - at, "copy outside output");
    std::copy(source.begin(), source.end(), target.begin() + at);
}

const Section &section(const Image &image, std::string_view name)
{
    const auto found = std::find_if(image.sections.begin(), image.sections.end(),
                                    [&](const Section &value) { return value.name == name; });
    require(found != image.sections.end(),
            "LLVM-linked image lacks required section " + std::string{name});
    return *found;
}

const Section *optional_section(const Image &image, std::string_view name)
{
    const auto found = std::find_if(image.sections.begin(), image.sections.end(),
                                    [&](const Section &value) { return value.name == name; });
    return found == image.sections.end() ? nullptr : &*found;
}

bool metadata_section(std::string_view name)
{
    return name == ".dynstr" || name == ".dynsym" || name == ".dynamic" || name == ".hash" ||
           name == ".gnu.hash" || name.starts_with(".rela.");
}

bool relro_section(std::string_view name)
{
    return name == ".got" || name == ".got.plt" || name.starts_with(".data.rel.ro") ||
           name.starts_with(".preinit_array") || name.starts_with(".init_array") ||
           name.starts_with(".fini_array");
}

std::string encode_id(int id)
{
    constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-";
    if (id == 0)
        return "A";
    std::string output;
    while (id > 0)
    {
        output.insert(output.begin(), alphabet[static_cast<std::size_t>(id % 64)]);
        id /= 64;
    }
    return output;
}

std::string nid(std::string_view name)
{
    constexpr std::array<std::uint8_t, 16> suffix = {0x51, 0x8d, 0x64, 0xa6, 0x35, 0xde,
                                                     0xd8, 0xc1, 0xe6, 0xb0, 0x39, 0xb1,
                                                     0xc3, 0xe5, 0x52, 0x30};
    Bytes input(name.begin(), name.end());
    input.insert(input.end(), suffix.begin(), suffix.end());
    const auto digest = crypto::sha1(input);
    std::array<std::uint8_t, 8> value{};
    std::reverse_copy(digest.begin(), digest.begin() + 8, value.begin());
    constexpr std::string_view base64 =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    unsigned bits = 0;
    int bit_count = 0;
    for (std::uint8_t byte : value)
    {
        bits = (bits << 8) | byte;
        bit_count += 8;
        while (bit_count >= 6 && result.size() < 11)
        {
            bit_count -= 6;
            result.push_back(base64[(bits >> bit_count) & 0x3f]);
        }
    }
    if (result.size() < 11 && bit_count > 0 && bit_count < 6)
        result.push_back(base64[(bits << (6 - bit_count)) & 0x3f]);
    result.resize(11);
    std::replace(result.begin(), result.end(), '/', '-');
    return result;
}

class StringTable
{
  public:
    StringTable()
    {
        data_.push_back(0);
    }

    std::uint32_t add(std::string_view value)
    {
        const auto found = offsets_.find(std::string{value});
        if (found != offsets_.end())
            return found->second;
        const std::uint32_t offset = static_cast<std::uint32_t>(data_.size());
        data_.insert(data_.end(), value.begin(), value.end());
        data_.push_back(0);
        offsets_.emplace(value, offset);
        return offset;
    }

    [[nodiscard]] const Bytes &data() const
    {
        return data_;
    }

  private:
    Bytes data_;
    std::map<std::string, std::uint32_t, std::less<>> offsets_;
};

std::uint32_t elf_hash(std::string_view name)
{
    std::uint32_t hash = 0;
    for (unsigned char character : name)
    {
        hash = (hash << 4) + character;
        const std::uint32_t carry = hash & 0xf0000000;
        if (carry != 0)
            hash ^= carry >> 24;
        hash &= ~carry;
    }
    return hash;
}

Bytes build_sysv_hash(std::span<const std::string> names)
{
    require(!names.empty(), "dynamic symbol table cannot be empty");
    const std::size_t count = names.size();
    Bytes output(8 + count * 8);
    write_u32(output, 0, static_cast<std::uint32_t>(count));
    write_u32(output, 4, static_cast<std::uint32_t>(count));
    const std::size_t chains = 8 + count * 4;
    for (std::size_t i = count; i-- > 1;)
    {
        const std::size_t bucket = elf_hash(names[i]) % count;
        const std::uint32_t previous = read_u32(output, 8 + bucket * 4);
        write_u32(output, chains + i * 4, previous);
        write_u32(output, 8 + bucket * 4, static_cast<std::uint32_t>(i));
    }
    return output;
}

Bytes build_process_parameters(std::uint32_t module_sdk, std::uint32_t companion_sdk)
{
    Bytes output(0x60);
    write_u64(output, 0, 0x60);
    output[8] = 'O';
    output[9] = 'R';
    output[10] = 'B';
    output[11] = 'I';
    write_u32(output, 0x0c, 5);
    write_u32(output, 0x10, companion_sdk);
    write_u32(output, 0x14, module_sdk);
    write_u32(output, 0x58, 1);
    return output;
}

struct ParameterBlocks
{
    Bytes data;
    std::array<std::size_t, 6> offsets{};
    std::size_t heap_size{};
    std::size_t heap_extended{};
};

ParameterBlocks build_parameter_blocks()
{
    constexpr std::array<std::size_t, 6> sizes = {0xa8, 0x38, 0x10, 0x78, 0xc0, 0x38};
    constexpr std::array<std::uint64_t, 6> counts = {0x000000010000000e, 0, 0, 2, 3, 1};
    ParameterBlocks result;
    std::size_t cursor = 0;
    for (std::size_t i = 0; i < sizes.size(); ++i)
    {
        result.offsets[i] = cursor;
        cursor += sizes[i];
    }
    result.heap_size = cursor;
    result.heap_extended = cursor + 8;
    result.data.resize(cursor + 16);
    for (std::size_t i = 0; i < sizes.size(); ++i)
    {
        write_u64(result.data, result.offsets[i], sizes[i]);
        if (counts[i] != 0)
            write_u64(result.data, result.offsets[i] + 8, counts[i]);
    }
    write_u64(result.data, result.heap_size, std::numeric_limits<std::uint64_t>::max());
    write_u32(result.data, result.heap_extended, 1);
    return result;
}

Bytes build_note()
{
    Bytes output(0x24);
    write_u32(output, 0, 4);
    write_u32(output, 4, 0x14);
    write_u32(output, 8, 3);
    output[12] = 'G';
    output[13] = 'N';
    output[14] = 'U';
    return output;
}

Bytes build_tail_note()
{
    Bytes output(0x18);
    write_u32(output, 0, 4);
    write_u32(output, 4, 8);
    write_u32(output, 8, 3);
    output[12] = 'S';
    output[13] = 'I';
    output[14] = 'E';
    return output;
}

Bytes build_comment(std::string_view file_name)
{
    Bytes text(file_name.begin(), file_name.end());
    text.push_back(0);
    Bytes output(align_up(12 + text.size(), 4));
    output[0] = 'P';
    output[1] = 'A';
    output[2] = 'T';
    output[3] = 'H';
    write_u32(output, 4, static_cast<std::uint32_t>(output.size() - 8));
    write_u32(output, 8, static_cast<std::uint32_t>(text.size()));
    copy_bytes(output, 12, text);
    return output;
}

void write_u32_be(std::span<std::uint8_t> data, std::size_t at, std::uint32_t value)
{
    require(at + 4 <= data.size(), "big-endian write outside output");
    data[at] = static_cast<std::uint8_t>(value >> 24);
    data[at + 1] = static_cast<std::uint8_t>(value >> 16);
    data[at + 2] = static_cast<std::uint8_t>(value >> 8);
    data[at + 3] = static_cast<std::uint8_t>(value);
}

Bytes build_version(std::span<const std::string> components, std::uint32_t module_sdk)
{
    Bytes output;
    for (const std::string &component : components)
    {
        const std::string name = component + ':';
        const std::size_t body = 1 + name.size() + 16;
        const std::size_t start = output.size();
        output.resize(start + 4 + body);
        write_u16(output, start + 2, static_cast<std::uint16_t>(body));
        output[start + 4] = 8;
        std::copy(name.begin(), name.end(), output.begin() + start + 5);
        for (std::size_t i = 0; i < 2; ++i)
        {
            const std::size_t at = start + 5 + name.size() + i * 8;
            write_u32_be(output, at, module_sdk);
            write_u32_be(output, at + 4, 1);
        }
    }
    return output;
}

struct Import
{
    std::string plain;
    const Stub *provider{};
    int module_id{};
    int library_id{};
    std::uint32_t dynamic_symbol{};
    std::string mangled;
};

struct ModuleRecord
{
    int id{};
    std::uint32_t soname{};
    std::uint32_t module_name{};
};

struct LibraryRecord
{
    int id{};
    int module_id{};
    std::uint32_t name{};
};

struct DynamicRelocation
{
    std::uint64_t offset{};
    std::uint64_t info{};
    std::uint64_t addend{};
};

std::vector<DynamicRelocation> read_relocations(const Section *input)
{
    std::vector<DynamicRelocation> output;
    if (input == nullptr)
        return output;
    require(input->entry_size == 24 && input->data.size() % 24 == 0,
            "LLVM relocation table has an unsupported layout");
    for (std::size_t at = 0; at < input->data.size(); at += 24)
        output.push_back({read_u64(input->data, at), read_u64(input->data, at + 8),
                          read_u64(input->data, at + 16)});
    return output;
}

Bytes write_relocations(std::span<const DynamicRelocation> relocations)
{
    Bytes output(relocations.size() * 24);
    for (std::size_t i = 0; i < relocations.size(); ++i)
    {
        write_u64(output, i * 24, relocations[i].offset);
        write_u64(output, i * 24 + 8, relocations[i].info);
        write_u64(output, i * 24 + 16, relocations[i].addend);
    }
    return output;
}

std::uint64_t dynamic_value(const Section &dynamic, std::int64_t wanted)
{
    require(dynamic.entry_size == 16 && dynamic.data.size() % 16 == 0,
            "LLVM dynamic table has an unsupported layout");
    for (std::size_t at = 0; at < dynamic.data.size(); at += 16)
    {
        const auto tag = static_cast<std::int64_t>(read_u64(dynamic.data, at));
        if (tag == wanted)
            return read_u64(dynamic.data, at + 8);
        if (tag == 0)
            break;
    }
    return 0;
}

void add_dynamic(std::vector<std::pair<std::int64_t, std::uint64_t>> &entries, std::int64_t tag,
                 std::uint64_t value)
{
    entries.emplace_back(tag, value);
}

Bytes build_dynamic(std::span<const ModuleRecord> modules, std::span<const LibraryRecord> libraries,
                    std::uint32_t module_info_name, std::uint32_t original_file_name,
                    std::uint64_t symbol_table, std::uint64_t string_table,
                    std::uint64_t string_size, std::uint64_t hash_table, std::uint64_t hash_size,
                    std::uint64_t jump_relocations, std::uint64_t jump_relocations_size,
                    std::uint64_t got, std::uint64_t symbol_table_size, std::uint64_t relocations,
                    std::uint64_t relocations_size, std::size_t relative_count,
                    std::uint64_t preinit, std::uint64_t preinit_size, std::uint64_t init_array,
                    std::uint64_t init_array_size, std::uint64_t fini_array,
                    std::uint64_t fini_array_size, std::uint64_t init, std::uint64_t fini)
{
    std::vector<std::pair<std::int64_t, std::uint64_t>> entries;
    for (const ModuleRecord &module : modules)
    {
        add_dynamic(entries, kDynamicNeeded, module.soname);
        add_dynamic(entries, kDynamicNeededModule,
                    module.module_name | (std::uint64_t{1} << 32) |
                        (static_cast<std::uint64_t>(module.id) << 48));
        for (const LibraryRecord &library : libraries)
        {
            if (library.module_id != module.id)
                continue;
            add_dynamic(entries, kDynamicImportLibrary,
                        library.name | (std::uint64_t{1} << 32) |
                            (static_cast<std::uint64_t>(library.id) << 48));
            add_dynamic(entries, kDynamicImportLibraryAttributes,
                        (static_cast<std::uint64_t>(library.id) << 48) | 9);
        }
    }
    add_dynamic(entries, kDynamicModuleInfo, module_info_name | (std::uint64_t{1} << 32));
    add_dynamic(entries, kDynamicModuleAttributes, 0);
    add_dynamic(entries, kDynamicOriginalFilename, original_file_name);
    add_dynamic(entries, kDynamicDebug, 0);
    add_dynamic(entries, kDynamicRela, relocations);
    add_dynamic(entries, kDynamicRelaSize, relocations_size);
    add_dynamic(entries, kDynamicRelaEntry, 24);
    add_dynamic(entries, kDynamicRelaCount, relative_count);
    add_dynamic(entries, kDynamicJumpRela, jump_relocations);
    add_dynamic(entries, kDynamicPltRelSize, jump_relocations_size);
    add_dynamic(entries, kDynamicPltGot, got);
    add_dynamic(entries, kDynamicPltRel, kDynamicRela);
    add_dynamic(entries, kDynamicSymbolTable, symbol_table);
    add_dynamic(entries, kDynamicSymbolEntry, 24);
    add_dynamic(entries, kDynamicStringTable, string_table);
    add_dynamic(entries, kDynamicStringSize, string_size);
    add_dynamic(entries, kDynamicHash, hash_table);
    add_dynamic(entries, kDynamicPreinitArray, preinit);
    add_dynamic(entries, kDynamicPreinitArraySize, preinit_size);
    add_dynamic(entries, kDynamicInitArray, init_array);
    add_dynamic(entries, kDynamicInitArraySize, init_array_size);
    add_dynamic(entries, kDynamicFiniArray, fini_array);
    add_dynamic(entries, kDynamicFiniArraySize, fini_array_size);
    add_dynamic(entries, kDynamicInit, init);
    add_dynamic(entries, kDynamicFini, fini);
    add_dynamic(entries, kDynamicSymbolTableSize, symbol_table_size);
    add_dynamic(entries, kDynamicHashSize, hash_size);
    add_dynamic(entries, 0, 0);
    Bytes output(entries.size() * 16);
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        write_u64(output, i * 16, static_cast<std::uint64_t>(entries[i].first));
        write_u64(output, i * 16 + 8, entries[i].second);
    }
    return output;
}

struct ProgramHeader
{
    std::uint32_t type{};
    std::uint32_t flags{};
    std::uint64_t offset{};
    std::uint64_t address{};
    std::uint64_t file_size{};
    std::uint64_t memory_size{};
    std::uint64_t alignment{};
};

void write_program_header(std::span<std::uint8_t> output, std::size_t index,
                          const ProgramHeader &header)
{
    const std::size_t at = 0x40 + index * 0x38;
    write_u32(output, at, header.type);
    write_u32(output, at + 4, header.flags);
    write_u64(output, at + 8, header.offset);
    write_u64(output, at + 16, header.address);
    write_u64(output, at + 24, header.address);
    write_u64(output, at + 32, header.file_size);
    write_u64(output, at + 40, header.memory_size);
    write_u64(output, at + 48, header.alignment);
}

void write_elf_header(std::span<std::uint8_t> output, std::uint64_t entry)
{
    require(output.size() >= kPage, "output cannot hold ELF header");
    output[0] = 0x7f;
    output[1] = 'E';
    output[2] = 'L';
    output[3] = 'F';
    output[4] = 2;
    output[5] = 1;
    output[6] = 1;
    output[7] = 9;
    output[8] = 2;
    write_u16(output, 0x10, kTypeDynamicExecutable);
    write_u16(output, 0x12, 0x3e);
    write_u32(output, 0x14, 1);
    write_u64(output, 0x18, entry);
    write_u64(output, 0x20, 0x40);
    write_u64(output, 0x28, 0);
    write_u32(output, 0x30, 0);
    write_u16(output, 0x34, 0x40);
    write_u16(output, 0x36, 0x38);
    write_u16(output, 0x38, 14);
    write_u16(output, 0x3a, 0x40);
    write_u16(output, 0x3c, 0);
    write_u16(output, 0x3e, 0);
}

} // namespace

Bytes write_executable(const Image &image, std::span<const Stub> stubs, const Options &options)
{
    const Section &text = section(image, ".text");
    const Section &dynamic_source = section(image, ".dynamic");
    const Section &got = section(image, ".got");
    const Section *eh_header = optional_section(image, ".eh_frame_hdr");
    require(text.executable() && text.address == 0 && text.file_offset >= kPage,
            "LLVM text layout is incompatible with the PS5 converter");

    std::uint64_t copied_file_end = kPage;
    for (const Section &input : image.sections)
    {
        if (!input.allocated() || input.no_bits())
            continue;
        copied_file_end = std::max(copied_file_end, input.file_offset + input.data.size());
    }

    std::uint64_t text_end = text.address + text.size;
    std::uint64_t ro_start = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t ro_end = 0;
    std::uint64_t relro_start = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t relro_content_end = 0;
    std::uint64_t data_start = dynamic_source.address;
    std::uint64_t data_end = data_start + 8;
    std::uint64_t data_stored_end = data_start + 8;
    std::uint64_t tls_start = 0;
    std::uint64_t tls_file_end = 0;
    std::uint64_t tls_memory_end = 0;
    std::uint64_t tls_alignment = 0x20;

    for (const Section &input : image.sections)
    {
        if (!input.allocated() || input.name == ".dynamic")
            continue;
        const std::uint64_t end = input.address + input.size;
        if (input.executable())
        {
            text_end = std::max(text_end, end);
        }
        else if (input.tls())
        {
            if (tls_start == 0)
                tls_start = input.address;
            tls_start = std::min(tls_start, input.address);
            tls_memory_end = std::max(tls_memory_end, end);
            if (!input.no_bits())
                tls_file_end = std::max(tls_file_end, end);
            tls_alignment = std::max(tls_alignment, input.alignment);
        }
        else if (relro_section(input.name))
        {
            relro_start = std::min(relro_start, input.address);
            relro_content_end = std::max(relro_content_end, end);
        }
        else if (input.writable() || input.no_bits())
        {
            data_start = std::min(data_start, input.address);
            data_end = std::max(data_end, end);
            if (!input.no_bits())
                data_stored_end = std::max(data_stored_end, end);
        }
        else if (!metadata_section(input.name))
        {
            ro_start = std::min(ro_start, input.address);
            ro_end = std::max(ro_end, end);
        }
    }
    require(relro_start != std::numeric_limits<std::uint64_t>::max(),
            "LLVM-linked image has no GOT/RELRO region");
    if (ro_start == std::numeric_limits<std::uint64_t>::max())
    {
        ro_start = align_up(text_end, kPage);
        ro_end = ro_start;
    }

    const Bytes process_parameters =
        build_process_parameters(options.module_sdk, options.companion_sdk);
    const ParameterBlocks blocks = build_parameter_blocks();
    const std::uint64_t process_address =
        align_up(std::max(relro_content_end, ro_end > relro_start ? ro_end : 0), 8);
    const std::uint64_t blocks_address = align_up(process_address + process_parameters.size(), 8);
    const std::uint64_t relro_end = blocks_address + blocks.data.size();
    require(relro_end <= data_start, "LLVM layout leaves no room for PS5 process parameters");

    std::vector<Import> imports;
    std::vector<const Stub *> module_order;
    for (const std::string &needed : image.needed)
    {
        const auto provider = std::find_if(stubs.begin(), stubs.end(), [&](const Stub &candidate)
                                           { return candidate.soname == needed; });
        require(provider != stubs.end(), "public SDK stub directory lacks needed module " + needed);
        module_order.push_back(&*provider);
    }
    for (std::size_t i = 1; i < image.dynamic_symbols.size(); ++i)
    {
        const elf::Symbol &symbol = image.dynamic_symbols[i];
        require(symbol.undefined(), "native converter does not yet publish application exports");
        const Stub *provider = nullptr;
        for (const Stub *candidate : module_order)
        {
            if (std::find(candidate->exports.begin(), candidate->exports.end(), symbol.name) !=
                candidate->exports.end())
            {
                provider = candidate;
                break;
            }
        }
        require(provider != nullptr, "no public SDK stub exports required symbol " + symbol.name);
        imports.push_back({symbol.name, provider, 0, 0, static_cast<std::uint32_t>(i), {}});
    }

    std::vector<ModuleRecord> modules;
    std::vector<LibraryRecord> libraries;
    StringTable strings;
    for (std::size_t i = 0; i < module_order.size(); ++i)
    {
        const Stub *provider = module_order[i];
        modules.push_back({static_cast<int>(i + 1), strings.add(provider->soname),
                           strings.add(provider->module_name)});
        libraries.push_back(
            {static_cast<int>(i), static_cast<int>(i + 1), strings.add(provider->library_name)});
    }
    for (Import &import : imports)
    {
        const auto module = std::find(module_order.begin(), module_order.end(), import.provider);
        require(module != module_order.end(), "internal import provider error");
        const int index = static_cast<int>(module - module_order.begin());
        import.module_id = index + 1;
        import.library_id = index;
        import.mangled = nid(import.plain) + "#" + encode_id(import.library_id) + "#" +
                         encode_id(import.module_id);
    }

    const std::uint32_t file_name_offset = strings.add(options.file_name);
    std::string module_name = options.file_name;
    if (const std::size_t dot = module_name.find_last_of('.'); dot != std::string::npos)
        module_name.resize(dot);
    const std::uint32_t module_name_offset = strings.add(module_name);

    Bytes dynamic_symbols(image.dynamic_symbols.size() * 24);
    std::vector<std::string> hash_names(image.dynamic_symbols.size());
    for (const Import &import : imports)
    {
        const elf::Symbol &source = image.dynamic_symbols[import.dynamic_symbol];
        const std::size_t at = import.dynamic_symbol * 24;
        write_u32(dynamic_symbols, at, strings.add(import.mangled));
        dynamic_symbols[at + 4] = source.info;
        dynamic_symbols[at + 5] = source.other;
        write_u16(dynamic_symbols, at + 6, source.section);
        write_u64(dynamic_symbols, at + 8, source.value);
        write_u64(dynamic_symbols, at + 16, source.size);
        hash_names[import.dynamic_symbol] = nid(import.plain) + "#" +
                                            import.provider->library_name + "#" +
                                            import.provider->module_name;
    }
    const Bytes dynamic_strings = strings.data();
    const Bytes hash = build_sysv_hash(hash_names);

    std::vector<DynamicRelocation> source_relocations =
        read_relocations(optional_section(image, ".rela.dyn"));
    std::vector<DynamicRelocation> relative;
    std::vector<DynamicRelocation> symbolic;
    for (const DynamicRelocation &relocation : source_relocations)
    {
        if (static_cast<std::uint32_t>(relocation.info) == kRelRelative)
            relative.push_back(relocation);
        else
            symbolic.push_back(relocation);
    }
    const std::array<std::pair<std::uint64_t, std::uint64_t>, 8> parameter_pointers = {{
        {process_address + 0x38, blocks_address + blocks.offsets[0]},
        {process_address + 0x40, blocks_address + blocks.offsets[1]},
        {process_address + 0x48, blocks_address + blocks.offsets[2]},
        {blocks_address + 0x30, blocks_address + blocks.offsets[3]},
        {blocks_address + 0x38, blocks_address + blocks.offsets[4]},
        {blocks_address + 0x60, blocks_address + blocks.offsets[5]},
        {blocks_address + 0x10, blocks_address + blocks.heap_size},
        {blocks_address + 0x20, blocks_address + blocks.heap_extended},
    }};
    std::vector<DynamicRelocation> relocations;
    for (const auto &[offset, addend] : parameter_pointers)
        relocations.push_back({offset, kRelRelative, addend});
    relocations.insert(relocations.end(), relative.begin(), relative.end());
    const std::size_t relative_count = relocations.size();
    relocations.insert(relocations.end(), symbolic.begin(), symbolic.end());
    const Bytes rela_dynamic = write_relocations(relocations);
    const Section *source_plt = optional_section(image, ".rela.plt");
    const Bytes rela_plt = source_plt == nullptr ? Bytes{} : source_plt->data;
    const Bytes note = build_note();

    data_end = std::max(data_end, data_start + 8);
    const std::uint64_t dynamic_base = align_up(data_end, 16);
    const std::uint64_t string_address = dynamic_base;
    const std::uint64_t symbol_address = align_up(string_address + dynamic_strings.size(), 8);
    const std::uint64_t jump_address = align_up(symbol_address + dynamic_symbols.size(), 8);
    const std::uint64_t relocation_address = align_up(jump_address + rela_plt.size(), 8);
    const std::uint64_t hash_address = align_up(relocation_address + rela_dynamic.size(), 8);
    const std::uint64_t note_address = align_up(hash_address + hash.size(), 4);
    const std::uint64_t dynamic_address = align_up(note_address + note.size(), 8);

    const Bytes dynamic = build_dynamic(
        modules, libraries, module_name_offset, file_name_offset, symbol_address, string_address,
        dynamic_strings.size(), hash_address, hash.size(), jump_address, rela_plt.size(),
        got.address, dynamic_symbols.size(), relocation_address, rela_dynamic.size(),
        relative_count, dynamic_value(dynamic_source, kDynamicPreinitArray),
        dynamic_value(dynamic_source, kDynamicPreinitArraySize),
        dynamic_value(dynamic_source, kDynamicInitArray),
        dynamic_value(dynamic_source, kDynamicInitArraySize),
        dynamic_value(dynamic_source, kDynamicFiniArray),
        dynamic_value(dynamic_source, kDynamicFiniArraySize),
        dynamic_value(dynamic_source, kDynamicInit), dynamic_value(dynamic_source, kDynamicFini));
    const std::uint64_t dynamic_end = dynamic_address + dynamic.size();

    std::vector<std::string> components = options.version_components;
    if (components.empty())
    {
        components.push_back("app_crt.o");
        for (const Stub *provider : module_order)
            components.push_back(provider->soname);
    }
    const Bytes comment = build_comment(options.file_name);
    const Bytes version = build_version(components, options.module_sdk);
    const Bytes tail_note = build_tail_note();

    const std::uint64_t dynamic_file = congruent_offset(copied_file_end, dynamic_base);
    const auto dynamic_file_at = [&](std::uint64_t address)
    { return dynamic_file + (address - dynamic_base); };
    const std::uint64_t comment_file = align_up(dynamic_file_at(dynamic_end), 8);
    const std::uint64_t version_file = align_up(comment_file + comment.size(), 4);
    const std::uint64_t tail_file = align_up(version_file + version.size(), 4);
    Bytes output(tail_file + tail_note.size());

    for (const Section &input : image.sections)
    {
        if (!input.allocated() || input.no_bits() || metadata_section(input.name) ||
            input.name == ".dynamic")
            continue;
        copy_bytes(output, input.file_offset, input.data);
    }
    // The RELRO segment starts at its lowest-addressed section. Sections are
    // copied at their LLVM file offsets, which lld keeps page-congruent with
    // their addresses, so the segment's file offset comes from that same
    // section. Using the GOT's offset broke once .data.rel.ro (relocated
    // constants) preceded the GOT: the segment became va 0x10000 at file
    // 0x14090 and the PS5 loader rejected it as not page aligned.
    const Section *relro_first = nullptr;
    for (const Section &input : image.sections)
    {
        if (input.allocated() && relro_section(input.name) &&
            (relro_first == nullptr || input.address < relro_first->address))
            relro_first = &input;
    }
    require(relro_first != nullptr && relro_first->address == relro_start,
            "RELRO start section does not match the RELRO region");
    const std::uint64_t relro_file = relro_first->file_offset;
    copy_bytes(output, relro_file + process_address - relro_start, process_parameters);
    copy_bytes(output, relro_file + blocks_address - relro_start, blocks.data);
    copy_bytes(output, dynamic_file_at(string_address), dynamic_strings);
    copy_bytes(output, dynamic_file_at(symbol_address), dynamic_symbols);
    copy_bytes(output, dynamic_file_at(jump_address), rela_plt);
    copy_bytes(output, dynamic_file_at(relocation_address), rela_dynamic);
    copy_bytes(output, dynamic_file_at(hash_address), hash);
    copy_bytes(output, dynamic_file_at(note_address), note);
    copy_bytes(output, dynamic_file_at(dynamic_address), dynamic);
    copy_bytes(output, comment_file, comment);
    copy_bytes(output, version_file, version);
    copy_bytes(output, tail_file, tail_note);

    write_elf_header(output, image.entry);
    const std::uint64_t ro_file =
        eh_header != nullptr ? eh_header->file_offset : section(image, ".eh_frame").file_offset;
    const std::uint64_t data_file = dynamic_source.file_offset;
    const std::uint64_t data_file_size = std::max<std::uint64_t>(8, data_stored_end - data_start);
    const std::uint64_t tls_file = tls_start == 0 ? relro_file + (relro_end - relro_start)
                                                  : relro_file + (tls_start - relro_start);
    const std::uint64_t eh_address = eh_header == nullptr ? ro_start : eh_header->address;
    const std::uint64_t eh_file = eh_header == nullptr ? ro_file : eh_header->file_offset;
    const std::uint64_t eh_size = eh_header == nullptr ? 0 : eh_header->size;
    const std::array<ProgramHeader, 14> headers = {{
        {kProgramLoad, kFlagExecute, text.file_offset, 0, text_end, text_end, kPage},
        {kProgramLoad, kFlagRead, ro_file, ro_start, ro_end - ro_start, ro_end - ro_start, kPage},
        {kProgramLoad, kFlagRead | kFlagWrite, relro_file, relro_start, relro_end - relro_start,
         relro_end - relro_start, kPage},
        {kProgramGnuRelro, kFlagRead, relro_file, relro_start, relro_end - relro_start,
         align_up(relro_end - relro_start, kPage), 1},
        {kProgramLoad, kFlagRead | kFlagWrite, data_file, data_start, data_file_size,
         data_end - data_start, kPage},
        {kProgramProcParam, kFlagRead, relro_file + process_address - relro_start, process_address,
         process_parameters.size(), process_parameters.size(), 8},
        {kProgramDynamic, kFlagRead | kFlagWrite, dynamic_file_at(dynamic_address), dynamic_address,
         dynamic.size(), dynamic.size(), 8},
        {kProgramTls, kFlagRead, tls_start == 0 ? relro_file : tls_file,
         tls_start == 0 ? relro_start : tls_start, tls_start == 0 ? 0 : tls_file_end - tls_start,
         tls_start == 0 ? 0 : tls_memory_end - tls_start, tls_start == 0 ? 1 : tls_alignment},
        {kProgramGnuEhFrame, kFlagRead, eh_file, eh_address, eh_size, eh_size, 4},
        {kProgramLoad, 0, dynamic_file, dynamic_base, dynamic_end - dynamic_base,
         dynamic_end - dynamic_base, kPage},
        {kProgramComment, 0, comment_file, 0, comment.size(), 0, 0x10},
        {kProgramVersion, 0, version_file, 0, version.size(), version.size(), 1},
        {kProgramNote, 0, dynamic_file_at(note_address), note_address, note.size(), note.size(), 4},
        {kProgramNote, 0, tail_file, 0, tail_note.size(), 0, 4},
    }};
    // The PS5 loader rejects any PT_LOAD whose file offset and address differ
    // modulo its alignment; fail the host build instead of the console launch.
    for (const ProgramHeader &header : headers)
    {
        if (header.type == kProgramLoad && header.alignment > 1)
            require(header.offset % header.alignment == header.address % header.alignment,
                    "PT_LOAD at address " + std::to_string(header.address) +
                        " is not page aligned with file offset " + std::to_string(header.offset));
    }
    for (std::size_t i = 0; i < headers.size(); ++i)
        write_program_header(output, i, headers[i]);

    const auto build_id = crypto::sha1(output);
    std::copy(build_id.begin(), build_id.begin() + 16,
              output.begin() + dynamic_file_at(note_address) + 16);
    std::copy(build_id.begin(), build_id.begin() + 8, output.begin() + tail_file + 16);
    return output;
}

} // namespace ps5::module
