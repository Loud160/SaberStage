// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Exercises Settings behavior on the host without starting Beat Saber.
// - Regression coverage focuses on deterministic state, validation, and boundary conditions.

#include "saberstage/settings/SettingsModel.hpp"
#include "saberstage/settings/SettingsService.hpp"
#include "saberstage/ui/MenuCopy.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void Write(const std::filesystem::path& path, std::string_view value) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << value;
}

std::string Read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

} // namespace

int main() {
    using namespace saberstage::settings;

    const auto defaults = Defaults();
    Check(defaults.schemaVersion == kCurrentSchemaVersion, "defaults use current schema");
    Check(defaults.camera.profiles.size() == 1, "Prompt 3 persists exactly one camera profile");
    Check(defaults.camera.Primary().profileId == "primary", "defaults keep stable primary camera identity");
    Check(!defaults.camera.Primary().anchoredFloatEnabled, "anchored float defaults off");
    Check(defaults.camera.Primary().followMode == saberstage::camera::FollowMode::Player,
          "default camera follows the logical player anchor");
    Check(defaults.camera.Primary().multisampleCount == 1,
          "third-person MSAA defaults off to protect Quest 2 gameplay performance");
    Check(defaults.preview.selectedCameraId == "primary", "preview targets the stable primary camera");
    Check(defaults.preview.floorResolutionWidth == 1920 && defaults.preview.floatingResolutionWidth == 512 &&
              defaults.preview.floorFramesPerSecond == 15 && defaults.preview.floatingFramesPerSecond == 15,
          "preview defaults preserve the previous floor and movable quality");
    auto invalidPreview = defaults;
    invalidPreview.preview.floorResolutionWidth = 4096;
    invalidPreview.preview.floatingResolutionWidth = -1;
    invalidPreview.preview.floorFramesPerSecond = 0;
    invalidPreview.preview.floatingFramesPerSecond = 500;
    ValidateAndRepair(invalidPreview);
    Check(invalidPreview.preview.floorResolutionWidth == 1920 && invalidPreview.preview.floatingResolutionWidth == 512 &&
              invalidPreview.preview.floorFramesPerSecond == 15 && invalidPreview.preview.floatingFramesPerSecond == 15,
          "corrupt preview choices repair independently to supported defaults");
    Check(defaults.preview.rotationDegrees.y == 0.0F &&
              defaults.recording.worldControlsRotationDegrees.y == 0.0F,
          "world panels default to the visible FloatingScreen face");
    Check(defaults.chat.width == 70.0F && defaults.chat.height == 58.0F,
          "larger chat size limits do not change the initial or reset size");
    Check(defaults.chat.fontSize == 4.6F,
          "Quest chat defaults to a readable world-space font size");
    auto enlargedChat = defaults;
    enlargedChat.chat.width = 240.0F;
    enlargedChat.chat.height = 200.0F;
    ValidateAndRepair(enlargedChat);
    Check(enlargedChat.chat.width == 240.0F && enlargedChat.chat.height == 200.0F,
          "chat accepts double the previous maximum width and height");
    enlargedChat.chat.width = 241.0F;
    enlargedChat.chat.height = 201.0F;
    ValidateAndRepair(enlargedChat);
    Check(enlargedChat.chat.width == defaults.chat.width &&
              enlargedChat.chat.height == defaults.chat.height,
          "invalid saved chat sizes retain the existing reset-to-default policy");
    enlargedChat.chat.width = 45.0F;
    enlargedChat.chat.height = 32.0F;
    ValidateAndRepair(enlargedChat);
    Check(enlargedChat.chat.width == 45.0F && enlargedChat.chat.height == 32.0F,
          "chat minimum dimensions remain unchanged");
    enlargedChat.chat.width = 0.0F;
    enlargedChat.chat.height = 0.0F;
    ValidateAndRepair(enlargedChat);
    Check(enlargedChat.chat.width == defaults.chat.width &&
              enlargedChat.chat.height == defaults.chat.height,
          "undersized saved chat dimensions still reset to defaults");
    Check(!defaults.recording.gameplayOnly, "recording defaults to continuous menu and gameplay capture");
    Check(defaults.recording.local.resolution == RecordingResolution::P1080 &&
              defaults.recording.local.framesPerSecond == 30 &&
              defaults.recording.livestream.resolution == RecordingResolution::P1080 &&
              defaults.recording.livestream.framesPerSecond == 30,
          "both output profiles default to gameplay-safe 1080p30 output");
    Check(defaults.recording.local.peakBitrateBitsPerSecond >=
              defaults.recording.local.bitrateBitsPerSecond &&
              defaults.recording.livestream.peakBitrateBitsPerSecond >=
                  defaults.recording.livestream.bitrateBitsPerSecond,
          "default peak bitrate is not below target bitrate in either profile");
    Check(!defaults.recording.local.microphoneEnabled &&
              !defaults.recording.livestream.microphoneEnabled &&
              defaults.recording.local.audio.microphoneMode == MicrophoneMode::Open &&
              defaults.recording.livestream.audio.microphoneMode == MicrophoneMode::Open,
          "Quest microphone capture defaults off in both independent profiles");
    Check(!defaults.tts.enabled && defaults.tts.ignoreKnownBots &&
              defaults.tts.ignoreCommands && !defaults.tts.speakUrls &&
              !defaults.tts.speakEmoteNames && defaults.tts.queueCapacity == 4 &&
              defaults.tts.outputRoute == TtsOutputRoute::HeadsetOnly,
          "local Chat TTS defaults off with conservative filtering and queue bounds");
    Check(!defaults.connectionTest.hasResult &&
              defaults.connectionTest.sustainedUploadMegabitsPerSecond == 0.0F,
          "connection test defaults do not impose an unmeasured stream ceiling");
    Check(defaults.broadcast.provider == LivestreamProvider::Twitch &&
              defaults.broadcast.twitch.enabled &&
              !defaults.broadcast.kick.enabled &&
              !defaults.broadcast.youtube.enabled &&
              !defaults.broadcast.custom.enabled &&
              defaults.broadcast.twitch.maximumVideoBitrateBitsPerSecond == 6'000'000 &&
              defaults.broadcast.kick.maximumVideoBitrateBitsPerSecond == 8'000'000 &&
              defaults.broadcast.twitch.serverUrl.rfind("rtmp://", 0) == 0 &&
              defaults.broadcast.youtube.serverUrl.rfind("rtmps://", 0) == 0 &&
              defaults.broadcast.twitch.streamKey.empty() &&
              defaults.broadcast.youtube.streamKey.empty() &&
              defaults.broadcast.twitchAccount.clientId == kSaberStageTwitchClientId &&
              defaults.broadcast.keepHeadsetAwake &&
              defaults.recording.livestream.gameAudioEnabled &&
              defaults.recording.livestream.gameAudioVolumePercent == 100.0F &&
              !defaults.recording.livestream.microphoneEnabled &&
              defaults.recording.livestream.microphoneVolumePercent == 100.0F &&
               !defaults.broadcast.postMapInfoToChat &&
               !defaults.broadcast.twitchAccount.chatWriteAuthorized &&
               defaults.broadcast.twitchAccount.protectedTokenEnvelope.empty(),
           "livestream defaults keep independent endpoints and SaberStage's public Twitch identity");
    auto providerProfile = defaults.recording.livestream;
    auto providerDestination = defaults.broadcast.twitch;
    providerDestination.streamKey = "test-key";
    Check(ValidateLivestreamProfileForProvider(
              LivestreamProvider::Twitch, providerProfile, providerDestination).empty(),
          "Twitch's default 1080p30 CBR profile satisfies its published limits");
    auto recommendedProfile = providerProfile;
    recommendedProfile.resolution = RecordingResolution::P1440;
    recommendedProfile.framesPerSecond = 30;
    recommendedProfile.h264Level = H264Level::L50;
    recommendedProfile = RecommendedLivestreamProfile(
        LivestreamProvider::Twitch, recommendedProfile);
    Check(recommendedProfile.resolution == RecordingResolution::P1080 &&
              recommendedProfile.framesPerSecond == 30 &&
              recommendedProfile.bitrateBitsPerSecond == 4'500'000 &&
              recommendedProfile.peakBitrateBitsPerSecond == 4'500'000 &&
              recommendedProfile.rateControl == RateControlMode::ConstantBitrate &&
              recommendedProfile.h264Level == H264Level::Automatic &&
              recommendedProfile.keyframeIntervalSeconds == 2 &&
              recommendedProfile.audioBitrateBitsPerSecond == 160'000,
          "Twitch recommendations preserve supported frame rate, lower unsupported 1440p, and reset shared encoder constraints");
    providerProfile.bitrateBitsPerSecond = 8'000'000;
    const auto twitchBitrateError = ValidateLivestreamProfileForProvider(
        LivestreamProvider::Twitch, providerProfile, providerDestination);
    Check(twitchBitrateError.find("at most 6 Mbps") != std::string::npos &&
              twitchBitrateError.find("set to 8 Mbps") != std::string::npos,
          "Twitch preflight reports both configured and allowed bitrate");
    providerProfile = defaults.recording.livestream;
    providerProfile.bitrateBitsPerSecond = 8'000'000;
    providerDestination = defaults.broadcast.kick;
    providerDestination.streamKey = "test-key";
    Check(ValidateLivestreamProfileForProvider(
              LivestreamProvider::Kick, providerProfile, providerDestination).empty(),
          "Kick accepts its published 8 Mbps 1080p CBR ceiling");
    providerProfile.resolution = RecordingResolution::P1440;
    Check(ValidateLivestreamProfileForProvider(
              LivestreamProvider::Kick, providerProfile, providerDestination)
              .find("1920x1080") != std::string::npos,
          "Kick preflight rejects output above its published resolution cap");
    providerProfile = defaults.recording.livestream;
    providerProfile.framesPerSecond = 60;
    providerProfile.bitrateBitsPerSecond = 10'000'000;
    providerDestination = defaults.broadcast.youtube;
    providerDestination.streamKey = "test-key";
    Check(MaximumLivestreamVideoBitrate(
              LivestreamProvider::YouTube, providerProfile, providerDestination) == 12'000'000 &&
              ValidateLivestreamProfileForProvider(
                  LivestreamProvider::YouTube, providerProfile, providerDestination).empty(),
          "YouTube applies its 1080p60 H.264 bitrate ceiling");
    recommendedProfile = providerProfile;
    recommendedProfile.resolution = RecordingResolution::P1440;
    recommendedProfile.framesPerSecond = 60;
    recommendedProfile = RecommendedLivestreamProfile(
        LivestreamProvider::YouTube, recommendedProfile);
    Check(recommendedProfile.resolution == RecordingResolution::P1440 &&
              recommendedProfile.framesPerSecond == 60 &&
              recommendedProfile.bitrateBitsPerSecond == 24'000'000 &&
              recommendedProfile.audioBitrateBitsPerSecond == 128'000,
          "YouTube recommendations preserve its supported 1440p60 selection");
    providerProfile = defaults.recording.livestream;
    providerProfile.bitrateBitsPerSecond = 7'000'000;
    providerDestination = defaults.broadcast.custom;
    providerDestination.serverUrl = "rtmps://custom.example/live";
    providerDestination.streamKey = "test-key";
    providerDestination.maximumVideoBitrateBitsPerSecond = 6'000'000;
    Check(ValidateLivestreamProfileForProvider(
              LivestreamProvider::Custom, providerProfile, providerDestination)
              .find("at most 6 Mbps") != std::string::npos,
          "custom destinations enforce their user-configured bitrate ceiling");
    auto refreshFixture = defaults.broadcast.twitchAccount;
    refreshFixture.accessToken = "access";
    refreshFixture.refreshToken = "refresh";
    refreshFixture.login = "channel";
    refreshFixture.userId = "123";
    refreshFixture.expiresAtUnixSeconds = 10'000;
    Check(!TwitchTokenNeedsRefresh(refreshFixture, 9'000),
          "healthy Twitch tokens are not refreshed early");
    Check(TwitchTokenNeedsRefresh(refreshFixture, 9'700),
          "Twitch tokens refresh within the five-minute safety window");
    refreshFixture.refreshToken.clear();
    Check(!TwitchTokenNeedsRefresh(refreshFixture, 10'000),
          "missing refresh credentials require authorization instead of a broken refresh attempt");
    Check(!defaults.camera.Primary().keepLevel,
          "camera level lock defaults off to preserve existing authored/manual roll");
    Check(!defaults.camera.Primary().gizmoVisible,
          "persistent HMD camera gizmo defaults off");
    Check(saberstage::ui::copy::LongestLine(saberstage::ui::copy::kScaffoldDescription) <= 32,
          "every scaffold description line fits the narrow menu budget");
    Check(saberstage::ui::copy::LineCount(saberstage::ui::copy::kScaffoldDescription) <= 8,
          "scaffold description fits its reserved menu height");

    auto invalid = defaults;
    invalid.camera.Primary().profileId.clear();
    invalid.camera.Primary().fovDegrees = 500.0F;
    invalid.camera.Primary().requestedWidth = 1279;
    invalid.camera.Primary().multisampleCount = 8;
    invalid.preview.scale = -1.0F;
    invalid.preview.selectedCameraId = "missing";
    invalid.preview.position.x = 2000.0F;
    invalid.recording.local.framesPerSecond = 1000;
    invalid.recording.local.bitrateBitsPerSecond = 20'000'000;
    invalid.recording.local.peakBitrateBitsPerSecond = 5'000'000;
    invalid.broadcast.reconnectAttempts = 1000;
    invalid.recording.livestream.gameAudioVolumePercent = -20.0F;
    invalid.recording.livestream.microphoneVolumePercent = 500.0F;
    invalid.broadcast.youtube.serverUrl = "https://not-an-rtmp-endpoint";
    invalid.broadcast.kick.streamKey = "invalid key with spaces";
    invalid.recording.livestream.audio.gateOpenThresholdDb =
        std::numeric_limits<float>::quiet_NaN();
    invalid.recording.livestream.audio.gateCloseThresholdDb = 4.0F;
    invalid.recording.livestream.audio.pushToTalkReleaseMilliseconds = 5000.0F;
    invalid.recording.livestream.audio.compressorRatio = -2.0F;
    invalid.recording.livestream.audio.limiterCeilingDb = 6.0F;
    invalid.tts.maximumCharacters = 50'000;
    invalid.tts.queueCapacity = 0;
    invalid.tts.speechRate = std::numeric_limits<float>::infinity();
    invalid.tts.voice = "unsupported-voice";
    invalid.connectionTest.hasResult = true;
    invalid.connectionTest.sustainedUploadMegabitsPerSecond =
        std::numeric_limits<float>::quiet_NaN();
    invalid.connectionTest.testedAtUnixSeconds = -1;
    const auto validation = ValidateAndRepair(invalid);
    Check(validation.changed && validation.repairedFields >= 7, "invalid fields are repaired individually");
    Check(invalid.camera.Primary().fovDegrees == defaults.camera.Primary().fovDegrees, "invalid FOV repairs to default");
    Check((invalid.camera.Primary().requestedWidth & 1) == 0, "odd encoder dimension becomes even");
    Check(invalid.camera.Primary().multisampleCount == 1,
          "unsupported third-person MSAA repairs to the Quest-safe default");
    Check(invalid.recording.local.framesPerSecond == 30, "recording FPS repairs to a supported hardware rate");
    Check(invalid.recording.local.peakBitrateBitsPerSecond ==
              invalid.recording.local.bitrateBitsPerSecond,
          "recording peak bitrate repairs to at least the target bitrate");
    Check(invalid.broadcast.reconnectAttempts == defaults.broadcast.reconnectAttempts,
          "livestream reconnect count repairs to its bounded default");
    Check(invalid.recording.livestream.gameAudioVolumePercent == 100.0F &&
              invalid.recording.livestream.microphoneVolumePercent == 100.0F,
          "livestream audio mix volumes repair to safe defaults");
    Check(invalid.broadcast.youtube.serverUrl == defaults.broadcast.youtube.serverUrl &&
              invalid.broadcast.kick.streamKey.empty(),
          "invalid service-specific livestream destinations repair without exposing credentials");
    Check(invalid.recording.livestream.audio.gateOpenThresholdDb ==
                  defaults.recording.livestream.audio.gateOpenThresholdDb &&
              invalid.recording.livestream.audio.gateCloseThresholdDb <=
                  invalid.recording.livestream.audio.gateOpenThresholdDb &&
              invalid.recording.livestream.audio.pushToTalkReleaseMilliseconds ==
                  defaults.recording.livestream.audio.pushToTalkReleaseMilliseconds &&
              invalid.recording.livestream.audio.compressorRatio ==
                  defaults.recording.livestream.audio.compressorRatio &&
              invalid.recording.livestream.audio.limiterCeilingDb ==
                  defaults.recording.livestream.audio.limiterCeilingDb,
          "invalid microphone DSP values repair to safe finite settings");
    auto wideGateOffset = defaults;
    wideGateOffset.recording.local.audio.gateOpenThresholdDb = -60.0F;
    wideGateOffset.recording.local.audio.gateCloseThresholdDb = -90.0F;
    ValidateAndRepair(wideGateOffset);
    Check(wideGateOffset.recording.local.audio.gateCloseThresholdDb == -90.0F,
          "voice gate preserves a 30 dB cutoff offset at the quietest open threshold");
    Check(invalid.tts.maximumCharacters == defaults.tts.maximumCharacters &&
              invalid.tts.queueCapacity == defaults.tts.queueCapacity &&
              invalid.tts.speechRate == defaults.tts.speechRate &&
              invalid.tts.voice == defaults.tts.voice,
          "invalid Chat TTS bounds repair to disabled-safe defaults");
    Check(!invalid.connectionTest.hasResult &&
              invalid.connectionTest.sustainedUploadMegabitsPerSecond == 0.0F &&
              invalid.connectionTest.testedAtUnixSeconds == 0,
          "invalid persisted bandwidth measurements cannot disable or bypass stream limits");
    auto legacyTtsVoice = defaults;
    legacyTtsVoice.tts.voice = "en-gb-scotland";
    ValidateAndRepair(legacyTtsVoice);
    Check(legacyTtsVoice.tts.voice == defaults.tts.voice,
          "legacy eSpeak voice migrates to the default KittenTTS neural voice");
    auto excessProfiles = defaults;
    auto futureProfile = saberstage::camera::DefaultCameraProfile();
    futureProfile.profileId = "future-secondary";
    excessProfiles.camera.profiles.push_back(futureProfile);
    const auto collectionRepair = ValidateAndRepair(excessProfiles);
    Check(collectionRepair.changed && excessProfiles.camera.profiles.size() == 1,
          "Prompt 3 bounds the future-ready profile collection to Primary");

    auto reset = defaults;
    reset.camera.Primary().fovDegrees = 110.0F;
    reset.preview.visible = true;
    ResetSubsystem(reset, Subsystem::Camera);
    Check(reset.camera.Primary().fovDegrees == defaults.camera.Primary().fovDegrees, "camera reset restores camera defaults");
    Check(reset.preview.visible, "camera reset preserves unrelated preview settings");

    reset.general.diagnosticsEnabled = false;
    ResetSubsystem(reset, Subsystem::General);
    Check(reset.general.diagnosticsEnabled, "general reset restores defaults");
    reset.preview.visible = true;
    ResetSubsystem(reset, Subsystem::Preview);
    Check(!reset.preview.visible, "preview reset restores defaults");
    reset.recording.local.framesPerSecond = 60;
    ResetSubsystem(reset, Subsystem::Recording);
    Check(reset.recording.local.framesPerSecond == defaults.recording.local.framesPerSecond,
          "recording reset restores defaults");
    reset.companion.enabled = true;
    ResetSubsystem(reset, Subsystem::Companion);
    Check(!reset.companion.enabled, "companion reset restores defaults");
    reset.scenes.enabled = true;
    ResetSubsystem(reset, Subsystem::Scenes);
    Check(!reset.scenes.enabled, "scenes reset restores defaults");
    reset.broadcast.enabled = true;
    ResetSubsystem(reset, Subsystem::Broadcast);
    Check(!reset.broadcast.enabled, "broadcast reset restores defaults");
    reset.chat.enabled = true;
    ResetSubsystem(reset, Subsystem::Chat);
    Check(!reset.chat.enabled, "chat reset restores defaults");
    FactoryReset(reset);
    Check(!reset.preview.visible, "factory reset restores all settings");

    const auto testRoot = std::filesystem::temp_directory_path() / "saberstage-settings-tests";
    std::error_code ec;
    std::filesystem::remove_all(testRoot, ec);
    const auto path = testRoot / "settings.json";

    SettingsService first(path);
    const auto firstLoad = first.Load();
    Check(!firstLoad.loadedExisting, "missing settings create defaults");
    Check(std::filesystem::exists(path), "default settings are saved");
    first.Edit().camera.Primary().fovDegrees = 92.0F;
    first.Edit().camera.Primary().position = {1.0F, 2.0F, -4.0F};
    first.Edit().camera.Primary().anchoredFloatMaxOffsetMeters = 1.25F;
    first.Edit().camera.Primary().multisampleCount = 2;
    first.Edit().camera.Primary().keepLevel = true;
    first.Edit().camera.Primary().gizmoVisible = true;
    first.Edit().preview.visible = true;
    first.Edit().preview.position = {0.25F, 1.4F, 2.25F};
    first.Edit().preview.rotationDegrees = {5.0F, 175.0F, 0.0F};
    first.Edit().preview.scale = 1.5F;
    first.Edit().preview.floorResolutionWidth = 960;
    first.Edit().preview.floorFramesPerSecond = 10;
    first.Edit().preview.floatingResolutionWidth = 1280;
    first.Edit().preview.floatingFramesPerSecond = 24;
    first.Edit().recording.gameplayOnly = true;
    first.Edit().recording.worldControlsStreamMode = true;
    first.Edit().recording.worldControlsPosition = {0.45F, 1.35F, 1.55F};
    first.Edit().recording.worldControlsRotationDegrees = {4.0F, 170.0F, -2.0F};
    auto& savedLocalProfile = first.Edit().recording.local;
    savedLocalProfile.resolution = RecordingResolution::P1440;
    savedLocalProfile.framesPerSecond = 60;
    savedLocalProfile.bitrateBitsPerSecond = 16'000'000;
    savedLocalProfile.peakBitrateBitsPerSecond = 20'000'000;
    savedLocalProfile.rateControl = RateControlMode::VariableBitrate;
    savedLocalProfile.encoderPriority = EncoderPriority::Performance;
    savedLocalProfile.h264Profile = H264Profile::Main;
    savedLocalProfile.h264Level = H264Level::L42;
    savedLocalProfile.keyframeIntervalSeconds = 3;
    savedLocalProfile.audioBitrateBitsPerSecond = 192'000;
    savedLocalProfile.gameAudioEnabled = true;
    savedLocalProfile.gameAudioVolumePercent = 85.0F;
    savedLocalProfile.microphoneEnabled = false;
    savedLocalProfile.audio.microphoneMode = MicrophoneMode::PushToTalk;

    auto& savedStreamProfile = first.Edit().recording.livestream;
    savedStreamProfile.resolution = RecordingResolution::P720;
    savedStreamProfile.framesPerSecond = 30;
    savedStreamProfile.bitrateBitsPerSecond = 6'000'000;
    savedStreamProfile.peakBitrateBitsPerSecond = 8'000'000;
    savedStreamProfile.rateControl = RateControlMode::ConstantBitrate;
    savedStreamProfile.encoderPriority = EncoderPriority::Quality;
    savedStreamProfile.h264Profile = H264Profile::High;
    savedStreamProfile.h264Level = H264Level::L41;
    savedStreamProfile.keyframeIntervalSeconds = 2;
    savedStreamProfile.audioBitrateBitsPerSecond = 160'000;
    first.Edit().broadcast.provider = LivestreamProvider::YouTube;
    first.Edit().broadcast.twitch.enabled = false;
    first.Edit().broadcast.youtube.enabled = true;
    first.Edit().broadcast.kick.enabled = true;
    first.Edit().broadcast.custom.enabled = true;
    first.Edit().broadcast.twitch.serverUrl = "rtmps://twitch.example/app";
    first.Edit().broadcast.twitch.streamKey = "test-twitch-key";
    first.Edit().broadcast.twitch.streamTitle = "Test Twitch title";
    first.Edit().broadcast.youtube.serverUrl = "rtmps://youtube.example/live2";
    first.Edit().broadcast.youtube.streamKey = "test-youtube-key";
    first.Edit().broadcast.kick.serverUrl = "rtmps://kick.example/app";
    first.Edit().broadcast.kick.streamKey = "test-kick-key";
    first.Edit().broadcast.custom.serverUrl = "rtmp://custom.example/live";
    first.Edit().broadcast.custom.streamKey = "test-custom-key";
    first.Edit().broadcast.custom.maximumVideoBitrateBitsPerSecond = 14'000'000;
    first.Edit().broadcast.reconnectAttempts = 12;
    first.Edit().broadcast.afkMediaPath = "/sdcard/Pictures/afk.gif";
    first.Edit().broadcast.keepHeadsetAwake = false;
    savedStreamProfile.gameAudioEnabled = false;
    savedStreamProfile.gameAudioVolumePercent = 65.0F;
    savedStreamProfile.microphoneEnabled = true;
    savedStreamProfile.microphoneVolumePercent = 135.0F;
    first.Edit().broadcast.postMapInfoToChat = true;
    savedStreamProfile.audio.microphoneMode = MicrophoneMode::VoiceActivated;
    savedStreamProfile.audio.pushToTalkHand = PushToTalkHand::Right;
    savedStreamProfile.audio.pushToTalkReleaseMilliseconds = 230.0F;
    savedStreamProfile.audio.highPassEnabled = false;
    savedStreamProfile.audio.gateOpenThresholdDb = -33.0F;
    savedStreamProfile.audio.gateCloseThresholdDb = -44.0F;
    savedStreamProfile.audio.gateAttackMilliseconds = 17.0F;
    savedStreamProfile.audio.gateHoldMilliseconds = 260.0F;
    savedStreamProfile.audio.gateReleaseMilliseconds = 310.0F;
    savedStreamProfile.audio.gatePreRollMilliseconds = 50.0F;
    savedStreamProfile.audio.compressorThresholdDb = -21.0F;
    savedStreamProfile.audio.compressorRatio = 4.0F;
    savedStreamProfile.audio.compressorAttackMilliseconds = 13.0F;
    savedStreamProfile.audio.compressorReleaseMilliseconds = 180.0F;
    savedStreamProfile.audio.compressorMakeupDb = 4.0F;
    savedStreamProfile.audio.limiterCeilingDb = -2.0F;
    savedStreamProfile.audio.limiterReleaseMilliseconds = 90.0F;
    first.Edit().tts.enabled = true;
    first.Edit().tts.speakUsernames = false;
    first.Edit().tts.ignoreKnownBots = false;
    first.Edit().tts.ignoreCommands = false;
    first.Edit().tts.speakUrls = true;
    first.Edit().tts.speakEmoteNames = true;
    first.Edit().tts.maximumCharacters = 300;
    first.Edit().tts.queueCapacity = 6;
    first.Edit().tts.staleAfterSeconds = 20.0F;
    first.Edit().tts.volumePercent = 90.0F;
    first.Edit().tts.speechRate = 1.25F;
    first.Edit().tts.voice = "expr-voice-4-f";
    first.Edit().tts.outputRoute = TtsOutputRoute::HeadsetAndBroadcast;
    first.Edit().connectionTest.hasResult = true;
    first.Edit().connectionTest.sustainedDownloadMegabitsPerSecond = 312.5F;
    first.Edit().connectionTest.sustainedUploadMegabitsPerSecond = 18.75F;
    first.Edit().connectionTest.peakDownloadMegabitsPerSecond = 401.0F;
    first.Edit().connectionTest.peakUploadMegabitsPerSecond = 22.5F;
    first.Edit().connectionTest.latencyMilliseconds = 17.0F;
    first.Edit().connectionTest.jitterMilliseconds = 2.5F;
    first.Edit().connectionTest.durationSeconds = 28.0F;
    first.Edit().connectionTest.testedAtUnixSeconds = 1'800'000'123;
    // Saving migrates any alpha-era per-user Client ID to SaberStage's
    // registered public application identifier.
    first.Edit().broadcast.twitchAccount.clientId = "legacy-client-id";
    first.Edit().broadcast.twitchAccount.accessToken = "test-access-token";
    first.Edit().broadcast.twitchAccount.refreshToken = "test-refresh-token";
    first.Edit().broadcast.twitchAccount.protectedTokenEnvelope =
        "ak1:00112233445566778899aabb:00112233445566778899aabbccddeeff";
    first.Edit().broadcast.twitchAccount.login = "test-login";
    first.Edit().broadcast.twitchAccount.userId = "123456";
    first.Edit().broadcast.twitchAccount.expiresAtUnixSeconds = 1'800'000'000;
    first.Edit().broadcast.twitchAccount.chatWriteAuthorized = true;
    first.Edit().chat.enabled = true;
    first.Edit().chat.position = {-0.32F, 1.42F, 1.72F};
    first.Edit().chat.rotationDegrees = {2.0F, 170.0F, -3.0F};
    first.Edit().chat.width = 240.0F;
    first.Edit().chat.height = 200.0F;
    std::string error;
    Check(first.Save(&error), "edited settings save safely");
    Check(Read(path).find("\"profiles\"") != std::string::npos,
          "schema 2 stores a future-ready camera profile collection");
    Check(!std::filesystem::exists(std::filesystem::path(path.string() + ".tmp")), "successful save leaves no temporary file");
    Check(!std::filesystem::exists(std::filesystem::path(path.string() + ".bak")), "successful save leaves no backup file");

    SettingsService second(path);
    const auto secondLoad = second.Load();
    Check(secondLoad.loadedExisting, "existing settings load");
    Check(second.Get().camera.Primary().fovDegrees == 92.0F, "saved value survives restart");
    Check(second.Get().camera.Primary().position.x == 1.0F && second.Get().camera.Primary().position.z == -4.0F,
          "camera profile placement survives restart");
    Check(second.Get().camera.Primary().anchoredFloatMaxOffsetMeters == 1.25F,
          "anchored-float tuning survives restart");
    Check(second.Get().camera.Primary().multisampleCount == 2,
          "third-person camera MSAA survives restart");
    Check(second.Get().camera.Primary().keepLevel,
          "third-person camera level lock survives restart");
    Check(second.Get().camera.Primary().gizmoVisible,
          "persistent HMD camera gizmo visibility survives restart");
    Check(second.Get().preview.visible && second.Get().preview.position.x == 0.25F &&
              second.Get().preview.rotationDegrees.y == 175.0F && second.Get().preview.scale == 1.5F,
          "floating preview pose, scale, and visibility survive restart");
    Check(second.Get().preview.floorResolutionWidth == 960 && second.Get().preview.floorFramesPerSecond == 10 &&
              second.Get().preview.floatingResolutionWidth == 1280 && second.Get().preview.floatingFramesPerSecond == 24,
          "independent preview resolution and FPS settings survive restart");
    Check(second.Get().recording.gameplayOnly, "gameplay-only recording preference survives restart");
    Check(second.Get().recording.worldControlsStreamMode &&
              second.Get().recording.worldControlsPosition.x == 0.45F &&
              second.Get().recording.worldControlsRotationDegrees.y == 170.0F,
          "movable recording controls mode and pose survive restart");
    const auto& loadedLocalProfile = second.Get().recording.local;
    const auto& loadedStreamProfile = second.Get().recording.livestream;
    Check(loadedLocalProfile.resolution == RecordingResolution::P1440 &&
              loadedLocalProfile.framesPerSecond == 60 &&
              loadedLocalProfile.bitrateBitsPerSecond == 16'000'000 &&
              loadedLocalProfile.peakBitrateBitsPerSecond == 20'000'000 &&
              loadedLocalProfile.rateControl == RateControlMode::VariableBitrate &&
              loadedLocalProfile.encoderPriority == EncoderPriority::Performance &&
              loadedLocalProfile.h264Profile == H264Profile::Main &&
              loadedLocalProfile.h264Level == H264Level::L42 &&
              loadedLocalProfile.keyframeIntervalSeconds == 3 &&
              loadedLocalProfile.audioBitrateBitsPerSecond == 192'000 &&
              loadedLocalProfile.gameAudioVolumePercent == 85.0F &&
              !loadedLocalProfile.microphoneEnabled &&
              loadedLocalProfile.audio.microphoneMode == MicrophoneMode::PushToTalk,
          "all local output-profile controls survive restart");
    Check(loadedStreamProfile.resolution == RecordingResolution::P720 &&
              loadedStreamProfile.framesPerSecond == 30 &&
              loadedStreamProfile.bitrateBitsPerSecond == 6'000'000 &&
              loadedStreamProfile.peakBitrateBitsPerSecond == 8'000'000 &&
              loadedStreamProfile.rateControl == RateControlMode::ConstantBitrate &&
              loadedStreamProfile.encoderPriority == EncoderPriority::Quality &&
              loadedStreamProfile.audioBitrateBitsPerSecond == 160'000,
          "stream encoder settings persist independently from local recording");
    const auto serializedSettings = Read(path);
    Check(serializedSettings.find("test-access-token") == std::string::npos &&
              serializedSettings.find("test-refresh-token") == std::string::npos &&
              serializedSettings.find("\"accessToken\"") == std::string::npos &&
              serializedSettings.find("\"refreshToken\"") == std::string::npos,
          "Twitch OAuth plaintext is never serialized to settings");
    Check(second.Get().broadcast.provider == LivestreamProvider::YouTube &&
              !second.Get().broadcast.twitch.enabled &&
              second.Get().broadcast.youtube.enabled &&
              second.Get().broadcast.kick.enabled &&
              second.Get().broadcast.custom.enabled &&
              second.Get().broadcast.twitch.serverUrl == "rtmps://twitch.example/app" &&
              second.Get().broadcast.twitch.streamKey == "test-twitch-key" &&
              second.Get().broadcast.twitch.streamTitle == "Test Twitch title" &&
              second.Get().broadcast.youtube.serverUrl == "rtmps://youtube.example/live2" &&
              second.Get().broadcast.youtube.streamKey == "test-youtube-key" &&
              second.Get().broadcast.kick.serverUrl == "rtmps://kick.example/app" &&
              second.Get().broadcast.kick.streamKey == "test-kick-key" &&
              second.Get().broadcast.custom.serverUrl == "rtmp://custom.example/live" &&
              second.Get().broadcast.custom.streamKey == "test-custom-key" &&
              second.Get().broadcast.custom.maximumVideoBitrateBitsPerSecond == 14'000'000 &&
              second.Get().broadcast.reconnectAttempts == 12 &&
              second.Get().broadcast.afkMediaPath == "/sdcard/Pictures/afk.gif" &&
               !second.Get().broadcast.keepHeadsetAwake &&
               second.Get().broadcast.postMapInfoToChat &&
               second.Get().broadcast.twitchAccount.clientId == kSaberStageTwitchClientId &&
               second.Get().broadcast.twitchAccount.accessToken.empty() &&
               second.Get().broadcast.twitchAccount.refreshToken.empty() &&
               second.Get().broadcast.twitchAccount.protectedTokenEnvelope ==
                   "ak1:00112233445566778899aabb:00112233445566778899aabbccddeeff" &&
               second.Get().broadcast.twitchAccount.login == "test-login" &&
              second.Get().broadcast.twitchAccount.userId == "123456" &&
              second.Get().broadcast.twitchAccount.expiresAtUnixSeconds == 1'800'000'000 &&
              second.Get().broadcast.twitchAccount.chatWriteAuthorized,
          "livestream destinations, AFK media, and protected Twitch account state survive restart");
    Check(!loadedStreamProfile.gameAudioEnabled &&
              loadedStreamProfile.gameAudioVolumePercent == 65.0F &&
              loadedStreamProfile.microphoneEnabled &&
              loadedStreamProfile.microphoneVolumePercent == 135.0F &&
              loadedStreamProfile.audio.microphoneMode == MicrophoneMode::VoiceActivated &&
              loadedStreamProfile.audio.pushToTalkHand == PushToTalkHand::Right &&
              loadedStreamProfile.audio.pushToTalkReleaseMilliseconds == 230.0F &&
              !loadedStreamProfile.audio.highPassEnabled &&
              loadedStreamProfile.audio.gateOpenThresholdDb == -33.0F &&
              loadedStreamProfile.audio.gateCloseThresholdDb == -44.0F &&
              loadedStreamProfile.audio.gateAttackMilliseconds == 17.0F &&
              loadedStreamProfile.audio.gateHoldMilliseconds == 260.0F &&
              loadedStreamProfile.audio.gateReleaseMilliseconds == 310.0F &&
              loadedStreamProfile.audio.gatePreRollMilliseconds == 50.0F &&
              loadedStreamProfile.audio.compressorThresholdDb == -21.0F &&
              loadedStreamProfile.audio.compressorRatio == 4.0F &&
              loadedStreamProfile.audio.compressorAttackMilliseconds == 13.0F &&
              loadedStreamProfile.audio.compressorReleaseMilliseconds == 180.0F &&
              loadedStreamProfile.audio.compressorMakeupDb == 4.0F &&
              loadedStreamProfile.audio.limiterCeilingDb == -2.0F &&
              loadedStreamProfile.audio.limiterReleaseMilliseconds == 90.0F,
          "stream microphone mix and DSP values survive independently from local audio");
    Check(second.Get().tts.enabled && !second.Get().tts.speakUsernames &&
              !second.Get().tts.ignoreKnownBots && !second.Get().tts.ignoreCommands &&
              second.Get().tts.speakUrls && second.Get().tts.speakEmoteNames &&
              second.Get().tts.maximumCharacters == 300 &&
              second.Get().tts.queueCapacity == 6 &&
              second.Get().tts.staleAfterSeconds == 20.0F &&
              second.Get().tts.volumePercent == 90.0F &&
              second.Get().tts.speechRate == 1.25F &&
              second.Get().tts.voice == "expr-voice-4-f" &&
              second.Get().tts.outputRoute == TtsOutputRoute::HeadsetAndBroadcast,
          "Chat TTS filtering, voice, queue, and routing survive restart");
    Check(second.Get().connectionTest.hasResult &&
              second.Get().connectionTest.sustainedDownloadMegabitsPerSecond == 312.5F &&
              second.Get().connectionTest.sustainedUploadMegabitsPerSecond == 18.75F &&
              second.Get().connectionTest.peakDownloadMegabitsPerSecond == 401.0F &&
              second.Get().connectionTest.peakUploadMegabitsPerSecond == 22.5F &&
              second.Get().connectionTest.latencyMilliseconds == 17.0F &&
              second.Get().connectionTest.jitterMilliseconds == 2.5F &&
              second.Get().connectionTest.durationSeconds == 28.0F &&
              second.Get().connectionTest.testedAtUnixSeconds == 1'800'000'123,
          "connection quality results and the stream upload ceiling survive restart");
    Check(second.Get().chat.enabled && second.Get().chat.position.x == -0.32F &&
              second.Get().chat.rotationDegrees.y == 170.0F &&
              second.Get().chat.width == 240.0F && second.Get().chat.height == 200.0F,
          "movable Twitch chat visibility, pose, and doubled maximum size survive restart");
    Check(Read(path).find("\"destinations\"") != std::string::npos,
          "livestream destinations use the service-specific schema");
    auto normalizedPreview = Defaults();
    normalizedPreview.preview.rotationDegrees = {365.0F, -540.0F, 720.0F};
    const auto previewValidation = ValidateAndRepair(normalizedPreview);
    Check(previewValidation.changed,
          "preview angle normalization is reported so the normalized pose is persisted");
    Check(normalizedPreview.preview.rotationDegrees.x == 5.0F &&
              normalizedPreview.preview.rotationDegrees.y == 180.0F &&
              normalizedPreview.preview.rotationDegrees.z == 0.0F,
          "preview angles normalize into the supported range");

    Write(path, R"({"schemaVersion":0,"camera":{"fovDegrees":105.0}})");
    // Old documents have no preview quality fields; decoding must retain each
    // monitor's prior effective defaults instead of sharing recording values.
    SettingsService migration(path);
    const auto migrated = migration.Load();
    Check(migrated.migrated, "schema zero runs migration hook");
    Check(migration.Get().preview.floorResolutionWidth == 1920 &&
              migration.Get().preview.floatingResolutionWidth == 512 &&
              migration.Get().preview.floorFramesPerSecond == 15 && migration.Get().preview.floatingFramesPerSecond == 15,
          "settings without preview quality fields retain their old effective defaults");
    Check(migration.Get().schemaVersion == kCurrentSchemaVersion, "migration writes current schema");
    Check(migration.Get().camera.Primary().fovDegrees == 105.0F, "migration preserves recognized valid value");
    Check(!migration.Get().recording.gameplayOnly, "older settings migrate to continuous recording by default");

    Write(path, R"({"schemaVersion":20,"preview":{"position":{"x":0.0,"y":1.15,"z":2.1},"rotationDegrees":{"x":0.0,"y":180.0,"z":0.0}},"recording":{"worldControlsPosition":{"x":0.42,"y":1.25,"z":1.45},"worldControlsRotationDegrees":{"x":0.0,"y":180.0,"z":0.0}}})");
    SettingsService legacyWorldPanels(path);
    const auto legacyWorldPanelLoad = legacyWorldPanels.Load();
    Check(legacyWorldPanelLoad.migrated &&
              legacyWorldPanels.Get().preview.rotationDegrees.y == 0.0F &&
              legacyWorldPanels.Get().recording.worldControlsRotationDegrees.y == 0.0F,
          "schema 20 untouched world panels migrate from their reversed default face");

    Write(path, R"({"schemaVersion":30,"chat":{"fontSize":3.3}})");
    SettingsService legacySmallChat(path);
    const auto legacySmallChatLoad = legacySmallChat.Load();
    Check(legacySmallChatLoad.migrated &&
              legacySmallChat.Get().chat.fontSize == 4.6F,
          "schema 30 default chat text migrates to the readable Quest size");

    Write(path, R"({"schemaVersion":30,"chat":{"fontSize":5.7}})");
    SettingsService legacyCustomChat(path);
    const auto legacyCustomChatLoad = legacyCustomChat.Load();
    Check(legacyCustomChatLoad.migrated &&
              legacyCustomChat.Get().chat.fontSize == 5.7F,
          "schema 30 custom chat text size is preserved during migration");

    // Schema 35 used one shared encoder profile, kept stream mixing under
    // broadcast, and kept microphone processing/routing in a top-level audio
    // object. Migration must seed both new profiles without losing the user's
    // established recording or stream behavior.
    Write(path, R"({"schemaVersion":35,"recording":{"backend":"hollywood","resolution":"720p","framesPerSecond":30,"bitrateBitsPerSecond":8000000,"peakBitrateBitsPerSecond":10000000,"rateControl":"vbr","encoderPriority":"quality","h264Profile":"high","h264Level":"4.1","keyframeIntervalSeconds":3,"audioBitrateBitsPerSecond":160000},"broadcast":{"gameAudioEnabled":false,"gameAudioVolumePercent":45.0,"microphoneEnabled":true,"microphoneVolumePercent":35.0},"audio":{"microphoneMode":"voice_activated","pushToTalkHand":"right","includeMicrophoneInRecordings":false,"includeMicrophoneInLivestreams":true,"highPassEnabled":false,"gateOpenThresholdDb":-32.0,"gateCloseThresholdDb":-43.0,"compressorEnabled":true,"limiterEnabled":false}})");
    SettingsService legacySharedOutputProfile(path);
    const auto legacySharedProfileLoad = legacySharedOutputProfile.Load();
    const auto& migratedLocal = legacySharedOutputProfile.Get().recording.local;
    const auto& migratedStream = legacySharedOutputProfile.Get().recording.livestream;
    Check(legacySharedProfileLoad.migrated &&
              migratedLocal.resolution == RecordingResolution::P720 &&
              migratedStream.resolution == RecordingResolution::P720 &&
              migratedLocal.framesPerSecond == 30 &&
              migratedStream.framesPerSecond == 30 &&
              migratedLocal.bitrateBitsPerSecond == 8'000'000 &&
              migratedStream.peakBitrateBitsPerSecond == 10'000'000 &&
              migratedLocal.rateControl == RateControlMode::VariableBitrate &&
              migratedStream.encoderPriority == EncoderPriority::Quality &&
              migratedLocal.h264Profile == H264Profile::High &&
              migratedStream.h264Level == H264Level::L41 &&
              migratedLocal.keyframeIntervalSeconds == 3 &&
              migratedStream.audioBitrateBitsPerSecond == 160'000,
          "schema 35 shared encoder settings seed independent local and stream profiles");
    Check(migratedLocal.gameAudioEnabled &&
              !migratedStream.gameAudioEnabled &&
              migratedStream.gameAudioVolumePercent == 45.0F &&
              !migratedLocal.microphoneEnabled &&
              migratedStream.microphoneEnabled &&
              migratedLocal.microphoneVolumePercent == 35.0F &&
              migratedStream.microphoneVolumePercent == 35.0F,
          "schema 35 stream mix and legacy microphone routing retain their prior behavior");
    Check(migratedLocal.audio.microphoneMode == MicrophoneMode::VoiceActivated &&
              migratedStream.audio.microphoneMode == MicrophoneMode::VoiceActivated &&
              migratedLocal.audio.pushToTalkHand == PushToTalkHand::Right &&
              !migratedStream.audio.highPassEnabled &&
              migratedLocal.audio.gateOpenThresholdDb == -32.0F &&
              migratedStream.audio.gateCloseThresholdDb == -43.0F &&
              migratedLocal.audio.compressorEnabled &&
              !migratedStream.audio.limiterEnabled,
          "schema 35 microphone processing is copied into both output profiles");

    Write(path, R"({"schemaVersion":19,"broadcast":{"provider":"kick","serverUrl":"rtmps://legacy-kick.example/app","reconnectAttempts":5}})");
    SettingsService legacyLivestream(path);
    const auto legacyLivestreamLoad = legacyLivestream.Load();
    Check(legacyLivestreamLoad.migrated &&
              legacyLivestream.Get().broadcast.kick.serverUrl ==
                  "rtmps://legacy-kick.example/app" &&
              legacyLivestream.Get().broadcast.twitch.serverUrl ==
                  defaults.broadcast.twitch.serverUrl,
          "schema 19 moves its shared endpoint into only the selected service");
    Check(Read(path).find("\"destinations\"") != std::string::npos,
          "legacy livestream migration rewrites the service-specific schema");

    Write(path, R"({"schemaVersion":36,"broadcast":{"provider":"kick","destinations":{"twitch":{"enabled":true,"serverUrl":"rtmp://twitch.example/app","streamKey":"tw"},"kick":{"serverUrl":"rtmps://kick.example/app","streamKey":"kick"},"youtube":{"enabled":true,"serverUrl":"rtmps://youtube.example/live2","streamKey":"yt"},"custom":{"enabled":true,"serverUrl":"rtmp://custom.example/live","streamKey":"custom"}}}})");
    SettingsService legacySingleDestination(path);
    const auto legacySingleDestinationLoad = legacySingleDestination.Load();
    Check(legacySingleDestinationLoad.migrated &&
              !legacySingleDestination.Get().broadcast.twitch.enabled &&
              legacySingleDestination.Get().broadcast.kick.enabled &&
              !legacySingleDestination.Get().broadcast.youtube.enabled &&
              !legacySingleDestination.Get().broadcast.custom.enabled,
          "schema 36 migration enables only the previously selected destination");

    Write(path, R"({"schemaVersion":25,"broadcast":{"twitchAccount":{"clientId":"legacy-client-id","accessToken":"legacy-access-secret","refreshToken":"legacy-refresh-secret","login":"legacy-login","userId":"654321","expiresAtUnixSeconds":1800000000,"chatWriteAuthorized":true}}})");
    SettingsService legacyPlaintextTwitch(path);
    const auto legacyPlaintextLoad = legacyPlaintextTwitch.Load();
    const auto migratedTwitchJson = Read(path);
    Check(legacyPlaintextLoad.migrated &&
              legacyPlaintextTwitch.Get().broadcast.twitchAccount.accessToken ==
                  "legacy-access-secret" &&
              legacyPlaintextTwitch.Get().broadcast.twitchAccount.refreshToken ==
                  "legacy-refresh-secret",
          "schema 25 Twitch plaintext remains available in memory for one-time Keystore migration");
    Check(migratedTwitchJson.find("legacy-access-secret") == std::string::npos &&
              migratedTwitchJson.find("legacy-refresh-secret") == std::string::npos &&
              migratedTwitchJson.find("\"accessToken\"") == std::string::npos &&
              migratedTwitchJson.find("\"refreshToken\"") == std::string::npos,
          "schema 25 migration immediately removes plaintext Twitch OAuth fields from disk");

    Write(path, R"({"schemaVersion":1,"camera":{"fovDegrees":"invalid","requestedWidth":1281},"chat":{"enabled":"invalid"}})");
    SettingsService wrongTypes(path);
    const auto wrongTypesLoad = wrongTypes.Load();
    Check(wrongTypesLoad.repaired, "wrong JSON field types are reported as repaired");
    Check(wrongTypes.Get().camera.Primary().fovDegrees == defaults.camera.Primary().fovDegrees, "wrong FOV type repairs to default");
    Check(wrongTypes.Get().camera.Primary().requestedWidth == 1280, "valid odd width repairs to an even width");
    SettingsService repairedReload(path);
    const auto repairedReloadResult = repairedReload.Load();
    Check(!repairedReloadResult.repaired, "repaired JSON is persisted in normalized form");

    auto& rich = repairedReload.Edit().chat;
    rich.showEmotes = true; rich.animateEmotes = true; rich.reverseOrder = true;
    rich.backgroundColor = {0.1F, 0.2F, 0.3F}; rich.fontSize = 4.5F;
    rich.controlsPlaced = true; rich.controlsScale = 1.35F;
    rich.requestsPlaced = true; rich.requestsScale = 1.5F;
    rich.requests.enabled = true; rich.requests.maximumPending = 81;
    rich.requests.cooldownPerUser = false; rich.requests.queueCooldownSeconds = 35;
    rich.requests.commands[0] = saberstage::broadcast::CommandPermission::SubscribersAndVips;
    Check(repairedReload.Save(&error), "rich chat configuration persists");
    SettingsService richReload(path); richReload.Load();
    const auto& loadedChat = richReload.Get().chat;
    Check(loadedChat.animateEmotes && loadedChat.reverseOrder && loadedChat.fontSize == 4.5F && loadedChat.backgroundColor.z == 0.3F,
        "rich chat appearance roundtrips");
    Check(loadedChat.controlsPlaced && loadedChat.controlsScale == 1.35F &&
              loadedChat.requestsPlaced && loadedChat.requestsScale == 1.5F,
        "independent chat control/request panel placement persists");
    Check(loadedChat.requests.maximumPending == 81 && !loadedChat.requests.cooldownPerUser && loadedChat.requests.queueCooldownSeconds == 35 &&
        loadedChat.requests.commands[0] == saberstage::broadcast::CommandPermission::SubscribersAndVips, "request policy and permissions roundtrip");

    const std::string futureJson = R"({"schemaVersion":99,"futureOnly":{"keepMe":true}})";
    Write(path, futureJson);
    SettingsService future(path);
    const auto futureLoad = future.Load();
    Check(futureLoad.unsupportedFutureSchema, "future schema is explicitly reported");
    Check(Read(path) == futureJson, "future schema file is not overwritten");

    Write(path, "{broken json");
    SettingsService malformed(path);
    const auto repaired = malformed.Load();
    Check(repaired.repaired, "malformed document restores defaults");
    Check(malformed.Get().camera.Primary().fovDegrees == defaults.camera.Primary().fovDegrees, "malformed fallback is deterministic");

    std::filesystem::remove(path, ec);
    Write(std::filesystem::path(path.string() + ".bak"), R"({"schemaVersion":1,"camera":{"fovDegrees":88.0}})");
    SettingsService recovery(path);
    const auto recovered = recovery.Load();
    Check(recovered.recoveredBackup, "orphaned backup is recovered");
    Check(recovery.Get().camera.Primary().fovDegrees == 88.0F, "backup contents survive recovery");

    Write(std::filesystem::path(path.string() + ".tmp"), "orphaned temporary data");
    Check(recovery.Save(&error), "save replaces an orphaned temporary file safely");
    Check(!std::filesystem::exists(std::filesystem::path(path.string() + ".tmp")), "orphaned temporary file is removed");

    std::filesystem::remove_all(testRoot, ec);
    if (failures == 0) std::cout << "All SaberStage settings tests passed\n";
    return failures == 0 ? 0 : 1;
}
