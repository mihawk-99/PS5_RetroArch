/*
 * ps5-native-app-boilerplate - ELF64 object and SDK stub reader.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Reads relocatable objects after LLVM has resolved archives and reads public
 * SDK shared stubs to discover symbol providers without a copied catalog.
 */

#include "elf_object.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace ps5::elf
{
namespace
{

void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

bool range(std::uint64_t offset, std::uint64_t size, std::uint64_t length)
{
    return offset <= length && size <= length - offset;
}

std::uint16_t u16(std::span<const std::uint8_t> data, std::size_t at)
{
    require(at + 2 <= data.size(), "ELF u16 read outside input");
    return static_cast<std::uint16_t>(data[at]) | static_cast<std::uint16_t>(data[at + 1]) << 8;
}

std::uint32_t u32(std::span<const std::uint8_t> data, std::size_t at)
{
    require(at + 4 <= data.size(), "ELF u32 read outside input");
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i)
        value |= static_cast<std::uint32_t>(data[at + i]) << (i * 8);
    return value;
}

std::uint64_t u64(std::span<const std::uint8_t> data, std::size_t at)
{
    require(at + 8 <= data.size(), "ELF u64 read outside input");
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i)
        value |= static_cast<std::uint64_t>(data[at + i]) << (i * 8);
    return value;
}

std::string string_at(std::span<const std::uint8_t> table, std::uint64_t at)
{
    require(at < table.size() || (at == 0 && table.empty()), "ELF string offset outside table");
    const std::size_t first = static_cast<std::size_t>(at);
    std::size_t end = first;
    while (end < table.size() && table[end] != 0)
        ++end;
    require(end < table.size(), "unterminated ELF string");
    return {reinterpret_cast<const char *>(table.data() + first), end - first};
}

struct RawSection
{
    std::uint32_t name{};
    std::uint32_t type{};
    std::uint64_t flags{};
    std::uint64_t address{};
    std::uint64_t offset{};
    std::uint64_t size{};
    std::uint32_t link{};
    std::uint32_t info{};
    std::uint64_t alignment{};
    std::uint64_t entry_size{};
};

struct ParsedSections
{
    std::vector<RawSection> raw;
    std::vector<std::string> names;
};

ParsedSections sections(std::span<const std::uint8_t> data)
{
    require(data.size() >= 0x40 && u32(data, 0) == 0x464c457f, "input is not an ELF file");
    require(data[4] == 2 && data[5] == 1, "only little-endian ELF64 is supported");
    const std::uint64_t offset = u64(data, 0x28);
    const std::size_t entry_size = u16(data, 0x3a);
    const std::size_t count = u16(data, 0x3c);
    const std::size_t names_index = u16(data, 0x3e);
    require(entry_size == 0x40, "unexpected ELF64 section-header size");
    require(count != 0 && range(offset, count * entry_size, data.size()),
            "ELF section table overruns input");
    require(names_index < count, "ELF section-name table index is invalid");

    ParsedSections result;
    result.raw.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        const std::size_t at = static_cast<std::size_t>(offset) + i * entry_size;
        result.raw.push_back({u32(data, at), u32(data, at + 4), u64(data, at + 8),
                              u64(data, at + 16), u64(data, at + 24), u64(data, at + 32),
                              u32(data, at + 40), u32(data, at + 44), u64(data, at + 48),
                              u64(data, at + 56)});
    }
    const RawSection &names = result.raw[names_index];
    require(range(names.offset, names.size, data.size()), "ELF section-name table overruns input");
    const auto table =
        data.subspan(static_cast<std::size_t>(names.offset), static_cast<std::size_t>(names.size));
    result.names.reserve(count);
    for (const RawSection &section : result.raw)
        result.names.push_back(string_at(table, section.name));
    return result;
}

