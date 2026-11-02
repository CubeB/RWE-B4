#include "Sha256.h"

#include <array>
#include <cstdint>
#include <cstring>

namespace rwe
{
    namespace
    {
        constexpr std::array<std::uint32_t, 64> sha256RoundConstants = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

        std::uint32_t rotateRight(std::uint32_t value, int bits)
        {
            return (value >> bits) | (value << (32 - bits));
        }

        void sha256Transform(std::array<std::uint32_t, 8>& state, const unsigned char* block)
        {
            std::array<std::uint32_t, 64> words{};
            for (std::size_t i = 0; i < 16; ++i)
            {
                words[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24)
                    | (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16)
                    | (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8)
                    | static_cast<std::uint32_t>(block[i * 4 + 3]);
            }
            for (std::size_t i = 16; i < 64; ++i)
            {
                const auto s0 = rotateRight(words[i - 15], 7) ^ rotateRight(words[i - 15], 18) ^ (words[i - 15] >> 3);
                const auto s1 = rotateRight(words[i - 2], 17) ^ rotateRight(words[i - 2], 19) ^ (words[i - 2] >> 10);
                words[i] = words[i - 16] + s0 + words[i - 7] + s1;
            }

            auto a = state[0];
            auto b = state[1];
            auto c = state[2];
            auto d = state[3];
            auto e = state[4];
            auto f = state[5];
            auto g = state[6];
            auto h = state[7];
            for (std::size_t i = 0; i < 64; ++i)
            {
                const auto s1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
                const auto choose = (e & f) ^ (~e & g);
                const auto temp1 = h + s1 + choose + sha256RoundConstants[i] + words[i];
                const auto s0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
                const auto majority = (a & b) ^ (a & c) ^ (b & c);
                const auto temp2 = s0 + majority;
                h = g;
                g = f;
                f = e;
                e = d + temp1;
                d = c;
                c = b;
                b = a;
                a = temp1 + temp2;
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
    }

    std::string sha256Hex(std::string_view bytes)
    {
        std::array<std::uint32_t, 8> state = {
            0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

        const auto* data = reinterpret_cast<const unsigned char*>(bytes.data());
        std::size_t remaining = bytes.size();
        while (remaining >= 64)
        {
            sha256Transform(state, data);
            data += 64;
            remaining -= 64;
        }

        std::array<unsigned char, 128> tail{};
        std::memcpy(tail.data(), data, remaining);
        tail[remaining] = 0x80;
        const std::size_t tailLength = remaining < 56 ? 64 : 128;
        const auto bitLength = static_cast<std::uint64_t>(bytes.size()) * 8;
        for (std::size_t i = 0; i < 8; ++i)
        {
            tail[tailLength - 1 - i] = static_cast<unsigned char>((bitLength >> (i * 8)) & 0xff);
        }
        sha256Transform(state, tail.data());
        if (tailLength == 128)
        {
            sha256Transform(state, tail.data() + 64);
        }

        static constexpr char digits[] = "0123456789abcdef";
        std::string result;
        result.reserve(64);
        for (const auto word : state)
        {
            for (int shift = 28; shift >= 0; shift -= 4)
            {
                result.push_back(digits[(word >> shift) & 0xf]);
            }
        }
        return result;
    }
}
