// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Loads SaberStage-private sherpa-onnx/ONNX Runtime libraries only when speech is first requested.
// - Runs the pinned eight-voice KittenTTS Nano v0.2 model synchronously on the existing TTS worker.

#include "saberstage/broadcast/KittenTtsBackend.hpp"

#include <sherpa-onnx/c-api/c-api.h>

#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <string>

namespace saberstage::broadcast {
namespace {

// Scotland2 mirrors QMOD libraryFiles from shared ModData storage into the
// application's private files/libs directory before loading mods. Android
// mounts /sdcard with noexec, so passing the public ModData path to dlopen()
// fails with EACCES even when the file and its hash are correct. Load the
// executable private mirror used by the modloader instead.
constexpr std::string_view kLibraryDirectory =
    "/data/user/0/com.beatgames.beatsaber/files/libs";
constexpr std::string_view kOnnxRuntimeName = "libsabstageort.so";
constexpr std::string_view kSherpaApiName = "libss-tts-neural-api.so";
constexpr std::size_t kMaximumUtteranceSeconds = 16U;

struct SherpaApi final {
    decltype(&SherpaOnnxCreateOfflineTts) create = nullptr;
    decltype(&SherpaOnnxDestroyOfflineTts) destroy = nullptr;
    decltype(&SherpaOnnxOfflineTtsSampleRate) sampleRate = nullptr;
    decltype(&SherpaOnnxOfflineTtsNumSpeakers) speakerCount = nullptr;
    decltype(&SherpaOnnxOfflineTtsGenerateWithConfig) generate = nullptr;
    decltype(&SherpaOnnxDestroyOfflineTtsGeneratedAudio) destroyAudio = nullptr;
};

SherpaApi gApi;

template<class Function>
Function Resolve(void* library, const char* name) noexcept {
    return reinterpret_cast<Function>(dlsym(library, name));
}

std::string DynamicLibraryError(std::string_view operation) {
    const auto* detail = dlerror();
    return std::string(operation) + (detail ? ": " + std::string(detail) : ": unknown linker error");
}

int SpeakerId(std::string_view voice) noexcept {
    static constexpr std::array<std::string_view, 8> voices{
        "expr-voice-2-m", "expr-voice-2-f", "expr-voice-3-m", "expr-voice-3-f",
        "expr-voice-4-m", "expr-voice-4-f", "expr-voice-5-m", "expr-voice-5-f"};
    const auto found = std::find(voices.begin(), voices.end(), voice);
    return found == voices.end() ? 1 : static_cast<int>(found - voices.begin());
}

struct CancellationContext final {
    const std::atomic<std::uint64_t>* generation = nullptr;
    std::uint64_t expected = 0;
};

int ContinueSynthesis(const float*, std::int32_t, float, void* opaque) noexcept {
    const auto* context = static_cast<const CancellationContext*>(opaque);
    return context && context->generation &&
            context->generation->load(std::memory_order_acquire) == context->expected
        ? 1
        : 0;
}

} // namespace

bool KittenTtsBackend::Initialize(
    const std::filesystem::path& modelDirectory,
    std::string* error) {
    Shutdown();
    const auto runtimePath = std::filesystem::path(kLibraryDirectory) / kOnnxRuntimeName;
    const auto apiPath = std::filesystem::path(kLibraryDirectory) / kSherpaApiName;
    dlerror();
    onnxRuntimeLibrary_ = dlopen(runtimePath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!onnxRuntimeLibrary_) {
        if (error) *error = DynamicLibraryError("The private ONNX Runtime could not be loaded");
        return false;
    }
    dlerror();
    sherpaApiLibrary_ = dlopen(apiPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!sherpaApiLibrary_) {
        if (error) *error = DynamicLibraryError("The private sherpa-onnx TTS runtime could not be loaded");
        Shutdown();
        return false;
    }

    gApi.create = Resolve<decltype(gApi.create)>(sherpaApiLibrary_, "SherpaOnnxCreateOfflineTts");
    gApi.destroy = Resolve<decltype(gApi.destroy)>(sherpaApiLibrary_, "SherpaOnnxDestroyOfflineTts");
    gApi.sampleRate = Resolve<decltype(gApi.sampleRate)>(sherpaApiLibrary_, "SherpaOnnxOfflineTtsSampleRate");
    gApi.speakerCount = Resolve<decltype(gApi.speakerCount)>(sherpaApiLibrary_, "SherpaOnnxOfflineTtsNumSpeakers");
    gApi.generate = Resolve<decltype(gApi.generate)>(sherpaApiLibrary_, "SherpaOnnxOfflineTtsGenerateWithConfig");
    gApi.destroyAudio = Resolve<decltype(gApi.destroyAudio)>(sherpaApiLibrary_, "SherpaOnnxDestroyOfflineTtsGeneratedAudio");
    if (!gApi.create || !gApi.destroy || !gApi.sampleRate || !gApi.speakerCount ||
            !gApi.generate || !gApi.destroyAudio) {
        if (error) *error = "The private sherpa-onnx library is missing a required TTS API.";
        Shutdown();
        return false;
    }

    const auto modelPath = (modelDirectory / "model.fp16.onnx").string();
    const auto voicesPath = (modelDirectory / "voices.bin").string();
    const auto tokensPath = (modelDirectory / "tokens.txt").string();
    const auto dataPath = (modelDirectory / "espeak-ng-data").string();
    if (!std::filesystem::is_regular_file(modelPath) ||
            !std::filesystem::is_regular_file(voicesPath) ||
            !std::filesystem::is_regular_file(tokensPath) ||
            !std::filesystem::is_regular_file(std::filesystem::path(dataPath) / "en_dict")) {
        if (error) *error = "The extracted KittenTTS model is incomplete.";
        Shutdown();
        return false;
    }

    SherpaOnnxOfflineTtsConfig config{};
    config.model.kitten.model = modelPath.c_str();
    config.model.kitten.voices = voicesPath.c_str();
    config.model.kitten.tokens = tokensPath.c_str();
    config.model.kitten.data_dir = dataPath.c_str();
    config.model.num_threads = 1;
    config.model.debug = 0;
    config.model.provider = "cpu";
    config.max_num_sentences = 1;
    config.silence_scale = 0.2F;
    synthesizer_ = gApi.create(&config);
    if (!synthesizer_) {
        if (error) *error = "KittenTTS Nano v0.2 could not initialize its neural model.";
        Shutdown();
        return false;
    }
    const auto* tts = static_cast<const SherpaOnnxOfflineTts*>(synthesizer_);
    if (gApi.sampleRate(tts) <= 0 || gApi.speakerCount(tts) < 8) {
        if (error) *error = "KittenTTS initialized with an unexpected sample rate or voice count.";
        Shutdown();
        return false;
    }
    return true;
}

bool KittenTtsBackend::Synthesize(
    std::string_view text,
    std::string_view voice,
    float rate,
    const std::atomic<std::uint64_t>& cancellationGeneration,
    std::uint64_t expectedGeneration,
    std::vector<float>& monoSamples,
    std::int32_t& sampleRate,
    std::string* error) {
    monoSamples.clear();
    sampleRate = 0;
    if (!synthesizer_ || !gApi.generate || text.empty()) {
        if (error) *error = synthesizer_
            ? "The TTS message was empty." : "The KittenTTS backend is not ready.";
        return false;
    }

    SherpaOnnxGenerationConfig config{};
    config.silence_scale = 0.2F;
    config.speed = std::clamp(rate, 0.5F, 2.0F);
    config.sid = SpeakerId(voice);
    CancellationContext cancellation{&cancellationGeneration, expectedGeneration};
    auto terminated = std::string(text);
    const auto* audio = gApi.generate(
        static_cast<const SherpaOnnxOfflineTts*>(synthesizer_), terminated.c_str(),
        &config, &ContinueSynthesis, &cancellation);
    if (cancellationGeneration.load(std::memory_order_acquire) != expectedGeneration) {
        if (audio) gApi.destroyAudio(audio);
        return true;
    }
    if (!audio || !audio->samples || audio->n <= 0 || audio->sample_rate <= 0) {
        if (audio) gApi.destroyAudio(audio);
        if (error) *error = "KittenTTS could not synthesize this chat message.";
        return false;
    }
    sampleRate = audio->sample_rate;
    const auto maximum = static_cast<std::size_t>(sampleRate) * kMaximumUtteranceSeconds;
    const auto count = std::min<std::size_t>(static_cast<std::size_t>(audio->n), maximum);
    monoSamples.assign(audio->samples, audio->samples + count);
    gApi.destroyAudio(audio);
    return !monoSamples.empty();
}

void KittenTtsBackend::Shutdown() noexcept {
    if (synthesizer_ && gApi.destroy) {
        gApi.destroy(static_cast<const SherpaOnnxOfflineTts*>(synthesizer_));
    }
    synthesizer_ = nullptr;
    gApi = {};
    if (sherpaApiLibrary_) dlclose(sherpaApiLibrary_);
    sherpaApiLibrary_ = nullptr;
    if (onnxRuntimeLibrary_) dlclose(onnxRuntimeLibrary_);
    onnxRuntimeLibrary_ = nullptr;
}

} // namespace saberstage::broadcast
