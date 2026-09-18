/*
 * ps5-native-app-boilerplate - Native FSELF container implementation.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Produces and reads deterministic plaintext signed-executable containers.
 * The implementation follows the documented container geometry used by the
 * project and contains no proprietary keys, signatures, or runtime code.
 */

#include "self_container.hpp"

#include "hash.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <zlib.h>

namespace ps5::self
{
namespace
{

constexpr std::size_t kContainerHeaderSize = 0x20;
constexpr std::size_t kSegmentEntrySize = 0x20;
constexpr std::size_t kExtendedInfoSize = 0x40;
constexpr std::size_t kControlRegionSize = 0x30;
constexpr std::size_t kMetadataBlockSize = 0x50;
constexpr std::size_t kMetadataFooterSize = 0x50;
constexpr std::size_t kSignatureSize = 0x200;
constexpr std::size_t kAlternateSignatureSize = 0x100;
constexpr std::size_t kSegmentBlockSize = 0x4000;
constexpr std::size_t kDigestSlotSize = 0x20;
constexpr std::size_t kFooterMarkerOffset = 0x30;
constexpr std::uint32_t kDefaultProgramType = 0x101;

constexpr std::size_t kElfHeaderSize = 0x40;
constexpr std::size_t kElfProgramHeaderSize = 0x38;
constexpr std::size_t kOsAbiOffset = 0x07;
constexpr std::size_t kTypeOffset = 0x10;
constexpr std::size_t kMachineOffset = 0x12;
constexpr std::size_t kProgramHeaderOffset = 0x20;
constexpr std::size_t kProgramHeaderEntrySizeOffset = 0x36;
constexpr std::size_t kProgramHeaderCountOffset = 0x38;

constexpr std::uint32_t kPtLoad = 0x00000001;
constexpr std::uint32_t kPtDynamic = 0x00000002;
constexpr std::uint32_t kPtDynlibData = 0x61000000;
constexpr std::uint32_t kPtProcParam = 0x61000001;
constexpr std::uint32_t kPtRelro = 0x61000010;
constexpr std::uint32_t kPtComment = 0x6fffff00;
constexpr std::uint32_t kPtVersionRecords = 0x6fffff01;

void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

bool range_in_bounds(std::uint64_t offset, std::uint64_t size, std::uint64_t length)
{
    return offset <= length && size <= length - offset;
}

std::uint16_t read_u16(std::span<const std::uint8_t> data, std::size_t at)
{
    require(at + 2 <= data.size(), "read_u16 outside input");
    return static_cast<std::uint16_t>(data[at]) | static_cast<std::uint16_t>(data[at + 1]) << 8;
}

std::uint32_t read_u32(std::span<const std::uint8_t> data, std::size_t at)
{
    require(at + 4 <= data.size(), "read_u32 outside input");
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i)
        value |= static_cast<std::uint32_t>(data[at + i]) << (i * 8);
    return value;
}

std::uint64_t read_u64(std::span<const std::uint8_t> data, std::size_t at)
{
    require(at + 8 <= data.size(), "read_u64 outside input");
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i)
        value |= static_cast<std::uint64_t>(data[at + i]) << (i * 8);
    return value;
}

void write_u16(std::span<std::uint8_t> data, std::size_t at, std::uint16_t value)
{
    require(at + 2 <= data.size(), "write_u16 outside output");
    data[at] = static_cast<std::uint8_t>(value);
    data[at + 1] = static_cast<std::uint8_t>(value >> 8);
}

void write_u32(std::span<std::uint8_t> data, std::size_t at, std::uint32_t value)
{
    require(at + 4 <= data.size(), "write_u32 outside output");
    for (std::size_t i = 0; i < 4; ++i)
        data[at + i] = static_cast<std::uint8_t>(value >> (i * 8));
}

void write_u64(std::span<std::uint8_t> data, std::size_t at, std::uint64_t value)
{
    require(at + 8 <= data.size(), "write_u64 outside output");
    for (std::size_t i = 0; i < 8; ++i)
        data[at + i] = static_cast<std::uint8_t>(value >> (i * 8));
}

void copy_bytes(std::span<std::uint8_t> target, std::size_t at,
                std::span<const std::uint8_t> source)
{
    require(at <= target.size() && source.size() <= target.size() - at, "copy outside output");
    std::copy(source.begin(), source.end(), target.begin() + at);
}

