// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// File responsibility: deterministic hostile-ZIP fixtures without network or headset access.
#include "saberstage/broadcast/MapArchive.hpp"
#include <zlib.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
using namespace saberstage::broadcast;
void Check(bool value, const char *why) {
    if (!value)
        throw std::runtime_error(why);
}
void Number(std::string &bytes, std::uint32_t value, int count) {
    for (int i = 0; i < count; ++i)
        bytes += static_cast<char>(value >> (8 * i));
}
std::string Zip(std::string name, std::string content, bool corrupt = false, unsigned mode = 0100644,
                bool deflated = false) {
    auto packed = content;
    if (deflated) {
        packed.resize(compressBound(content.size()));
        z_stream stream{};
        Check(deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) == Z_OK,
              "deflater");
        stream.next_in = reinterpret_cast<Bytef *>(content.data());
        stream.avail_in = content.size();
        stream.next_out = reinterpret_cast<Bytef *>(packed.data());
        stream.avail_out = packed.size();
        Check(deflate(&stream, Z_FINISH) == Z_STREAM_END, "deflate fixture");
        packed.resize(stream.total_out);
        deflateEnd(&stream);
    }
    const auto crc = crc32(0, reinterpret_cast<const Bytef *>(content.data()), content.size()) ^ (corrupt ? 1 : 0);
    std::string b;
    Number(b, 0x04034b50, 4);
    Number(b, 20, 2);
    Number(b, 0, 2);
    Number(b, deflated ? 8 : 0, 2);
    Number(b, 0, 4);
    Number(b, crc, 4);
    Number(b, packed.size(), 4);
    Number(b, content.size(), 4);
    Number(b, name.size(), 2);
    Number(b, 0, 2);
    b += name;
    b += packed;
    const auto central = b.size();
    Number(b, 0x02014b50, 4);
    Number(b, 0x0314, 2);
    Number(b, 20, 2);
    Number(b, 0, 2);
    Number(b, deflated ? 8 : 0, 2);
    Number(b, 0, 4);
    Number(b, crc, 4);
    Number(b, packed.size(), 4);
    Number(b, content.size(), 4);
    Number(b, name.size(), 2);
    Number(b, 0, 2);
    Number(b, 0, 2);
    Number(b, 0, 2);
    Number(b, 0, 2);
    Number(b, mode << 16, 4);
    Number(b, 0, 4);
    b += name;
    const auto centralSize = b.size() - central;
    Number(b, 0x06054b50, 4);
    Number(b, 0, 4);
    Number(b, 1, 2);
    Number(b, 1, 2);
    Number(b, centralSize, 4);
    Number(b, central, 4);
    Number(b, 0, 2);
    return b;
}
int main() try {
    char temp[] = "/tmp/saberstage-archive-XXXXXX";
    const char *made = mkdtemp(temp);
    Check(made, "temporary fixture directory");
    const auto root = std::filesystem::canonical(made);
    std::atomic<bool> cancel{false};
    int index = 0;
    const auto extract = [&](const std::string &zip, bool expected) {
        const auto path = root / std::to_string(++index);
        std::filesystem::create_directory(path);
        bool success = false;
        try {
            ExtractMapArchive(zip, path, cancel);
            success = true;
        } catch (const std::exception &) {
        }
        Check(success == expected, "ZIP acceptance policy");
        return path;
    };
    const auto good = extract(Zip("Info.dat", "{\"_version\":\"2.1.0\"}"), true);
    Check(std::filesystem::file_size(good / "Info.dat") == 20, "exact extracted data");
    extract(Zip("Info.dat", "payload", false, 0100644, true), true);
    extract(Zip("Info.dat", "", false, 0100644, true), true);
    extract(Zip("../Info.dat", "escape"), false);
    extract(Zip("/Info.dat", "absolute"), false);
    extract(Zip("x/../../Info.dat", "escape"), false);
    extract(Zip("C:\\Info.dat", "windows absolute"), false);
    extract(Zip("Info.dat", "crc", true), false);
    extract(Zip("Info.dat", "link", false, 0120777), false);
    extract(Zip("Expert.dat", "no metadata"), false);
    extract(Zip("Info.dat", "truncated").substr(0, 25), false);
    cancel = true;
    extract(Zip("Info.dat", "cancelled"), false);
    // Only the independently-created fixture directory belongs to this test.
    Check(root.parent_path() == "/tmp" && root.filename().string().starts_with("saberstage-archive-"), "cleanup scope");
    std::filesystem::remove_all(root);
    std::cout << "Map archive fixtures passed\n";
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
