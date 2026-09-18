/*
 * ps5-native-app-boilerplate - Native SHA-1 and SHA-256 implementation.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Implements the published SHA-1 and SHA-256 algorithms for deterministic
 * build metadata without requiring a host cryptography library.
 */

#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ps5::crypto
{
namespace detail
{

inline std::vector<std::uint8_t> pad(std::span<const std::uint8_t> input)
{
    std::vector<std::uint8_t> data(input.begin(), input.end());
    const std::uint64_t bit_count = static_cast<std::uint64_t>(data.size()) * 8;
    data.push_back(0x80);
    while ((data.size() % 64) != 56)
        data.push_back(0);
    for (int shift = 56; shift >= 0; shift -= 8)
        data.push_back(static_cast<std::uint8_t>(bit_count >> shift));
    return data;
}

inline std::uint32_t load_be32(const std::uint8_t *value)
{
    return (static_cast<std::uint32_t>(value[0]) << 24) |
           (static_cast<std::uint32_t>(value[1]) << 16) |
           (static_cast<std::uint32_t>(value[2]) << 8) | static_cast<std::uint32_t>(value[3]);
}

template <std::size_t N>
inline std::array<std::uint8_t, N * 4> store_words(const std::array<std::uint32_t, N> &words)
{
    std::array<std::uint8_t, N * 4> result{};
    for (std::size_t i = 0; i < N; ++i)
    {
        result[i * 4] = static_cast<std::uint8_t>(words[i] >> 24);
        result[i * 4 + 1] = static_cast<std::uint8_t>(words[i] >> 16);
        result[i * 4 + 2] = static_cast<std::uint8_t>(words[i] >> 8);
        result[i * 4 + 3] = static_cast<std::uint8_t>(words[i]);
    }
    return result;
}

} // namespace detail

inline std::array<std::uint8_t, 20> sha1(std::span<const std::uint8_t> input)
{
    std::array<std::uint32_t, 5> state{0x67452301U, 0xEFCDAB89U, 0x98BADCFEU, 0x10325476U,
                                       0xC3D2E1F0U};
    const auto data = detail::pad(input);
    for (std::size_t offset = 0; offset < data.size(); offset += 64)
    {
        std::array<std::uint32_t, 80> w{};
        for (std::size_t i = 0; i < 16; ++i)
            w[i] = detail::load_be32(data.data() + offset + i * 4);
        for (std::size_t i = 16; i < w.size(); ++i)
            w[i] = std::rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        auto [a, b, c, d, e] = state;
        for (std::size_t i = 0; i < w.size(); ++i)
        {
            std::uint32_t f;
            std::uint32_t k;
            if (i < 20)
            {
                f = (b & c) | (~b & d);
                k = 0x5A827999U;
            }
            else if (i < 40)
            {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1U;
            }
            else if (i < 60)
            {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCU;
            }
            else
            {
                f = b ^ c ^ d;
                k = 0xCA62C1D6U;
            }
            const std::uint32_t next = std::rotl(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = std::rotl(b, 30);
            b = a;
            a = next;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
    }
    return detail::store_words(state);
}

inline std::array<std::uint8_t, 32> sha256(std::span<const std::uint8_t> input)
{
    constexpr std::array<std::uint32_t, 64> k{
        0x428A2F98U, 0x71374491U, 0xB5C0FBCFU, 0xE9B5DBA5U, 0x3956C25BU, 0x59F111F1U, 0x923F82A4U,
        0xAB1C5ED5U, 0xD807AA98U, 0x12835B01U, 0x243185BEU, 0x550C7DC3U, 0x72BE5D74U, 0x80DEB1FEU,
        0x9BDC06A7U, 0xC19BF174U, 0xE49B69C1U, 0xEFBE4786U, 0x0FC19DC6U, 0x240CA1CCU, 0x2DE92C6FU,
        0x4A7484AAU, 0x5CB0A9DCU, 0x76F988DAU, 0x983E5152U, 0xA831C66DU, 0xB00327C8U, 0xBF597FC7U,
        0xC6E00BF3U, 0xD5A79147U, 0x06CA6351U, 0x14292967U, 0x27B70A85U, 0x2E1B2138U, 0x4D2C6DFCU,
        0x53380D13U, 0x650A7354U, 0x766A0ABBU, 0x81C2C92EU, 0x92722C85U, 0xA2BFE8A1U, 0xA81A664BU,
        0xC24B8B70U, 0xC76C51A3U, 0xD192E819U, 0xD6990624U, 0xF40E3585U, 0x106AA070U, 0x19A4C116U,
        0x1E376C08U, 0x2748774CU, 0x34B0BCB5U, 0x391C0CB3U, 0x4ED8AA4AU, 0x5B9CCA4FU, 0x682E6FF3U,
        0x748F82EEU, 0x78A5636FU, 0x84C87814U, 0x8CC70208U, 0x90BEFFFAU, 0xA4506CEBU, 0xBEF9A3F7U,
        0xC67178F2U};
    std::array<std::uint32_t, 8> state{0x6A09E667U, 0xBB67AE85U, 0x3C6EF372U, 0xA54FF53AU,
                                       0x510E527FU, 0x9B05688CU, 0x1F83D9ABU, 0x5BE0CD19U};
    const auto data = detail::pad(input);
    for (std::size_t offset = 0; offset < data.size(); offset += 64)
    {
        std::array<std::uint32_t, 64> w{};
        for (std::size_t i = 0; i < 16; ++i)
            w[i] = detail::load_be32(data.data() + offset + i * 4);
        for (std::size_t i = 16; i < w.size(); ++i)
        {
            const std::uint32_t s0 =
                std::rotr(w[i - 15], 7) ^ std::rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 =
                std::rotr(w[i - 2], 17) ^ std::rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        auto [a, b, c, d, e, f, g, h] = state;
        for (std::size_t i = 0; i < w.size(); ++i)
        {
            const std::uint32_t s1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
            const std::uint32_t choose = (e & f) ^ (~e & g);
            const std::uint32_t t1 = h + s1 + choose + k[i] + w[i];
            const std::uint32_t s0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = s0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }
    return detail::store_words(state);
}

} // namespace ps5::crypto