std::size_t align_up(std::size_t value, std::size_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

std::size_t metadata_size(std::size_t segment_count, std::uint32_t magic)
{
    return segment_count * kMetadataBlockSize + kMetadataFooterSize +
           (magic == kAlternateMagic ? kAlternateSignatureSize : kSignatureSize);
}

std::size_t digest_size(std::size_t data_size)
{
    return ((data_size + kSegmentBlockSize - 1) / kSegmentBlockSize) * kDigestSlotSize;
}

struct FileRegion
{
    std::size_t offset{};
    std::size_t size{};
};

FileRegion find_version_records(std::span<const std::uint8_t> elf, std::size_t program_count,
                                std::uint64_t limit)
{
    for (std::size_t i = 0; i < program_count; ++i)
    {
        const std::size_t header = kElfHeaderSize + i * kElfProgramHeaderSize;
        if (read_u32(elf, header) != kPtVersionRecords)
            continue;
        const std::uint64_t offset = read_u64(elf, header + 0x08);
        const std::uint64_t size = read_u64(elf, header + 0x20);
        if (size == 0 || !range_in_bounds(offset, size, limit) ||
            offset > std::numeric_limits<std::size_t>::max() ||
            size > std::numeric_limits<std::size_t>::max())
            return {};
        return {static_cast<std::size_t>(offset), static_cast<std::size_t>(size)};
    }
    return {};
}

Bytes inflate_segment(std::span<const std::uint8_t> input, std::size_t expected, int window_bits)
{
    Bytes output(expected);
    z_stream stream{};
    stream.next_in = const_cast<Bytef *>(reinterpret_cast<const Bytef *>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = reinterpret_cast<Bytef *>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());
    if (inflateInit2(&stream, window_bits) != Z_OK)
        return {};
    const int result = ::inflate(&stream, Z_FINISH);
    const bool valid = result == Z_STREAM_END && stream.total_out == expected;
    inflateEnd(&stream);
    return valid ? output : Bytes{};
}

Bytes inflate_segment(std::span<const std::uint8_t> input, std::size_t expected)
{
    if (expected == 0)
        return {};
    if (input.size() > std::numeric_limits<uInt>::max() ||
        expected > std::numeric_limits<uInt>::max())
        throw std::runtime_error("compressed segment is too large");
    Bytes output = inflate_segment(input, expected, MAX_WBITS);
    if (output.size() == expected)
        return output;
    output = inflate_segment(input, expected, -MAX_WBITS);
    require(output.size() == expected, "compressed segment did not inflate to its recorded size");
    return output;
}

void write_segment(std::span<std::uint8_t> output, std::size_t at, std::uint64_t flags,
                   std::uint64_t offset, std::uint64_t file_size, std::uint64_t memory_size)
{
    write_u64(output, at, flags);
    write_u64(output, at + 0x08, offset);
    write_u64(output, at + 0x10, file_size);
    write_u64(output, at + 0x18, memory_size);
}

struct SelectedSegment
{
    std::size_t header_index{};
    std::size_t file_offset{};
    std::size_t file_size{};
};

std::vector<SelectedSegment> select_segments(std::span<const std::uint8_t> elf,
                                             std::size_t header_offset, std::size_t header_count,
                                             bool include_proc_param)
{
    std::vector<SelectedSegment> selected;
    for (std::size_t i = 0; i < header_count; ++i)
    {
        const std::size_t at = header_offset + i * kElfProgramHeaderSize;
        const std::uint32_t type = read_u32(elf, at);
        const std::uint32_t flags = read_u32(elf, at + 4);
        const std::uint64_t offset = read_u64(elf, at + 0x08);
        const std::uint64_t size = read_u64(elf, at + 0x20);
        if (size == 0)
        {
            require(type != kPtLoad || flags == 0, "a mapped program header stores no bytes");
            continue;
        }
        if (type != kPtLoad && type != kPtDynlibData && type != kPtRelro && type != kPtComment &&
            (type != kPtProcParam || !include_proc_param))
            continue;
        require(range_in_bounds(offset, size, elf.size()),
                "a container segment reaches past the input ELF");
        require(offset <= std::numeric_limits<std::size_t>::max() &&
                    size <= std::numeric_limits<std::size_t>::max(),
                "a container segment is too large for this host");
        selected.push_back({i, static_cast<std::size_t>(offset), static_cast<std::size_t>(size)});
    }
    return selected;
}

void normalize_header(Bytes &elf)
{
    write_u16(elf, kMachineOffset, 0x3e);
    if (elf[kOsAbiOffset] == 0 || elf[kOsAbiOffset] == 3)
        elf[kOsAbiOffset] = 9;
    if (read_u16(elf, kTypeOffset) == 0)
        write_u16(elf, kTypeOffset, 2);
}

} // namespace

