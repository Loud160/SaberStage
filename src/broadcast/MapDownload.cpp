// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// File responsibility:
// - Installs only after an explicit user action, never from chat receipt or row selection.
// - Validates the map-content SHA1 used by SongCore, not the unrelated ZIP checksum.
// - Keeps incomplete files outside SongCore's scan root and publishes by atomic rename.
#include "saberstage/broadcast/MapDownload.hpp"
#include "saberstage/broadcast/MapArchive.hpp"
#include "saberstage/broadcast/ChatNetwork.hpp"
#include "saberstage/Logging.hpp"
#include <rapidjson/document.h>
extern "C" {
#include <libavutil/sha.h>
#include <libavutil/mem.h>
}
#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <fcntl.h>
#include <unistd.h>
#include <sstream>
#include <iomanip>

namespace saberstage::broadcast {
namespace {
bool ValidHash(std::string_view hash) {
    return hash.size() == 40 && std::all_of(hash.begin(), hash.end(), [](unsigned char c) { return std::isxdigit(c); });
}
std::string Read(const std::filesystem::path &path, std::size_t limit) {
    if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) > limit)
        throw std::runtime_error("Missing or oversized map data file");
    std::ifstream file(path, std::ios::binary);
    std::string bytes(static_cast<std::size_t>(std::filesystem::file_size(path)), '\0');
    if (!file.read(bytes.data(), bytes.size()))
        throw std::runtime_error("Could not read map data file");
    return bytes;
}
std::string Field(const rapidjson::Value &value, const char *name) {
    if (!value.IsObject() || !value.HasMember(name) || !value[name].IsString())
        throw std::runtime_error(std::string("Map metadata is missing ") + name);
    return {value[name].GetString(), value[name].GetStringLength()};
}
std::filesystem::path MapFile(const std::filesystem::path &root, std::string_view name) {
    // Metadata paths are untrusted independently of the ZIP's entry names.
    // Maps use files relative to their root; refuse links and escaping paths.
    if (name.empty() || name.size() > 240 || name.find_first_of("\\:\0", 0, 3) != name.npos)
        throw std::runtime_error("Unsafe filename in Info.dat");
    const auto relative = std::filesystem::path(name);
    if (relative.is_absolute())
        throw std::runtime_error("Absolute filename in Info.dat");
    for (const auto &part : relative)
        if (part == ".." || part == ".")
            throw std::runtime_error("Escaping filename in Info.dat");
    const auto file = root / relative;
    const auto canonicalRoot = std::filesystem::canonical(root);
    auto partPath = root;
    for (const auto &part : relative) {
        partPath /= part;
        if (std::filesystem::is_symlink(partPath))
            throw std::runtime_error("Symbolic link in map data path");
    }
    const auto resolved = std::filesystem::weakly_canonical(file);
    const auto under = resolved.lexically_relative(canonicalRoot);
    if (under.empty() || under.is_absolute() || *under.begin() == "..")
        throw std::runtime_error("Map data escaped its directory");
    if (!std::filesystem::is_regular_file(file) || std::filesystem::is_symlink(file))
        throw std::runtime_error("Required map file is missing");
    return file;
}
void Verify(const std::filesystem::path &root, std::string expected, const std::atomic<bool> &cancel) {
    if (std::filesystem::is_symlink(root))
        throw std::runtime_error("Installed map directory is a symbolic link");
    auto info = root / "Info.dat";
    if (!std::filesystem::exists(info))
        info = root / "info.dat";
    if (std::filesystem::is_symlink(info))
        throw std::runtime_error("Map metadata is a symbolic link");
    const auto bytes = Read(info, 4 * 1024 * 1024);
    rapidjson::Document document;
    document.Parse<rapidjson::kParseValidateEncodingFlag>(bytes.data(), bytes.size());
    if (document.HasParseError() || !document.IsObject())
        throw std::runtime_error("Invalid Info.dat JSON");
    std::unique_ptr<AVSHA, decltype(&av_free)> sha(av_sha_alloc(), av_free);
    if (!sha || av_sha_init(sha.get(), 160) < 0)
        throw std::runtime_error("Map hash verifier unavailable");
    av_sha_update(sha.get(), reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size());
    std::size_t dataFiles = 0, hashBytes = bytes.size();
    const auto append = [&](const std::string &name) {
        if (cancel)
            throw std::runtime_error("Map installation cancelled");
        if (++dataFiles > 512)
            throw std::runtime_error("Too many map data references");
        auto data = Read(MapFile(root, name), 64 * 1024 * 1024);
        hashBytes += data.size();
        if (hashBytes > 256 * 1024 * 1024)
            throw std::runtime_error("Map hash-data budget exceeded");
        av_sha_update(sha.get(), reinterpret_cast<const std::uint8_t *>(data.data()), data.size());
    };
    if (document.HasMember("_difficultyBeatmapSets") && document["_difficultyBeatmapSets"].IsArray()) {
        MapFile(root, Field(document, "_songFilename"));
        for (const auto &set : document["_difficultyBeatmapSets"].GetArray()) {
            if (!set.IsObject() || !set.HasMember("_difficultyBeatmaps") || !set["_difficultyBeatmaps"].IsArray())
                throw std::runtime_error("Invalid map difficulty list");
            for (const auto &diff : set["_difficultyBeatmaps"].GetArray())
                append(Field(diff, "_beatmapFilename"));
        }
    } else if (document.HasMember("audio") && document.HasMember("difficultyBeatmaps") &&
               document["difficultyBeatmaps"].IsArray()) {
        const auto &audio = document["audio"];
        MapFile(root, Field(audio, "songFilename"));
        append(Field(audio, "audioDataFilename"));
        for (const auto &diff : document["difficultyBeatmaps"].GetArray()) {
            append(Field(diff, "beatmapDataFilename"));
            append(Field(diff, "lightshowDataFilename"));
        }
    } else
        throw std::runtime_error("Unsupported map metadata version");
    if (!dataFiles)
        throw std::runtime_error("Map contains no difficulty data");
    std::array<std::uint8_t, 20> digest{};
    av_sha_final(sha.get(), digest.data());
    std::string actual;
    constexpr char hex[] = "0123456789abcdef";
    for (auto value : digest) {
        actual += hex[value >> 4];
        actual += hex[value & 15];
    }
    std::transform(expected.begin(), expected.end(), expected.begin(), [](unsigned char c) { return std::tolower(c); });
    if (actual != expected)
        throw std::runtime_error("Downloaded map content does not match its BeatSaver hash");
}
} // namespace
MapDownload::~MapDownload() {
    Cancel();
    if (worker_.joinable())
        worker_.join();
}
void MapDownload::Cancel() noexcept {
    cancel_ = true;
}
MapDownloadSnapshot MapDownload::Snapshot() const {
    std::lock_guard lock(mutex_);
    return snapshot_;
}
void MapDownload::Report(MapDownloadState state, std::string status) {
    std::lock_guard lock(mutex_);
    snapshot_.state = state;
    snapshot_.status = std::move(status);
    ++snapshot_.revision;
}
bool MapDownload::Start(RequestedMap map, std::filesystem::path directory) {
    if (!done_ || !ValidHash(map.hash) || !directory.is_absolute())
        return false;
    if (worker_.joinable())
        worker_.join(); // done_ is published only after the worker exits its work.
    cancel_ = false;
    done_ = false;
    {
        std::lock_guard lock(mutex_);
        snapshot_.hash = map.hash;
    }
    Report(MapDownloadState::Downloading, "Downloading map (up to 64 MB)...");
    try {
        worker_ = std::thread([this, map = std::move(map), directory = std::move(directory)] { Run(map, directory); });
    } catch (...) {
        done_ = true;
        Report(MapDownloadState::Failed, "Map worker could not start.");
        throw;
    }
    return true;
}
void MapDownload::Run(RequestedMap map, std::filesystem::path directory) noexcept {
    std::filesystem::path partial, stagingRoot;
    const auto cleanup = [&] {
        // Only this hash-specific directory under our dedicated staging root
        // belongs to this operation. Never delete the user's song directory.
        if (partial.empty() || stagingRoot.empty() || partial.parent_path() != stagingRoot ||
            std::filesystem::is_symlink(partial))
            return;
        if (std::filesystem::exists(partial) && std::filesystem::weakly_canonical(partial).parent_path() == stagingRoot)
            std::filesystem::remove_all(partial);
    };
    try {
        directory = std::filesystem::canonical(directory);
        stagingRoot = directory.parent_path() / ".saberstage-map-downloads";
        if (std::filesystem::is_symlink(stagingRoot))
            throw std::runtime_error("Map staging path is a symbolic link");
        std::filesystem::create_directories(stagingRoot);
        stagingRoot = std::filesystem::canonical(stagingRoot);
        partial = stagingRoot / map.hash;
        const auto destination = directory / ("SaberStage-" + map.hash);
        if (std::filesystem::exists(destination)) {
            Verify(destination, map.hash, cancel_);
        } else {
            cleanup();
            std::filesystem::create_directory(partial);
            const auto zip = FetchChatResource(
                "https://cdn.beatsaver.com/" + map.hash + ".zip", 64 * 1024 * 1024, cancel_, {},
                [this](std::size_t received, std::int64_t expected) {
                    std::ostringstream text;
                    text << "Downloading: " << std::fixed << std::setprecision(1) << received / 1048576.0 << " MB";
                    if (expected > 0)
                        text << " / " << expected / 1048576.0 << " MB ("
                             << static_cast<int>(100.0 * received / expected) << "%)";
                    Report(MapDownloadState::Downloading, text.str());
                },
                180);
            Report(MapDownloadState::Extracting, "Extracting and checking map archive...");
            ExtractMapArchive(zip, partial, cancel_);
            Report(MapDownloadState::Verifying, "Verifying map content hash...");
            Verify(partial, map.hash, cancel_);
            if (cancel_)
                throw std::runtime_error("Map installation cancelled");
            // Queue durability is not enough if the map's directory entry is
            // published before its bytes reach storage. Flush only our staged files.
            for (const auto &entry : std::filesystem::recursive_directory_iterator(partial)) {
                if (cancel_)
                    throw std::runtime_error("Map installation cancelled");
                const int fd = open(entry.path().c_str(), O_RDONLY | O_CLOEXEC);
                if (fd < 0)
                    throw std::runtime_error("Could not open staged map for durable flush");
                const int result = fsync(fd);
                close(fd);
                if (result != 0)
                    throw std::runtime_error("Could not flush staged map to storage");
            }
            // recursive_directory_iterator excludes its root. Flush that
            // directory too: durable file bytes alone do not commit Info.dat
            // and difficulty entries before the directory is published.
            const int stagedFd = open(partial.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (stagedFd < 0)
                throw std::runtime_error("Could not open staged map directory for durable flush");
            const int stagedResult = fsync(stagedFd);
            close(stagedFd);
            if (stagedResult != 0)
                throw std::runtime_error("Could not flush staged map directory");
            std::filesystem::rename(partial, destination);
            const int fd = open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (fd < 0)
                throw std::runtime_error("Could not open song directory for durable flush");
            const int result = fsync(fd);
            close(fd);
            if (result != 0)
                throw std::runtime_error("Could not flush installed map directory");
        }
        Report(MapDownloadState::Installed, "Map installed. Refreshing the song library...");
        Logging::Logger.info("Requested map installed key={} hash={}", map.key, map.hash);
    } catch (const std::exception &error) {
        Report(cancel_ ? MapDownloadState::Cancelled : MapDownloadState::Failed,
               cancel_ ? "Map download cancelled." : error.what());
        Logging::Logger.warn("Requested map installation ended key={} cancelled={}: {}", map.key, cancel_.load(),
                             error.what());
    } catch (...) {
        Report(MapDownloadState::Failed, "Unexpected map installation failure; see log.");
        Logging::Logger.error("Requested map installation failed unexpectedly");
    }
    try {
        cleanup();
    } catch (const std::exception &error) {
        Logging::Logger.warn("Map partial cleanup failed: {}", error.what());
    }
    done_ = true;
}
} // namespace saberstage::broadcast
