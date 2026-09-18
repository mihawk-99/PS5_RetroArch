/*
 * ps5-native-app-boilerplate - Native host build utility.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Provides C++ implementations of the repository's host-side build steps.
 * Handles deterministic FSELF containers and converts LLVM-linked PIE files
 * into PS5 dynamic executables without a managed-code toolchain.
 */

#include "hash.hpp"
#include "elf_object.hpp"
#include "sce_module_writer.hpp"
#include "self_container.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{

using Bytes = std::vector<std::uint8_t>;

Bytes read_file(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        throw std::runtime_error("cannot open input: " + path.string());
    const std::streamsize size = input.tellg();
    if (size < 0)
        throw std::runtime_error("cannot size input: " + path.string());
    Bytes data(static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char *>(data.data()), size);
    if (!input)
        throw std::runtime_error("cannot read input: " + path.string());
    return data;
}

void write_file(const std::filesystem::path &path, std::span<const std::uint8_t> data)
{
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        throw std::runtime_error("cannot create output: " + path.string());
    output.write(reinterpret_cast<const char *>(data.data()),
                 static_cast<std::streamsize>(data.size()));
    if (!output)
        throw std::runtime_error("cannot write output: " + path.string());
}

bool has_flag(std::span<char *> args, std::string_view name)
{
    return std::any_of(args.begin(), args.end(),
                       [&](const char *value) { return std::string_view{value} == name; });
}

std::optional<std::string_view> option(std::span<char *> args, std::string_view name)
{
    for (std::size_t i = 0; i + 1 < args.size(); ++i)
        if (std::string_view{args[i]} == name)
            return args[i + 1];
    return std::nullopt;
}

std::vector<std::string_view> options(std::span<char *> args, std::string_view name)
{
    std::vector<std::string_view> values;
    for (std::size_t i = 0; i + 1 < args.size(); ++i)
        if (std::string_view{args[i]} == name)
            values.emplace_back(args[i + 1]);
    return values;
}

std::uint64_t parse_integer(std::string_view value)
{
    int base = 10;
    if (value.starts_with("0x") || value.starts_with("0X"))
    {
        value.remove_prefix(2);
        base = 16;
    }
    std::uint64_t result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result, base);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        throw std::runtime_error("invalid integer option");
    return result;
}

Bytes parse_hex(std::string_view value)
{
    if ((value.size() & 1) != 0)
        throw std::runtime_error("hex value has odd length");
    Bytes output(value.size() / 2);
    for (std::size_t i = 0; i < output.size(); ++i)
    {
        unsigned integer{};
        const auto first = value.data() + i * 2;
        const auto parsed = std::from_chars(first, first + 2, integer, 16);
        if (parsed.ec != std::errc{} || parsed.ptr != first + 2)
            throw std::runtime_error("invalid hexadecimal value");
        output[i] = static_cast<std::uint8_t>(integer);
    }
    return output;
}

std::string hex(std::span<const std::uint8_t> data)
{
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (std::uint8_t value : data)
        output << std::setw(2) << unsigned(value);
    return output.str();
}

int self_command(std::span<char *> args)
{
    auto input_name = option(args, "--file");
    if (!input_name)
        input_name = option(args, "--in");
    if (!input_name)
        throw std::runtime_error("self requires --file or --in");
    const std::filesystem::path input_path{std::string{*input_name}};
    const Bytes input = read_file(std::filesystem::absolute(input_path));
    const bool do_sign = has_flag(args, "--sign");
    const bool do_extract = has_flag(args, "--extract");

    if (!do_sign && !do_extract)
    {
        std::cout << "file: " << input_path.filename().string() << '\n';
        if (ps5::self::is_self(input))
        {
            const auto image = ps5::self::parse(input);
            bool encrypted = false;
            for (const auto &segment : image.segments)
                encrypted |= segment.encrypted();
            std::cout << "container: signed, " << (encrypted ? "encrypted" : "plaintext") << '\n'
                      << "segments: " << image.segments.size() << '\n';
            if (image.has_extended_info)
            {
                std::cout << "authority: 0x" << std::hex << std::setw(16) << std::setfill('0')
                          << image.authority << '\n'
                          << "program type: 0x" << std::setw(16) << image.extended_program_type
                          << '\n'
                          << "app version: 0x" << std::setw(16) << image.app_version << '\n'
                          << "firmware version: 0x" << std::setw(16) << image.firmware_version
                          << '\n'
                          << "digest: " << hex(image.digest) << '\n';
                if (!encrypted)
                {
                    const auto extracted = ps5::self::extract(input);
                    const auto computed = ps5::crypto::sha256(extracted);
                    std::cout << "integrity: "
                              << (std::equal(computed.begin(), computed.end(), image.digest.begin())
                                      ? "valid"
                                      : "INVALID")
                              << '\n';
                }
            }
        }
        else if (ps5::self::is_elf(input))
        {
            std::cout << "container: unsigned ELF\n";
        }
        else
        {
            throw std::runtime_error("file is neither an ELF nor a signed container");
        }
        return 0;
    }

    const auto output_name = option(args, "--out");
    if (!output_name)
        throw std::runtime_error("self operation requires --out");
    Bytes output;
    if (do_sign)
    {
        if (ps5::self::is_self(input))
        {
            std::cout << "input is already wrapped; left unchanged\n";
            return 0;
        }
        ps5::self::SignOptions options;
        if (const auto value = option(args, "--magic"))
            options.magic = static_cast<std::uint32_t>(parse_integer(*value));
        if (const auto value = option(args, "--app-version"))
            options.app_version = parse_integer(*value);
        if (const auto value = option(args, "--fw-version"))
            options.firmware_version = parse_integer(*value);
        if (const auto value = option(args, "--authority"))
            options.authority = parse_integer(*value);
        if (const auto value = option(args, "--auth-info"))
            options.auth_info = parse_hex(*value);
        options.include_proc_param = has_flag(args, "--include-procparam-segment");
        options.normalize_header = !has_flag(args, "--no-normalize");
        output = ps5::self::sign(input, options);
    }
    else
    {
        output = ps5::self::extract(input);
        if (has_flag(args, "--strip-sections"))
            output = ps5::self::strip_sections(output);
    }

    const auto path = std::filesystem::absolute(std::filesystem::path{std::string{*output_name}});
    write_file(path, output);
    std::cout << "wrote " << output.size() << " bytes: " << path.string() << '\n'
              << "sha256 " << hex(ps5::crypto::sha256(output)) << '\n';
    return 0;
}