bool is_elf(std::span<const std::uint8_t> data)
{
    return data.size() >= kElfHeaderSize && read_u32(data, 0) == 0x464c457f;
}

bool is_self(std::span<const std::uint8_t> data)
{
    if (data.size() < kContainerHeaderSize)
        return false;
    const std::uint32_t magic = read_u32(data, 0);
    return magic == kMagic || magic == kAlternateMagic;
}

Image parse(std::span<const std::uint8_t> data)
{
    require(is_self(data), "input is not a signed-executable container");
    Image image;
    image.program_type = read_u32(data, 0x08);
    image.header_size = read_u16(data, 0x0c);
    image.metadata_size = read_u16(data, 0x0e);
    image.file_size = read_u64(data, 0x10);
    const std::size_t segment_count = read_u16(data, 0x18);
    const std::size_t table_end = kContainerHeaderSize + segment_count * kSegmentEntrySize;
    require(table_end <= data.size(), "container segment table overruns input");
    require(image.header_size <= data.size(), "container header overruns input");

    image.segments.reserve(segment_count);
    for (std::size_t i = 0; i < segment_count; ++i)
    {
        const std::size_t at = kContainerHeaderSize + i * kSegmentEntrySize;
        image.segments.push_back({read_u64(data, at), read_u64(data, at + 0x08),
                                  read_u64(data, at + 0x10), read_u64(data, at + 0x18)});
    }

    require(table_end + kElfHeaderSize <= data.size() && is_elf(data.subspan(table_end)),
            "container has no readable ELF header");
    const std::size_t program_count = read_u16(data, table_end + kProgramHeaderCountOffset);
    const std::size_t elf_headers_size = kElfHeaderSize + program_count * kElfProgramHeaderSize;
    require(table_end + elf_headers_size <= data.size(),
            "container ELF header table overruns input");
    image.elf_headers.assign(data.begin() + table_end, data.begin() + table_end + elf_headers_size);

    const std::size_t extended_at = align_up(table_end + elf_headers_size, 0x10);
    if (extended_at + kExtendedInfoSize <= image.header_size &&
        extended_at + kExtendedInfoSize <= data.size())
    {
        image.has_extended_info = true;
        image.authority = read_u64(data, extended_at);
        image.extended_program_type = read_u64(data, extended_at + 0x08);
        image.app_version = read_u64(data, extended_at + 0x10);
        image.firmware_version = read_u64(data, extended_at + 0x18);
        std::copy_n(data.begin() + extended_at + 0x20, image.digest.size(), image.digest.begin());
    }
    return image;
}

Bytes extract(std::span<const std::uint8_t> data)
{
    const Image image = parse(data);
    const std::span<const std::uint8_t> headers = image.elf_headers;
    const std::size_t program_count = read_u16(headers, kProgramHeaderCountOffset);
    const std::size_t program_table_end = kElfHeaderSize + program_count * kElfProgramHeaderSize;
    require(program_table_end <= headers.size(), "stored ELF program-header table is incomplete");

    std::uint64_t output_size = headers.size();
    for (std::size_t i = 0; i < program_count; ++i)
    {
        const std::size_t at = kElfHeaderSize + i * kElfProgramHeaderSize;
        const std::uint64_t offset = read_u64(headers, at + 0x08);
        const std::uint64_t size = read_u64(headers, at + 0x20);
        require(offset <= std::numeric_limits<std::size_t>::max() &&
                    size <= std::numeric_limits<std::size_t>::max() - offset,
                "ELF program header is too large for this host");
        output_size = std::max(output_size, offset + size);
    }
    require(output_size <= std::numeric_limits<std::size_t>::max(),
            "reconstructed ELF is too large for this host");
    Bytes output(static_cast<std::size_t>(output_size));

    for (const Segment &segment : image.segments)
    {
        if (!segment.blocked())
            continue;
        require(!segment.encrypted(),
                "encrypted retail segment cannot be extracted without its key");
        if (segment.id() >= program_count)
            continue;
        const std::size_t header = kElfHeaderSize + segment.id() * kElfProgramHeaderSize;
        const std::uint64_t output_offset = read_u64(headers, header + 0x08);
        const std::uint64_t output_length = read_u64(headers, header + 0x20);
        require(range_in_bounds(segment.file_offset, segment.file_size, data.size()),
                "container segment overruns input");
        const auto stored = data.subspan(static_cast<std::size_t>(segment.file_offset),
                                         static_cast<std::size_t>(segment.file_size));
        Bytes inflated;
        std::span<const std::uint8_t> payload = stored;
        if (segment.compressed())
        {
            inflated = inflate_segment(stored, static_cast<std::size_t>(output_length));
            payload = inflated;
        }
        const std::size_t copy_size =
            static_cast<std::size_t>(std::min<std::uint64_t>(payload.size(), output_length));
        require(range_in_bounds(output_offset, copy_size, output.size()),
                "container segment is outside reconstructed ELF");
        copy_bytes(output, static_cast<std::size_t>(output_offset), payload.first(copy_size));
    }

    const FileRegion version = find_version_records(headers, program_count, output.size());
    if (version.size != 0)
    {
        std::uint64_t tail = 0;
        for (const Segment &segment : image.segments)
        {
            if (range_in_bounds(segment.file_offset, segment.file_size, data.size()))
                tail = std::max(tail, segment.file_offset + segment.file_size);
        }
        if (range_in_bounds(tail, version.size, data.size()) &&
            range_in_bounds(version.offset, version.size, output.size()))
            copy_bytes(output, version.offset,
                       data.subspan(static_cast<std::size_t>(tail), version.size));
    }
    copy_bytes(output, 0, headers.first(program_table_end));
    return output;
}

