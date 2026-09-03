// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// File responsibility:
// - Reads ZIP central records with explicit bounds and rejects unsafe paths/types.
// - Extracts stored/deflated entries with byte budgets, CRC checks and cancellation.
// ZIP64, encrypted and multi-disk archives are intentionally rejected, not guessed.
#include "saberstage/broadcast/MapArchive.hpp"
#include <zlib.h>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace saberstage::broadcast {
namespace {
std::uint32_t Read(std::string_view data, std::size_t at, std::size_t count) {
    if (at > data.size() || count > data.size() - at)
        throw std::runtime_error("Truncated map ZIP record");
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < count; ++i)
        value |= static_cast<std::uint32_t>(static_cast<unsigned char>(data[at + i])) << (8 * i);
    return value;
}
std::string SafePath(std::string_view raw) {
    if (raw.empty() || raw.size() > 240 || raw.front() == '/' || raw.find_first_of("\\:\0", 0, 3) != raw.npos ||
        std::any_of(raw.begin(), raw.end(), [](unsigned char c) { return c < 32 || c == 127; }))
        throw std::runtime_error("Unsafe path in map ZIP");
    std::size_t start = 0;
    while (start < raw.size()) {
        const auto slash = raw.find('/', start);
        const auto part = raw.substr(start, slash == raw.npos ? raw.size() - start : slash - start);
        if (part.empty() || part == "." || part == ".." || part.back() == ' ' || part.back() == '.')
            throw std::runtime_error("Unsafe path component in map ZIP");
        if (slash == raw.npos)
            break;
        start = slash + 1;
    }
    return std::string(raw);
}
struct Entry {
    std::string name;
    std::uint32_t packed, unpacked, crc, offset;
    int method;
    bool directory;
};
} // namespace
void ExtractMapArchive(std::string_view data, const std::filesystem::path &directory, const std::atomic<bool> &cancel) {
    if (!std::filesystem::is_directory(directory) || !std::filesystem::is_empty(directory) ||
        std::filesystem::is_symlink(directory))
        throw std::runtime_error("Map extraction requires a newly created empty directory");
    if (data.size() < 22 || data.size() > 64U * 1024U * 1024U)
        throw std::runtime_error("Map ZIP size is invalid");
    std::size_t end = data.size() - 22;
    const auto minimum = data.size() > 65557 ? data.size() - 65557 : 0;
    while (Read(data, end, 4) != 0x06054b50U || end + 22 + Read(data, end + 20, 2) != data.size()) {
        if (end == minimum)
            throw std::runtime_error("Map ZIP directory is missing");
        --end;
    }
    const auto count = Read(data, end + 10, 2);
    const auto centralSize = Read(data, end + 12, 4);
    const auto centralStart = Read(data, end + 16, 4);
    if (!count || count > 512 || Read(data, end + 4, 2) || Read(data, end + 6, 2) || Read(data, end + 8, 2) != count ||
        centralStart > end || centralSize > end - centralStart)
        throw std::runtime_error("Unsupported or oversized map ZIP directory");
    std::vector<Entry> entries;
    std::set<std::string> names;
    std::uint64_t total = 0;
    std::size_t cursor = centralStart;
    bool info = false;
    for (std::size_t i = 0; i < count; ++i) {
        if (Read(data, cursor, 4) != 0x02014b50U)
            throw std::runtime_error("Malformed central ZIP record");
        const auto nameSize = Read(data, cursor + 28, 2), extra = Read(data, cursor + 30, 2),
                   comment = Read(data, cursor + 32, 2);
        if (cursor + 46 + nameSize + extra + comment > centralStart + centralSize)
            throw std::runtime_error("ZIP record exceeds central directory");
        const auto flags = Read(data, cursor + 8, 2), method = Read(data, cursor + 10, 2),
                   attributes = Read(data, cursor + 38, 4);
        const auto unixType = (attributes >> 16) & 0170000;
        if ((flags & 1) || Read(data, cursor + 34, 2) || (method != 0 && method != 8) ||
            (unixType != 0 && unixType != 0100000 && unixType != 0040000))
            throw std::runtime_error("Unsupported ZIP encryption, compression or file type");
        Entry entry{SafePath(data.substr(cursor + 46, nameSize)),
                    Read(data, cursor + 20, 4),
                    Read(data, cursor + 24, 4),
                    Read(data, cursor + 16, 4),
                    Read(data, cursor + 42, 4),
                    static_cast<int>(method),
                    false};
        entry.directory = entry.name.back() == '/';
        auto canonicalName = entry.name;
        std::transform(canonicalName.begin(), canonicalName.end(), canonicalName.begin(),
                       [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; });
        if (!names.insert(canonicalName).second)
            throw std::runtime_error("Duplicate case-insensitive path in map ZIP");
        info |= canonicalName == "info.dat";
        total += entry.unpacked;
        if (entry.unpacked > 64U * 1024U * 1024U || total > 128U * 1024U * 1024U || entry.offset >= centralStart)
            throw std::runtime_error("Map ZIP expanded-data budget exceeded");
        entries.push_back(std::move(entry));
        cursor += 46 + nameSize + extra + comment;
    }
    if (!info)
        throw std::runtime_error("Map ZIP has no root Info.dat");
    const auto root = std::filesystem::canonical(directory);
    for (const auto &entry : entries) {
        if (cancel)
            throw std::runtime_error("Map installation cancelled");
        const auto target = root / entry.name;
        if (entry.directory) {
            std::filesystem::create_directories(target);
            continue;
        }
        const auto local = entry.offset;
        if (Read(data, local, 4) != 0x04034b50U || Read(data, local + 8, 2) != static_cast<unsigned>(entry.method) ||
            (Read(data, local + 6, 2) & 1))
            throw std::runtime_error("Invalid local map ZIP record");
        const auto localName = Read(data, local + 26, 2), extra = Read(data, local + 28, 2);
        const auto start = static_cast<std::uint64_t>(local) + 30 + localName + extra;
        if (start > centralStart || entry.packed > centralStart - start ||
            data.substr(local + 30, localName) != entry.name)
            throw std::runtime_error("Map ZIP data bounds or names disagree");
        const auto input = data.substr(start, entry.packed);
        std::string output(entry.unpacked, '\0');
        if (entry.method == 0) {
            if (input.size() != output.size())
                throw std::runtime_error("Invalid stored ZIP size");
            output.assign(input);
        } else {
            z_stream stream{};
            stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(input.data()));
            stream.avail_in = input.size();
            // zlib needs one spare output byte to report STREAM_END for empty
            // entries. The advertised size and CRC are still checked exactly.
            output.resize(entry.unpacked + 1);
            stream.next_out = reinterpret_cast<Bytef *>(output.data());
            stream.avail_out = output.size();
            if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
                throw std::runtime_error("ZIP inflater could not start");
            int result = Z_OK;
            while (result == Z_OK && !cancel)
                result = inflate(&stream, Z_FINISH);
            const auto produced = stream.total_out, consumed = stream.total_in;
            inflateEnd(&stream);
            if (cancel)
                throw std::runtime_error("Map installation cancelled");
            if (result != Z_STREAM_END || produced != entry.unpacked || consumed != entry.packed)
                throw std::runtime_error("Corrupt or oversized deflated map entry");
            output.resize(entry.unpacked);
        }
        if (crc32(0, reinterpret_cast<const Bytef *>(output.data()), output.size()) != entry.crc)
            throw std::runtime_error("Map ZIP CRC mismatch");
        std::filesystem::create_directories(target.parent_path());
        std::ofstream file(target, std::ios::binary | std::ios::trunc);
        if (!file.write(output.data(), output.size()) || !file.flush())
            throw std::runtime_error("Map extraction write failed (storage full or unavailable)");
    }
}
} // namespace saberstage::broadcast