std::span<const std::uint8_t> section_data(std::span<const std::uint8_t> data,
                                           const RawSection &section)
{
    if (section.type == kSectionNoBits)
        return {};
    require(range(section.offset, section.size, data.size()), "ELF section overruns input");
    return data.subspan(static_cast<std::size_t>(section.offset),
                        static_cast<std::size_t>(section.size));
}

std::vector<Symbol> read_symbols(std::span<const std::uint8_t> data, const ParsedSections &parsed,
                                 std::size_t symbol_index)
{
    const RawSection &symbols = parsed.raw[symbol_index];
    require(symbols.entry_size == 24 && symbols.link < parsed.raw.size(),
            "unsupported ELF symbol table");
    const auto bytes = section_data(data, symbols);
    const auto strings = section_data(data, parsed.raw[symbols.link]);
    std::vector<Symbol> result;
    result.reserve(bytes.size() / 24);
    for (std::size_t at = 0; at + 24 <= bytes.size(); at += 24)
        result.push_back({string_at(strings, u32(bytes, at)), bytes[at + 4], bytes[at + 5],
                          u16(bytes, at + 6), u64(bytes, at + 8), u64(bytes, at + 16)});
    return result;
}

std::string normalized_soname(std::string name)
{
    constexpr std::string_view suffix = ".sprx";
    if (name.ends_with(suffix))
        name.replace(name.size() - suffix.size(), suffix.size(), ".prx");
    return name;
}

std::string module_from_soname(std::string_view soname)
{
    const std::size_t slash = soname.find_last_of("/\\");
    if (slash != std::string_view::npos)
        soname.remove_prefix(slash + 1);
    const std::size_t dot = soname.find('.');
    return std::string{soname.substr(0, dot)};
}

} // namespace

Bytes read_file(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        throw std::runtime_error("cannot open input: " + path.string());
    const std::streamsize size = input.tellg();
    require(size >= 0, "cannot size input: " + path.string());
    Bytes data(static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char *>(data.data()), size);
    if (!input)
        throw std::runtime_error("cannot read input: " + path.string());
    return data;
}

Object read_object(std::span<const std::uint8_t> data, std::string origin)
{
    require(u16(data, 0x10) == 1, "link input is not a relocatable ELF: " + origin);
    const ParsedSections parsed = sections(data);
    Object object;
    object.origin = std::move(origin);
    object.sections.reserve(parsed.raw.size());
    for (std::size_t i = 0; i < parsed.raw.size(); ++i)
    {
        const RawSection &raw = parsed.raw[i];
        const auto bytes = section_data(data, raw);
        object.sections.push_back({parsed.names[i], raw.type, raw.flags, raw.address, raw.offset,
                                   raw.size, raw.link, raw.info,
                                   std::max<std::uint64_t>(raw.alignment, 1), raw.entry_size,
                                   Bytes{bytes.begin(), bytes.end()}});
    }

    std::size_t symbol_index = parsed.raw.size();
    for (std::size_t i = 0; i < parsed.raw.size(); ++i)
        if (parsed.raw[i].type == kSectionSymbolTable)
        {
            symbol_index = i;
            break;
        }
    require(symbol_index < parsed.raw.size(), "relocatable ELF has no symbol table");
    object.symbols = read_symbols(data, parsed, symbol_index);

    for (std::size_t i = 0; i < parsed.raw.size(); ++i)
    {
        const RawSection &raw = parsed.raw[i];
        if (raw.type != kSectionRela)
            continue;
        require(raw.entry_size == 24 && raw.link == symbol_index && raw.info < parsed.raw.size(),
                "unsupported ELF relocation section");
        const auto bytes = section_data(data, raw);
        auto &output = object.relocations[raw.info];
        output.reserve(bytes.size() / 24);
        for (std::size_t at = 0; at + 24 <= bytes.size(); at += 24)
        {
            const std::uint64_t info = u64(bytes, at + 8);
            output.push_back({u64(bytes, at), static_cast<std::uint32_t>(info >> 32),
                              static_cast<std::uint32_t>(info),
                              static_cast<std::int64_t>(u64(bytes, at + 16))});
        }
    }
    return object;
}