Bytes sign(std::span<const std::uint8_t> input, const SignOptions &options)
{
    require(is_elf(input), "input is not an ELF file");
    require(input[4] == 2, "only 64-bit ELF modules are supported");
    require(options.magic == kMagic || options.magic == kAlternateMagic,
            "unsupported signed-container magic");
    require(options.auth_info.empty() || options.auth_info.size() == 0x88,
            "authentication info must be exactly 0x88 bytes");

    Bytes elf(input.begin(), input.end());
    if (options.normalize_header)
        normalize_header(elf);
    const std::size_t program_offset = read_u64(elf, kProgramHeaderOffset);
    const std::size_t program_entry_size = read_u16(elf, kProgramHeaderEntrySizeOffset);
    const std::size_t program_count = read_u16(elf, kProgramHeaderCountOffset);
    require(program_entry_size == kElfProgramHeaderSize,
            "unexpected ELF program-header entry size");
    require(program_offset == kElfHeaderSize,
            "ELF program-header table must immediately follow the ELF header");
    require(program_count <= (elf.size() - program_offset) / kElfProgramHeaderSize,
            "ELF program-header table overruns input");

    const auto selected =
        select_segments(elf, program_offset, program_count, options.include_proc_param);
    require(!selected.empty(), "ELF has no loadable segment content");
    const std::size_t segment_count = selected.size() * 2;
    const std::size_t after_segments = kContainerHeaderSize + segment_count * kSegmentEntrySize;
    const std::size_t elf_headers_size = kElfHeaderSize + program_count * kElfProgramHeaderSize;
    const std::size_t extended_at = align_up(after_segments + elf_headers_size, 0x10);
    const std::size_t header_size = extended_at + kExtendedInfoSize + kControlRegionSize;
    const std::size_t meta_size = metadata_size(segment_count, options.magic);
    require(header_size <= std::numeric_limits<std::uint16_t>::max() &&
                meta_size <= std::numeric_limits<std::uint16_t>::max(),
            "container header or metadata exceeds its 16-bit field");

    std::vector<std::size_t> offsets(segment_count);
    std::vector<std::size_t> digest_sizes(selected.size());
    std::size_t cursor = header_size + meta_size;
    for (std::size_t i = 0; i < selected.size(); ++i)
    {
        digest_sizes[i] = digest_size(selected[i].file_size);
        offsets[i * 2] = cursor;
        cursor += digest_sizes[i];
        offsets[i * 2 + 1] = cursor;
        cursor = align_up(cursor + selected[i].file_size, 0x10);
    }
    const std::size_t declared_file_size = cursor;
    const std::size_t version_start = offsets.back() + selected.back().file_size;
    const FileRegion version = find_version_records(elf, program_count, elf.size());
    Bytes output(std::max(declared_file_size, version_start + version.size));

    write_u32(output, 0, options.magic);
    output[0x04] = 0;
    output[0x05] = 1;
    output[0x06] = 1;
    output[0x07] = 0x12;
    write_u32(output, 0x08, kDefaultProgramType);
    write_u16(output, 0x0c, static_cast<std::uint16_t>(header_size));
    write_u16(output, 0x0e, static_cast<std::uint16_t>(meta_size));
    write_u64(output, 0x10, declared_file_size);
    write_u16(output, 0x18, static_cast<std::uint16_t>(segment_count));
    write_u16(output, 0x1a, 0x22);

    for (std::size_t i = 0; i < selected.size(); ++i)
    {
        const std::size_t digest_entry = kContainerHeaderSize + (i * 2) * kSegmentEntrySize;
        const std::size_t data_entry = digest_entry + kSegmentEntrySize;
        const std::uint64_t digest_flags = ((i * 2 + 1) << 20) | 0x10004;
        const std::uint64_t data_flags = (selected[i].header_index << 20) | 0x2804;
        write_segment(output, digest_entry, digest_flags, offsets[i * 2], digest_sizes[i],
                      digest_sizes[i]);
        write_segment(output, data_entry, data_flags, offsets[i * 2 + 1], selected[i].file_size,
                      selected[i].file_size);
    }

    copy_bytes(output, after_segments, std::span<const std::uint8_t>{elf}.first(elf_headers_size));
    write_u64(output, extended_at, options.authority);
    write_u64(output, extended_at + 0x08, 1);
    write_u64(output, extended_at + 0x10, options.app_version);
    write_u64(output, extended_at + 0x18, options.firmware_version);
    write_u64(output, extended_at + kExtendedInfoSize, 3);

    const std::size_t footer = header_size + segment_count * kMetadataBlockSize;
    write_u32(output, footer + kFooterMarkerOffset, 0x10000);
    if (!options.auth_info.empty())
    {
        const std::size_t signature = footer + kMetadataFooterSize;
        write_u64(output, signature, 0x88);
        write_u64(output, signature + 0x08, options.authority);
        copy_bytes(output, signature + 0x10,
                   std::span<const std::uint8_t>{options.auth_info}.subspan(0x08));
    }

    for (std::size_t i = 0; i < selected.size(); ++i)
        copy_bytes(output, offsets[i * 2 + 1],
                   std::span<const std::uint8_t>{elf}.subspan(selected[i].file_offset,
                                                              selected[i].file_size));
    if (version.size != 0)
        copy_bytes(output, version_start,
                   std::span<const std::uint8_t>{elf}.subspan(version.offset, version.size));

    const Bytes reconstructed = extract(output);
    const auto digest = crypto::sha256(reconstructed);
    copy_bytes(output, extended_at + 0x20, digest);
    return output;
}

