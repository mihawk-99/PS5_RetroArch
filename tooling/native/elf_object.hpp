/*
 * ps5-native-app-boilerplate - ELF object model and reader.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Defines the compact ELF64 structures used by the native PS5 module writer.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace ps5::elf
{

using Bytes = std::vector<std::uint8_t>;

inline constexpr std::uint32_t kSectionProgBits = 1;
inline constexpr std::uint32_t kSectionSymbolTable = 2;
inline constexpr std::uint32_t kSectionStringTable = 3;
inline constexpr std::uint32_t kSectionRela = 4;
inline constexpr std::uint32_t kSectionDynamic = 6;
inline constexpr std::uint32_t kSectionNoBits = 8;
inline constexpr std::uint32_t kSectionDynamicSymbols = 11;

inline constexpr std::uint64_t kFlagWrite = 0x1;
inline constexpr std::uint64_t kFlagAlloc = 0x2;
inline constexpr std::uint64_t kFlagExecute = 0x4;
inline constexpr std::uint64_t kFlagTls = 0x400;

inline constexpr int kBindLocal = 0;
inline constexpr int kBindGlobal = 1;
inline constexpr int kBindWeak = 2;
inline constexpr int kTypeNoType = 0;
inline constexpr int kTypeObject = 1;
inline constexpr int kTypeFunction = 2;
inline constexpr int kTypeSection = 3;
inline constexpr int kTypeTls = 6;
inline constexpr int kTypeGnuIfunc = 10;

struct Section
{
    std::string name;
    std::uint32_t type{};
    std::uint64_t flags{};
    std::uint64_t address{};
    std::uint64_t file_offset{};
    std::uint64_t size{};
    std::uint32_t link{};
    std::uint32_t info{};
    std::uint64_t alignment{1};
    std::uint64_t entry_size{};
    Bytes data;

    [[nodiscard]] bool allocated() const
    {
        return (flags & kFlagAlloc) != 0;
    }
    [[nodiscard]] bool executable() const
    {
        return (flags & kFlagExecute) != 0;
    }
    [[nodiscard]] bool writable() const
    {
        return (flags & kFlagWrite) != 0;
    }
    [[nodiscard]] bool tls() const
    {
        return (flags & kFlagTls) != 0;
    }
    [[nodiscard]] bool no_bits() const
    {
        return type == kSectionNoBits;
    }
};

struct Symbol
{
    std::string name;
    std::uint8_t info{};
    std::uint8_t other{};
    std::uint16_t section{};
    std::uint64_t value{};
    std::uint64_t size{};

    [[nodiscard]] int binding() const
    {
        return info >> 4;
    }
    [[nodiscard]] int type() const
    {
        return info & 0xf;
    }
    [[nodiscard]] bool undefined() const
    {
        return section == 0;
    }
    [[nodiscard]] bool weak() const
    {
        return binding() == kBindWeak;
    }
    [[nodiscard]] bool global_or_weak() const
    {
        return binding() == kBindGlobal || binding() == kBindWeak;
    }
};

struct Relocation
{
    std::uint64_t offset{};
    std::uint32_t symbol{};
    std::uint32_t type{};
    std::int64_t addend{};
};

struct Object
{
    std::string origin;
    std::vector<Section> sections;
    std::vector<Symbol> symbols;
    std::map<std::size_t, std::vector<Relocation>> relocations;
};

struct Image
{
    std::string origin;
    std::uint64_t entry{};
    std::vector<Section> sections;
    std::vector<Symbol> dynamic_symbols;
    std::vector<std::string> needed;
};

struct Stub
{
    std::string soname;
    std::string module_name;
    std::string library_name;
    std::vector<std::string> exports;
};

[[nodiscard]] Bytes read_file(const std::filesystem::path &path);
[[nodiscard]] Object read_object(std::span<const std::uint8_t> data, std::string origin);
[[nodiscard]] Image read_image(std::span<const std::uint8_t> data, std::string origin);
[[nodiscard]] Stub read_stub(std::span<const std::uint8_t> data, std::string origin);

} // namespace ps5::elf