Image read_image(std::span<const std::uint8_t> data, std::string origin)
{
    require(u16(data, 0x10) == 3, "link output is not an ELF shared image: " + origin);
    const ParsedSections parsed = sections(data);
    Image image;
    image.origin = std::move(origin);
    image.entry = u64(data, 0x18);
    image.sections.reserve(parsed.raw.size());
    for (std::size_t i = 0; i < parsed.raw.size(); ++i)
    {
        const RawSection &raw = parsed.raw[i];
        const auto bytes = section_data(data, raw);
        image.sections.push_back({parsed.names[i], raw.type, raw.flags, raw.address, raw.offset,
                                  raw.size, raw.link, raw.info,
                                  std::max<std::uint64_t>(raw.alignment, 1), raw.entry_size,
                                  Bytes{bytes.begin(), bytes.end()}});
        if (raw.type == kSectionDynamicSymbols)
            image.dynamic_symbols = read_symbols(data, parsed, i);
    }
    for (std::size_t i = 0; i < parsed.raw.size(); ++i)
    {
        const RawSection &dynamic = parsed.raw[i];
        if (dynamic.type != kSectionDynamic)
            continue;
        require(dynamic.link < parsed.raw.size() && dynamic.entry_size == 16,
                "linked ELF has an unsupported dynamic table");
        const auto entries = section_data(data, dynamic);
        const auto strings = section_data(data, parsed.raw[dynamic.link]);
        for (std::size_t at = 0; at + 16 <= entries.size(); at += 16)
        {
            const std::uint64_t tag = u64(entries, at);
            if (tag == 0)
                break;
            if (tag == 1)
                image.needed.push_back(normalized_soname(string_at(strings, u64(entries, at + 8))));
        }
    }
    require(!image.dynamic_symbols.empty(),
            "linked ELF has no dynamic symbol table: " + image.origin);
    return image;
}

Stub read_stub(std::span<const std::uint8_t> data, std::string origin)
{
    const ParsedSections parsed = sections(data);
    std::size_t symbols_index = parsed.raw.size();
    std::size_t dynamic_index = parsed.raw.size();
    for (std::size_t i = 0; i < parsed.raw.size(); ++i)
    {
        if (parsed.raw[i].type == kSectionDynamicSymbols)
            symbols_index = i;
        if (parsed.raw[i].type == kSectionDynamic)
            dynamic_index = i;
    }
    require(symbols_index < parsed.raw.size() && dynamic_index < parsed.raw.size(),
            "SDK stub lacks dynamic metadata: " + origin);
    const auto symbols = read_symbols(data, parsed, symbols_index);
    const RawSection &dynamic = parsed.raw[dynamic_index];
    require(dynamic.link < parsed.raw.size() && dynamic.entry_size == 16,
            "SDK stub has unsupported dynamic table: " + origin);
    const auto strings = section_data(data, parsed.raw[dynamic.link]);
    const auto entries = section_data(data, dynamic);
    std::string soname;
    for (std::size_t at = 0; at + 16 <= entries.size(); at += 16)
    {
        const std::uint64_t tag = u64(entries, at);
        if (tag == 0)
            break;
        if (tag == 14)
            soname = string_at(strings, u64(entries, at + 8));
    }
    require(!soname.empty(), "SDK stub has no SONAME: " + origin);

    Stub stub;
    stub.soname = normalized_soname(std::move(soname));
    stub.module_name = module_from_soname(stub.soname);
    stub.library_name = stub.module_name;
    for (const Symbol &symbol : symbols)
        if (!symbol.undefined() && symbol.global_or_weak() && !symbol.name.empty())
            stub.exports.push_back(symbol.name);
    return stub;
}

} // namespace ps5::elf
