// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Validates and extracts the embedded KittenTTS model with strict ZIP limits.
// - Reuses a completed version directory and never exposes a partially extracted model.

#include "saberstage/broadcast/TtsData.hpp"

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

extern "C" std::uint8_t _binary_kitten_data_zip_start[];
extern "C" std::uint8_t _binary_kitten_data_zip_end[];

namespace saberstage::broadcast {
namespace {

constexpr std::string_view kDataIdentity = "kitten-nano-en-v0_2-fp16-e11a97b8";
constexpr std::string_view kArchivePrefix = "kitten-nano-en-v0_2-fp16/";

std::uint32_t Read(std::string_view data, std::size_t at, std::size_t count) {
    if (at > data.size() || count > data.size() - at) {
        throw std::runtime_error("Truncated embedded TTS ZIP record");
    }
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < count; ++index) {
        value |= static_cast<std::uint32_t>(static_cast<unsigned char>(data[at + index])) <<
            (8U * index);
    }
    return value;
}

std::string SafePath(std::string_view raw) {
    if (raw.empty() || raw.size() > 240U || raw.front() == '/' ||
            raw.find_first_of("\\:\0", 0, 3) != raw.npos ||
            !raw.starts_with(kArchivePrefix) ||
            std::any_of(raw.begin(), raw.end(), [](unsigned char character) {
                return character < 32U || character == 127U;
            })) {
        throw std::runtime_error("Unsafe path in embedded TTS data");
    }
    std::size_t start = 0;
    while (start < raw.size()) {
        const auto slash = raw.find('/', start);
        const auto part = raw.substr(start, slash == raw.npos ? raw.size() - start : slash - start);
        if (part.empty() || part == "." || part == "..") {
            throw std::runtime_error("Unsafe path component in embedded TTS data");
        }
        if (slash == raw.npos) break;
        start = slash + 1U;
    }
    return std::string(raw);
}

struct Entry {
    std::string name;
    std::uint32_t packed = 0;
    std::uint32_t unpacked = 0;
    std::uint32_t crc = 0;
    std::uint32_t offset = 0;
    int method = 0;
    bool directory = false;
};

void Extract(std::string_view data, const std::filesystem::path& directory) {
    if (data.size() < 22U || data.size() > 28U * 1024U * 1024U) {
        throw std::runtime_error("Embedded TTS ZIP size is invalid");
    }
    std::size_t end = data.size() - 22U;
    const auto minimum = data.size() > 65'557U ? data.size() - 65'557U : 0U;
    while (Read(data, end, 4) != 0x06054b50U ||
            end + 22U + Read(data, end + 20U, 2) != data.size()) {
        if (end == minimum) throw std::runtime_error("Embedded TTS ZIP directory is missing");
        --end;
    }
    const auto count = Read(data, end + 10U, 2);
    const auto centralSize = Read(data, end + 12U, 4);
    const auto centralStart = Read(data, end + 16U, 4);
    if (!count || count > 512U || Read(data, end + 4U, 2) || Read(data, end + 6U, 2) ||
            Read(data, end + 8U, 2) != count || centralStart > end ||
            centralSize > end - centralStart) {
        throw std::runtime_error("Unsupported embedded TTS ZIP directory");
    }
    std::vector<Entry> entries;
    entries.reserve(count);
    std::set<std::string> names;
    std::uint64_t total = 0;
    std::size_t cursor = centralStart;
    for (std::size_t index = 0; index < count; ++index) {
        if (Read(data, cursor, 4) != 0x02014b50U) {
            throw std::runtime_error("Malformed embedded TTS ZIP record");
        }
        const auto nameSize = Read(data, cursor + 28U, 2);
        const auto extraSize = Read(data, cursor + 30U, 2);
        const auto commentSize = Read(data, cursor + 32U, 2);
        if (cursor + 46U + nameSize + extraSize + commentSize > centralStart + centralSize) {
            throw std::runtime_error("Embedded TTS ZIP record exceeds its directory");
        }
        const auto method = Read(data, cursor + 10U, 2);
        const auto attributes = Read(data, cursor + 38U, 4);
        const auto unixType = (attributes >> 16U) & 0170000U;
        if ((Read(data, cursor + 8U, 2) & 1U) || Read(data, cursor + 34U, 2) ||
                (method != 0U && method != 8U) ||
                (unixType != 0U && unixType != 0100000U && unixType != 0040000U)) {
            throw std::runtime_error("Unsupported embedded TTS ZIP entry");
        }
        Entry entry;
        entry.name = SafePath(data.substr(cursor + 46U, nameSize));
        entry.packed = Read(data, cursor + 20U, 4);
        entry.unpacked = Read(data, cursor + 24U, 4);
        entry.crc = Read(data, cursor + 16U, 4);
        entry.offset = Read(data, cursor + 42U, 4);
        entry.method = static_cast<int>(method);
        entry.directory = entry.name.back() == '/';
        auto canonical = entry.name;
        std::transform(canonical.begin(), canonical.end(), canonical.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        if (!names.insert(canonical).second) {
            throw std::runtime_error("Duplicate path in embedded TTS ZIP");
        }
        total += entry.unpacked;
        if (entry.unpacked > 24U * 1024U * 1024U || total > 26U * 1024U * 1024U ||
                entry.offset >= centralStart) {
            throw std::runtime_error("Embedded TTS data exceeds its extraction budget");
        }
        entries.push_back(std::move(entry));
        cursor += 46U + nameSize + extraSize + commentSize;
    }
    const auto root = std::filesystem::canonical(directory);
    for (const auto& entry : entries) {
        const auto target = root / entry.name;
        if (entry.directory) {
            std::filesystem::create_directories(target);
            continue;
        }
        if (Read(data, entry.offset, 4) != 0x04034b50U ||
                Read(data, entry.offset + 8U, 2) != static_cast<unsigned>(entry.method) ||
                (Read(data, entry.offset + 6U, 2) & 1U)) {
            throw std::runtime_error("Invalid embedded TTS local ZIP record");
        }
        const auto localName = Read(data, entry.offset + 26U, 2);
        const auto extra = Read(data, entry.offset + 28U, 2);
        const auto start = static_cast<std::uint64_t>(entry.offset) + 30U + localName + extra;
        if (start > centralStart || entry.packed > centralStart - start ||
                data.substr(entry.offset + 30U, localName) != entry.name) {
            throw std::runtime_error("Embedded TTS ZIP data bounds disagree");
        }
        const auto input = data.substr(start, entry.packed);
        std::string output(entry.unpacked, '\0');
        if (entry.method == 0) {
            if (input.size() != output.size()) throw std::runtime_error("Invalid TTS ZIP stored size");
            output.assign(input);
        } else {
            z_stream stream{};
            stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
            stream.avail_in = input.size();
            output.resize(entry.unpacked + 1U);
            stream.next_out = reinterpret_cast<Bytef*>(output.data());
            stream.avail_out = output.size();
            if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
                throw std::runtime_error("Embedded TTS ZIP inflater could not start");
            }
            const auto result = inflate(&stream, Z_FINISH);
            const auto produced = stream.total_out;
            const auto consumed = stream.total_in;
            inflateEnd(&stream);
            if (result != Z_STREAM_END || produced != entry.unpacked || consumed != entry.packed) {
                throw std::runtime_error("Corrupt embedded TTS ZIP entry");
            }
            output.resize(entry.unpacked);
        }
        if (crc32(0, reinterpret_cast<const Bytef*>(output.data()), output.size()) != entry.crc) {
            throw std::runtime_error("Embedded TTS ZIP CRC mismatch");
        }
        std::filesystem::create_directories(target.parent_path());
        std::ofstream file(target, std::ios::binary | std::ios::trunc);
        if (!file.write(output.data(), static_cast<std::streamsize>(output.size())) || !file.flush()) {
            throw std::runtime_error("Embedded TTS data could not be written");
        }
    }
}

} // namespace