int link_command(std::span<char *> args)
{
    const auto input_name = option(args, "--in");
    const auto output_name = option(args, "--out");
    if (!input_name || !output_name)
        throw std::runtime_error("link requires --in and --out");
    const std::filesystem::path input_path{std::string{*input_name}};
    const Bytes input = read_file(std::filesystem::absolute(input_path));
    const auto image = ps5::elf::read_image(input, input_path.string());

    std::vector<ps5::elf::Stub> stubs;
    for (std::string_view name : options(args, "--stub"))
    {
        const std::filesystem::path path{std::string{name}};
        const Bytes bytes = read_file(std::filesystem::absolute(path));
        stubs.push_back(ps5::elf::read_stub(bytes, path.string()));
    }
    if (const auto directory_name = option(args, "--stub-dir"))
    {
        const std::filesystem::path directory{std::string{*directory_name}};
        std::vector<std::filesystem::path> paths;
        for (const auto &entry : std::filesystem::directory_iterator(directory))
            if (entry.is_regular_file() && entry.path().extension() == ".so")
                paths.push_back(entry.path());
        std::sort(paths.begin(), paths.end());
        for (const auto &path : paths)
        {
            const Bytes bytes = read_file(path);
            stubs.push_back(ps5::elf::read_stub(bytes, path.string()));
        }
    }
    if (stubs.empty())
        throw std::runtime_error("link requires at least one --stub");

    ps5::module::Options link_options;
    if (const auto value = option(args, "--file-name"))
        link_options.file_name = *value;
    if (const auto value = option(args, "--module-sdk"))
        link_options.module_sdk = static_cast<std::uint32_t>(parse_integer(*value));
    if (const auto value = option(args, "--companion-sdk"))
        link_options.companion_sdk = static_cast<std::uint32_t>(parse_integer(*value));
    for (std::string_view value : options(args, "--component"))
        link_options.version_components.emplace_back(value);

    const Bytes output = ps5::module::write_executable(image, stubs, link_options);
    const auto path = std::filesystem::absolute(std::filesystem::path{std::string{*output_name}});
    write_file(path, output);
    std::cout << "wrote " << output.size() << " bytes: " << path.string() << '\n'
              << "sha256 " << hex(ps5::crypto::sha256(output)) << '\n';
    return 0;
}

void usage()
{
    std::cerr << "usage:\n"
              << "  ps5-native-tool self --sign --in <elf> --out <fself> [options]\n"
              << "  ps5-native-tool self --extract --file <fself> --out <elf>\n"
              << "  ps5-native-tool self --inspect --file <module>\n"
              << "  ps5-native-tool link --in <llvm-pie> --out <ps5-elf>"
                 " (--stub <sdk-so>... | --stub-dir <sdk-lib>)\n";
}

} // namespace

int main(int argc, char **argv)
{
    try
    {
        if (argc < 2)
        {
            usage();
            return 2;
        }
        const std::span<char *> args{argv + 2, static_cast<std::size_t>(argc - 2)};
        if (std::string_view{argv[1]} == "self")
            return self_command(args);
        if (std::string_view{argv[1]} == "link")
            return link_command(args);
        usage();
        return 2;
    }
    catch (const std::exception &error)
    {
        std::cerr << "error: " << error.what() << '\n';
        return 2;
    }
}