Bytes strip_sections(std::span<const std::uint8_t> elf)
{
    require(is_elf(elf), "input is not an ELF file");
    const std::uint64_t program_offset = read_u64(elf, kProgramHeaderOffset);
    const std::size_t program_entry_size = read_u16(elf, kProgramHeaderEntrySizeOffset);
    const std::size_t program_count = read_u16(elf, kProgramHeaderCountOffset);
    require(program_entry_size >= kElfProgramHeaderSize, "ELF program-header entry is too small");
    std::uint64_t extent = std::max<std::uint64_t>(
        kElfHeaderSize, program_offset + program_count * program_entry_size);
    bool has_dynamic = false;
    for (std::size_t i = 0; i < program_count; ++i)
    {
        const std::uint64_t at = program_offset + i * program_entry_size;
        if (!range_in_bounds(at, kElfProgramHeaderSize, elf.size()))
            break;
        const std::uint32_t type = read_u32(elf, static_cast<std::size_t>(at));
        const std::uint64_t offset = read_u64(elf, static_cast<std::size_t>(at) + 0x08);
        const std::uint64_t size = read_u64(elf, static_cast<std::size_t>(at) + 0x20);
        has_dynamic |= type == kPtDynamic;
        if (offset <= std::numeric_limits<std::uint64_t>::max() - size)
            extent = std::max(extent, offset + size);
    }
    require(has_dynamic, "only an ELF with a dynamic segment can be stripped");
    extent = std::min<std::uint64_t>(extent, elf.size());
    Bytes output(elf.begin(), elf.begin() + static_cast<std::size_t>(extent));
    write_u64(output, 0x28, 0);
    write_u16(output, 0x3a, 0);
    write_u16(output, 0x3c, 0);
    write_u16(output, 0x3e, 0);
    return output;
}

} // namespace ps5::self