std::filesystem::path EnsureEmbeddedKittenTtsData(const std::filesystem::path& storageRoot) {
    const auto versionRoot = storageRoot / std::string(kDataIdentity);
    const auto marker = versionRoot / ".complete";
    const auto modelRoot = versionRoot / std::string(kArchivePrefix.substr(0, kArchivePrefix.size() - 1U));
    if (std::filesystem::is_regular_file(marker) &&
            std::filesystem::is_regular_file(modelRoot / "model.fp16.onnx") &&
            std::filesystem::is_regular_file(modelRoot / "voices.bin") &&
            std::filesystem::is_regular_file(modelRoot / "tokens.txt") &&
            std::filesystem::is_regular_file(modelRoot / "espeak-ng-data" / "en_dict")) {
        return modelRoot;
    }
    std::filesystem::create_directories(storageRoot);
    const auto temporary = storageRoot / (std::string(kDataIdentity) + ".new");
    std::error_code ignored;
    std::filesystem::remove_all(temporary, ignored);
    std::filesystem::create_directories(temporary);
    const auto* begin = _binary_kitten_data_zip_start;
    const auto* end = _binary_kitten_data_zip_end;
    if (end <= begin) throw std::runtime_error("Embedded TTS data is empty");
    Extract(std::string_view(
        reinterpret_cast<const char*>(begin), static_cast<std::size_t>(end - begin)), temporary);
    {
        std::ofstream complete(temporary / ".complete", std::ios::trunc);
        if (!complete.write(kDataIdentity.data(), static_cast<std::streamsize>(kDataIdentity.size())) ||
                !complete.flush()) {
            throw std::runtime_error("Embedded TTS completion marker could not be written");
        }
    }
    std::filesystem::remove_all(versionRoot, ignored);
    std::filesystem::rename(temporary, versionRoot);
    return versionRoot / std::string(kArchivePrefix.substr(0, kArchivePrefix.size() - 1U));
}

} // namespace saberstage::broadcast
