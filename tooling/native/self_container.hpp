/*
 * ps5-native-app-boilerplate - Native FSELF container interface.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Declares deterministic wrapping, extraction, inspection, and integrity
 * helpers for plaintext PS5 signed-executable containers.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ps5::self
{

using Bytes = std::vector<std::uint8_t>;

inline constexpr std::uint32_t kMagic = 0xEEF51454;
inline constexpr std::uint32_t kAlternateMagic = 0x1D3D154F;
inline constexpr std::uint64_t kDeveloperAuthority = 0x3100000000000002;

struct SignOptions
{
    std::uint32_t magic = kMagic;
    bool include_proc_param = false;
    bool normalize_header = true;
    std::uint64_t app_version = 0;
    std::uint64_t firmware_version = 0;
    std::uint64_t authority = kDeveloperAuthority;
    Bytes auth_info;
};

struct Segment
{
    std::uint64_t flags{};
    std::uint64_t file_offset{};
    std::uint64_t file_size{};
    std::uint64_t memory_size{};

    [[nodiscard]] std::size_t id() const
    {
        return (flags >> 20) & 0xffff;
    }
    [[nodiscard]] bool encrypted() const
    {
        return (flags & 0x2) != 0;
    }
    [[nodiscard]] bool compressed() const
    {
        return (flags & 0x8) != 0;
    }
    [[nodiscard]] bool blocked() const
    {
        return (flags & 0x800) != 0;
    }
};

struct Image
{
    std::uint32_t program_type{};
    std::uint16_t header_size{};
    std::uint16_t metadata_size{};
    std::uint64_t file_size{};
    std::vector<Segment> segments;
    Bytes elf_headers;
    bool has_extended_info{};
    std::uint64_t authority{};
    std::uint64_t extended_program_type{};
    std::uint64_t app_version{};
    std::uint64_t firmware_version{};
    std::array<std::uint8_t, 32> digest{};
};

[[nodiscard]] bool is_elf(std::span<const std::uint8_t> data);
[[nodiscard]] bool is_self(std::span<const std::uint8_t> data);
[[nodiscard]] Image parse(std::span<const std::uint8_t> data);
[[nodiscard]] Bytes extract(std::span<const std::uint8_t> data);
[[nodiscard]] Bytes sign(std::span<const std::uint8_t> elf, const SignOptions &options = {});
[[nodiscard]] Bytes strip_sections(std::span<const std::uint8_t> elf);

} // namespace ps5::self
