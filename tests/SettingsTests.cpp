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
    Check(defaults.preview.rotationDegrees.y == 0.0F &&
              defaults.recording.worldControlsRotationDegrees.y == 0.0F,
          "world panels default to the visible FloatingScreen face");
    Check(defaults.chat.width == 70.0F && defaults.chat.height == 58.0F,
          "larger chat size limits do not change the initial or reset size");
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
    Check(!defaults.recording.controllerShortcutEnabled, "controller recording shortcut defaults off");
    Check(!defaults.recording.worldControlsVisible, "movable recording controls default off");
    Check(defaults.recording.backend == RecordingBackend::Hollywood,
          "existing hardware recording backend remains the migration-safe default");
    Check(defaults.recording.resolution == RecordingResolution::P1080 &&
              defaults.recording.framesPerSecond == 30,
          "recording defaults protect gameplay with 1080p30 output");
    Check(defaults.recording.peakBitrateBitsPerSecond >= defaults.recording.bitrateBitsPerSecond,
          "default peak bitrate is not below target bitrate");
    Check(defaults.broadcast.provider == LivestreamProvider::Twitch &&
              defaults.broadcast.twitch.serverUrl.rfind("rtmp://", 0) == 0 &&
              defaults.broadcast.youtube.serverUrl.rfind("rtmps://", 0) == 0 &&
              defaults.broadcast.twitch.streamKey.empty() &&
              defaults.broadcast.youtube.streamKey.empty() &&
              defaults.broadcast.twitchAccount.clientId == kSaberStageTwitchClientId &&
              defaults.broadcast.keepHeadsetAwake &&
              defaults.broadcast.gameAudioEnabled &&
              defaults.broadcast.gameAudioVolumePercent == 100.0F &&
              !defaults.broadcast.microphoneEnabled &&
              defaults.broadcast.microphoneVolumePercent == 100.0F &&
               !defaults.broadcast.postMapInfoToChat &&
               !defaults.broadcast.twitchAccount.chatWriteAuthorized &&
               defaults.broadcast.twitchAccount.protectedTokenEnvelope.empty(),
           "livestream defaults keep independent endpoints and SaberStage's public Twitch identity");
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
    Check(defaults.avatar.maximumTextureDimension == 1024, "VRM textures default to the Quest-conscious 1024 cap");
    Check(defaults.avatar.qualityPreset == AvatarQualityPreset::Balanced &&
              defaults.avatar.toonLighting && defaults.avatar.normalMaps &&
              defaults.avatar.rimLighting && !defaults.avatar.matcap &&
              defaults.avatar.emission && defaults.avatar.animatedExpressions &&
              defaults.avatar.cutoutSmoothing == AvatarCutoutSmoothing::Low &&
              !defaults.avatar.alphaToMaskEnabled &&
              defaults.avatar.outlines == AvatarOutlineMode::Off &&
              defaults.avatar.materialStage == AvatarMaterialStage::Configured &&
              defaults.avatar.lightingMode == AvatarLightingMode::Balanced,
          "Balanced avatar defaults expose each MToon cost independently");
    Check(defaults.avatar.springBones && defaults.avatar.springBoneQuality == SpringBoneQuality::Medium &&
              defaults.avatar.springCollisions == SpringCollisionQuality::Reduced,
          "SpringBones default to a conservative explicit Quest budget");
    Check(defaults.avatar.sideStepLeanLimitPercent == 100.0F,
          "side-step lean override defaults to the original solver boundary");
    Check(defaults.avatar.plantedLegLeanLimitPercent == 100.0F,
          "planted-leg lean override defaults to the original support boundary");
    Check(defaults.avatar.stanceWidthPercent == 100.0F &&
              defaults.avatar.backwardSpineCurveLimitPercent == 100.0F,
          "stance width and backward spine controls default to original solver behavior");
    Check(defaults.avatar.retargetingProfiles.empty() &&
              RetargetingForSelectedAvatar(defaults.avatar).armSpanAvatarSizing &&
              !RetargetingForSelectedAvatar(defaults.avatar).matchPlayerHeight &&
              RetargetingForSelectedAvatar(defaults.avatar).manualAvatarScalePercent == 100.0F &&
              RetargetingForSelectedAvatar(defaults.avatar).keepHandsOnSabers &&
              !RetargetingForSelectedAvatar(defaults.avatar).adjustBodyProportions &&
              RetargetingForSelectedAvatar(defaults.avatar).autoFloorHeight,
          "new avatar fitting defaults preserve arm reach while leaving optional geometry passes off");
    Check(defaults.activeAvatarPlayerProfileId == "default" &&
              defaults.avatarPlayerProfiles.size() == 5 &&
              defaults.avatarPlayerProfiles.front().displayName == "Profile 1" &&
              defaults.avatarPlayerProfiles.back().displayName == "Profile 5",
          "five fixed migration-safe Avatar player slots are available");
    Check(defaults.avatar.selectedFile == "avatar.vrm", "avatar profile uses a stable mod-local default filename");
    Check(defaults.avatar.selectedPath.empty(), "avatar profile waits for an on-headset file selection");
    Check(!defaults.camera.Primary().keepLevel,
          "camera level lock defaults off to preserve existing authored/manual roll");
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
    invalid.recording.framesPerSecond = 1000;
    invalid.recording.bitrateBitsPerSecond = 20'000'000;
    invalid.recording.peakBitrateBitsPerSecond = 5'000'000;
    invalid.broadcast.reconnectAttempts = 1000;
    invalid.broadcast.gameAudioVolumePercent = -20.0F;
    invalid.broadcast.microphoneVolumePercent = 500.0F;
    invalid.broadcast.youtube.serverUrl = "https://not-an-rtmp-endpoint";
    invalid.broadcast.kick.streamKey = "invalid key with spaces";
    invalid.avatar.selectedFile = "../outside.vrm";
    invalid.avatar.selectedPath = "relative/outside.vrm";
    invalid.avatar.maximumTextureDimension = 8192;
    invalid.avatar.cutoutSmoothing = static_cast<AvatarCutoutSmoothing>(99);
    invalid.avatar.materialStage = static_cast<AvatarMaterialStage>(99);
    invalid.avatar.lightingMode = static_cast<AvatarLightingMode>(99);
    invalid.avatar.springUpdateRateHz = 1000;
    invalid.avatar.springSubsteps = 20;
    invalid.avatar.maximumSpringChains = 0;
    invalid.avatar.maximumSpringJoints = 5000;
    invalid.avatar.sideStepLeanLimitPercent = 10.0F;
    invalid.avatar.plantedLegLeanLimitPercent = 10.0F;
    invalid.avatar.stanceWidthPercent = 20.0F;
    invalid.avatar.backwardSpineCurveLimitPercent = 150.0F;
    invalid.avatar.retargetingProfiles.push_back({
        .avatarKey = "/sdcard/Download/Test.vrm",
        .matchPlayerHeight = true,
        .heightAdjustmentBalance = 5.0F,
        .manualAvatarScalePercent = 500.0F,
        .shoulderWidthPercent = 20.0F,
        .waistHipWidthPercent = 300.0F,
        .lowerTorsoWidthPercent = 400.0F,
        .neckBaseWidthPercent = std::numeric_limits<float>::quiet_NaN(),
        .torsoHeightPercent = 10.0F,
        .upperLegLengthPercent = 300.0F,
        .lowerLegLengthPercent = std::numeric_limits<float>::quiet_NaN(),
        .legWidthPercent = 400.0F,
        .neutralKneeBendDegrees = 40.0F,
        .attackPoseDegrees = -80.0F,
        .backStiffnessPercent = 200.0F,
        .floorOffsetMeters = 4.0F});
    const auto validation = ValidateAndRepair(invalid);
    Check(validation.changed && validation.repairedFields >= 7, "invalid fields are repaired individually");
    Check(invalid.camera.Primary().fovDegrees == defaults.camera.Primary().fovDegrees, "invalid FOV repairs to default");
    Check((invalid.camera.Primary().requestedWidth & 1) == 0, "odd encoder dimension becomes even");
    Check(invalid.camera.Primary().multisampleCount == 1,
          "unsupported third-person MSAA repairs to the Quest-safe default");
    Check(invalid.recording.framesPerSecond == 30, "recording FPS repairs to a supported hardware rate");
    Check(invalid.recording.peakBitrateBitsPerSecond == invalid.recording.bitrateBitsPerSecond,
          "recording peak bitrate repairs to at least the target bitrate");
    Check(invalid.broadcast.reconnectAttempts == defaults.broadcast.reconnectAttempts,
          "livestream reconnect count repairs to its bounded default");
    Check(invalid.broadcast.gameAudioVolumePercent == 100.0F &&
              invalid.broadcast.microphoneVolumePercent == 100.0F,
          "livestream audio mix volumes repair to safe defaults");
    Check(invalid.broadcast.youtube.serverUrl == defaults.broadcast.youtube.serverUrl &&
              invalid.broadcast.kick.streamKey.empty(),
          "invalid service-specific livestream destinations repair without exposing credentials");
    Check(invalid.avatar.materialStage == AvatarMaterialStage::Configured &&
              invalid.avatar.lightingMode == AvatarLightingMode::Balanced &&
              invalid.avatar.cutoutSmoothing == AvatarCutoutSmoothing::Low,
          "invalid avatar material diagnostics repair to configured balanced rendering");
    Check(invalid.avatar.sideStepLeanLimitPercent == defaults.avatar.sideStepLeanLimitPercent,
          "invalid side-step lean limit repairs to the original solver boundary");
    Check(invalid.avatar.plantedLegLeanLimitPercent == defaults.avatar.plantedLegLeanLimitPercent,
          "invalid planted-leg lean limit repairs to the original support boundary");
    Check(invalid.avatar.stanceWidthPercent == defaults.avatar.stanceWidthPercent &&
              invalid.avatar.backwardSpineCurveLimitPercent ==
                  defaults.avatar.backwardSpineCurveLimitPercent,
          "invalid stance and backward spine limits repair to original behavior");
    Check(invalid.avatar.retargetingProfiles.size() == 1 &&
              invalid.avatar.retargetingProfiles[0].heightAdjustmentBalance == 0.0F &&
              invalid.avatar.retargetingProfiles[0].manualAvatarScalePercent == 100.0F &&
              invalid.avatar.retargetingProfiles[0].shoulderWidthPercent == 100.0F &&
              invalid.avatar.retargetingProfiles[0].waistHipWidthPercent == 100.0F &&
              invalid.avatar.retargetingProfiles[0].lowerTorsoWidthPercent == 100.0F &&
              invalid.avatar.retargetingProfiles[0].neckBaseWidthPercent == 100.0F &&
              invalid.avatar.retargetingProfiles[0].torsoHeightPercent == 100.0F &&
              invalid.avatar.retargetingProfiles[0].upperLegLengthPercent == 100.0F &&
              invalid.avatar.retargetingProfiles[0].lowerLegLengthPercent == 100.0F &&
              invalid.avatar.retargetingProfiles[0].legWidthPercent == 100.0F &&
              invalid.avatar.retargetingProfiles[0].neutralKneeBendDegrees == 0.0F &&
              invalid.avatar.retargetingProfiles[0].attackPoseDegrees == 0.0F &&
              invalid.avatar.retargetingProfiles[0].backStiffnessPercent == 50.0F &&
              invalid.avatar.retargetingProfiles[0].floorOffsetMeters == 0.0F,
          "invalid per-avatar fit fields repair independently without losing the avatar key");

    auto playerProfiles = defaults;
    playerProfiles.camera.Primary().fovDegrees = 77.0F;
    playerProfiles.avatar.visible = false;
    SyncActiveAvatarPlayerProfile(playerProfiles);
    Check(SwitchAvatarPlayerProfile(playerProfiles, "player-2") && playerProfiles.avatar.visible,
          "unused fixed Avatar player slots start from safe Avatar defaults");
    playerProfiles.avatar.selectedPath = "/sdcard/Download/Player Two.vrm";
    Check(SwitchAvatarPlayerProfile(playerProfiles, "default") &&
              !playerProfiles.avatar.visible &&
              playerProfiles.camera.Primary().fovDegrees == 77.0F,
          "switching players restores Avatar settings without changing shared camera settings");
    Check(SwitchAvatarPlayerProfile(playerProfiles, "player-2") &&
              playerProfiles.avatar.selectedPath == "/sdcard/Download/Player Two.vrm",
          "Avatar settings remain independent between player profiles");

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
    reset.recording.framesPerSecond = 60;
    ResetSubsystem(reset, Subsystem::Recording);
    Check(reset.recording.framesPerSecond == defaults.recording.framesPerSecond, "recording reset restores defaults");
    reset.companion.enabled = true;
    ResetSubsystem(reset, Subsystem::Companion);
    Check(!reset.companion.enabled, "companion reset restores defaults");
    reset.avatar.enabled = true;
    ResetSubsystem(reset, Subsystem::Avatar);
    Check(!reset.avatar.enabled, "avatar reset restores defaults");
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
    first.Edit().preview.visible = true;
    first.Edit().preview.position = {0.25F, 1.4F, 2.25F};
    first.Edit().preview.rotationDegrees = {5.0F, 175.0F, 0.0F};
    first.Edit().preview.scale = 1.5F;
    first.Edit().recording.gameplayOnly = true;
    first.Edit().recording.controllerShortcutEnabled = true;
    first.Edit().recording.worldControlsVisible = true;
    first.Edit().recording.worldControlsStreamMode = true;
    first.Edit().recording.worldControlsPosition = {0.45F, 1.35F, 1.55F};
    first.Edit().recording.worldControlsRotationDegrees = {4.0F, 170.0F, -2.0F};
    first.Edit().recording.backend = RecordingBackend::DirectFfmpegHardware;
    first.Edit().recording.resolution = RecordingResolution::P1440;
    first.Edit().recording.framesPerSecond = 60;
    first.Edit().recording.bitrateBitsPerSecond = 16'000'000;
    first.Edit().recording.peakBitrateBitsPerSecond = 20'000'000;
    first.Edit().recording.rateControl = RateControlMode::VariableBitrate;
    first.Edit().recording.encoderPriority = EncoderPriority::Performance;
    first.Edit().recording.h264Profile = H264Profile::Main;
    first.Edit().recording.h264Level = H264Level::L42;
    first.Edit().recording.keyframeIntervalSeconds = 3;
    first.Edit().recording.audioBitrateBitsPerSecond = 192'000;
    first.Edit().broadcast.provider = LivestreamProvider::YouTube;
    first.Edit().broadcast.twitch.serverUrl = "rtmps://twitch.example/app";
    first.Edit().broadcast.twitch.streamKey = "test-twitch-key";
    first.Edit().broadcast.twitch.streamTitle = "Test Twitch title";
    first.Edit().broadcast.youtube.serverUrl = "rtmps://youtube.example/live2";
    first.Edit().broadcast.youtube.streamKey = "test-youtube-key";
    first.Edit().broadcast.kick.serverUrl = "rtmps://kick.example/app";
    first.Edit().broadcast.kick.streamKey = "test-kick-key";
    first.Edit().broadcast.custom.serverUrl = "rtmp://custom.example/live";
    first.Edit().broadcast.custom.streamKey = "test-custom-key";
    first.Edit().broadcast.reconnectAttempts = 12;
    first.Edit().broadcast.afkMediaPath = "/sdcard/Pictures/afk.gif";
    first.Edit().broadcast.keepHeadsetAwake = false;
    first.Edit().broadcast.gameAudioEnabled = false;
    first.Edit().broadcast.gameAudioVolumePercent = 65.0F;
    first.Edit().broadcast.microphoneEnabled = true;
    first.Edit().broadcast.microphoneVolumePercent = 135.0F;
    first.Edit().broadcast.postMapInfoToChat = true;
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
    first.Edit().avatar.selectedFile = "Black Heart.vrm";
    first.Edit().avatar.selectedPath = "/sdcard/Download/Black Heart.vrm";
    first.Edit().avatar.maximumTextureDimension = 512;
    first.Edit().avatar.qualityPreset = AvatarQualityPreset::Custom;
    first.Edit().avatar.matcap = true;
    first.Edit().avatar.cutoutSmoothing = AvatarCutoutSmoothing::High;
    first.Edit().avatar.alphaToMaskEnabled = true;
    first.Edit().avatar.animatedExpressions = false;
    first.Edit().avatar.outlines = AvatarOutlineMode::Reduced;
    first.Edit().avatar.materialStage = AvatarMaterialStage::RimLighting;
    first.Edit().avatar.lightingMode = AvatarLightingMode::Studio;
    first.Edit().avatar.springBoneQuality = SpringBoneQuality::Custom;
    first.Edit().avatar.springCollisions = SpringCollisionQuality::Full;
    first.Edit().avatar.springUpdateRateHz = 40;
    first.Edit().avatar.springSubsteps = 2;
    first.Edit().avatar.maximumSpringChains = 48;
    first.Edit().avatar.maximumSpringJoints = 160;
    first.Edit().avatar.sideStepLeanLimitPercent = 65.0F;
    first.Edit().avatar.plantedLegLeanLimitPercent = 55.0F;
    first.Edit().avatar.stanceWidthPercent = 145.0F;
    first.Edit().avatar.backwardSpineCurveLimitPercent = 35.0F;
    auto& savedFit = EditRetargetingForSelectedAvatar(first.Edit().avatar);
    savedFit.armSpanAvatarSizing = false;
    savedFit.matchPlayerHeight = true;
    savedFit.heightAdjustmentBalance = -0.35F;
    savedFit.manualAvatarScaleEnabled = true;
    savedFit.manualAvatarScalePercent = 137.0F;
    savedFit.keepHandsOnSabers = false;
    savedFit.gripOffsetsInitialized = true;
    savedFit.leftControllerToWrist.position = {0.011F, -0.022F, 0.033F};
    savedFit.leftControllerToWrist.rotationDegrees = {4.0F, 5.0F, 6.0F};
    savedFit.leftControllerToWrist.gripClosurePercent = 135.0F;
    savedFit.leftControllerToWrist.thumbCurvePercent = 120.0F;
    savedFit.rightControllerToWrist.position = {-0.014F, 0.025F, 0.036F};
    savedFit.rightControllerToWrist.rotationDegrees = {-7.0F, 8.0F, -9.0F};
    savedFit.rightControllerToWrist.gripClosurePercent = 65.0F;
    savedFit.rightControllerToWrist.thumbCurvePercent = 75.0F;
    savedFit.adjustBodyProportions = true;
    savedFit.torsoWidthPercent = 116.0F;
    savedFit.autoShoulderWidth = true;
    savedFit.shoulderWidthPercent = 121.0F;
    savedFit.waistHipWidthPercent = 94.0F;
    savedFit.lowerTorsoWidthPercent = 112.0F;
    savedFit.neckBaseWidthPercent = 108.0F;
    savedFit.headSizePercent = 125.0F;
    savedFit.torsoHeightPercent = 106.0F;
    savedFit.upperLegLengthPercent = 109.0F;
    savedFit.lowerLegLengthPercent = 96.0F;
    savedFit.legWidthPercent = 118.0F;
    savedFit.neutralKneeBendDegrees = 7.0F;
    savedFit.attackPoseDegrees = 9.0F;
    savedFit.backStiffnessPercent = 73.0F;
    savedFit.autoFloorHeight = false;
    savedFit.floorOffsetMeters = -0.035F;
    savedFit.preventArmBodyClipping = true;
    savedFit.armSpringBoneInteraction = true;
    first.Edit().avatar.leftControllerToWrist.position = {0.01F, -0.02F, 0.03F};
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
    Check(second.Get().preview.visible && second.Get().preview.position.x == 0.25F &&
              second.Get().preview.rotationDegrees.y == 175.0F && second.Get().preview.scale == 1.5F,
          "floating preview pose, scale, and visibility survive restart");
    Check(second.Get().recording.gameplayOnly, "gameplay-only recording preference survives restart");
    Check(second.Get().recording.controllerShortcutEnabled,
          "controller recording shortcut preference survives restart");
    Check(second.Get().recording.worldControlsVisible &&
              second.Get().recording.worldControlsStreamMode &&
              second.Get().recording.worldControlsPosition.x == 0.45F &&
              second.Get().recording.worldControlsRotationDegrees.y == 170.0F,
          "movable recording controls visibility and pose survive restart");
    Check(second.Get().recording.backend == RecordingBackend::DirectFfmpegHardware &&
              second.Get().recording.resolution == RecordingResolution::P1440 &&
              second.Get().recording.framesPerSecond == 60 &&
              second.Get().recording.bitrateBitsPerSecond == 16'000'000 &&
              second.Get().recording.peakBitrateBitsPerSecond == 20'000'000 &&
              second.Get().recording.rateControl == RateControlMode::VariableBitrate &&
              second.Get().recording.encoderPriority == EncoderPriority::Performance &&
              second.Get().recording.h264Profile == H264Profile::Main &&
              second.Get().recording.h264Level == H264Level::L42 &&
              second.Get().recording.keyframeIntervalSeconds == 3 &&
              second.Get().recording.audioBitrateBitsPerSecond == 192'000,
          "all direct hardware encoder controls survive restart");
    const auto serializedSettings = Read(path);
    Check(serializedSettings.find("test-access-token") == std::string::npos &&
              serializedSettings.find("test-refresh-token") == std::string::npos &&
              serializedSettings.find("\"accessToken\"") == std::string::npos &&
              serializedSettings.find("\"refreshToken\"") == std::string::npos,
          "Twitch OAuth plaintext is never serialized to settings");
    Check(second.Get().broadcast.provider == LivestreamProvider::YouTube &&
              second.Get().broadcast.twitch.serverUrl == "rtmps://twitch.example/app" &&
              second.Get().broadcast.twitch.streamKey == "test-twitch-key" &&
              second.Get().broadcast.twitch.streamTitle == "Test Twitch title" &&
              second.Get().broadcast.youtube.serverUrl == "rtmps://youtube.example/live2" &&
              second.Get().broadcast.youtube.streamKey == "test-youtube-key" &&
              second.Get().broadcast.kick.serverUrl == "rtmps://kick.example/app" &&
              second.Get().broadcast.kick.streamKey == "test-kick-key" &&
              second.Get().broadcast.custom.serverUrl == "rtmp://custom.example/live" &&
              second.Get().broadcast.custom.streamKey == "test-custom-key" &&
              second.Get().broadcast.reconnectAttempts == 12 &&
              second.Get().broadcast.afkMediaPath == "/sdcard/Pictures/afk.gif" &&
               !second.Get().broadcast.keepHeadsetAwake &&
               !second.Get().broadcast.gameAudioEnabled &&
               second.Get().broadcast.gameAudioVolumePercent == 65.0F &&
               second.Get().broadcast.microphoneEnabled &&
               second.Get().broadcast.microphoneVolumePercent == 135.0F &&
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
    Check(second.Get().chat.enabled && second.Get().chat.position.x == -0.32F &&
              second.Get().chat.rotationDegrees.y == 170.0F &&
              second.Get().chat.width == 240.0F && second.Get().chat.height == 200.0F,
          "movable Twitch chat visibility, pose, and doubled maximum size survive restart");
    Check(Read(path).find("\"destinations\"") != std::string::npos,
          "livestream destinations use the service-specific schema");
    Check(second.Get().avatar.selectedFile == "Black Heart.vrm" &&
              second.Get().avatar.selectedPath == "/sdcard/Download/Black Heart.vrm" &&
              second.Get().avatar.maximumTextureDimension == 512 &&
              second.Get().avatar.leftControllerToWrist.position.z == 0.03F &&
              second.Get().avatar.qualityPreset == AvatarQualityPreset::Custom &&
              second.Get().avatar.matcap &&
              second.Get().avatar.cutoutSmoothing == AvatarCutoutSmoothing::High &&
              second.Get().avatar.alphaToMaskEnabled &&
              !second.Get().avatar.animatedExpressions &&
              second.Get().avatar.outlines == AvatarOutlineMode::Reduced &&
              second.Get().avatar.materialStage == AvatarMaterialStage::RimLighting &&
              second.Get().avatar.lightingMode == AvatarLightingMode::Studio &&
              second.Get().avatar.springBoneQuality == SpringBoneQuality::Custom &&
              second.Get().avatar.springCollisions == SpringCollisionQuality::Full &&
              second.Get().avatar.springUpdateRateHz == 40 && second.Get().avatar.springSubsteps == 2 &&
              second.Get().avatar.maximumSpringChains == 48 && second.Get().avatar.maximumSpringJoints == 160 &&
              second.Get().avatar.sideStepLeanLimitPercent == 65.0F &&
              second.Get().avatar.plantedLegLeanLimitPercent == 55.0F &&
              second.Get().avatar.stanceWidthPercent == 145.0F &&
              second.Get().avatar.backwardSpineCurveLimitPercent == 35.0F,
          "avatar selection, visual quality, SpringBone budget, and wrist calibration survive restart");
    const auto loadedFit = RetargetingForSelectedAvatar(second.Get().avatar);
    Check(loadedFit.avatarKey == "/sdcard/Download/Black Heart.vrm" &&
              !loadedFit.armSpanAvatarSizing && loadedFit.matchPlayerHeight &&
              loadedFit.heightAdjustmentBalance == -0.35F &&
              loadedFit.manualAvatarScaleEnabled && loadedFit.manualAvatarScalePercent == 137.0F &&
              !loadedFit.keepHandsOnSabers && loadedFit.gripOffsetsInitialized &&
              loadedFit.leftControllerToWrist.position.z == 0.033F &&
              loadedFit.leftControllerToWrist.gripClosurePercent == 135.0F &&
              loadedFit.leftControllerToWrist.thumbCurvePercent == 120.0F &&
              loadedFit.rightControllerToWrist.rotationDegrees.z == -9.0F &&
              loadedFit.rightControllerToWrist.gripClosurePercent == 65.0F &&
              loadedFit.rightControllerToWrist.thumbCurvePercent == 75.0F &&
              loadedFit.adjustBodyProportions && loadedFit.autoShoulderWidth &&
              loadedFit.torsoWidthPercent == 116.0F &&
              loadedFit.shoulderWidthPercent == 121.0F &&
              loadedFit.waistHipWidthPercent == 94.0F &&
              loadedFit.lowerTorsoWidthPercent == 112.0F &&
              loadedFit.neckBaseWidthPercent == 108.0F &&
              loadedFit.headSizePercent == 125.0F &&
              loadedFit.torsoHeightPercent == 106.0F &&
              loadedFit.upperLegLengthPercent == 109.0F &&
              loadedFit.lowerLegLengthPercent == 96.0F &&
              loadedFit.legWidthPercent == 118.0F &&
              loadedFit.neutralKneeBendDegrees == 7.0F &&
              loadedFit.attackPoseDegrees == 9.0F &&
              loadedFit.backStiffnessPercent == 73.0F &&
              !loadedFit.autoFloorHeight && loadedFit.floorOffsetMeters == -0.035F &&
              loadedFit.preventArmBodyClipping && loadedFit.armSpringBoneInteraction,
          "all per-avatar fit, grip, posture, floor, and collision controls survive restart");
    const auto savedSecondPlayerId = std::string("player-2");
    Check(SwitchAvatarPlayerProfile(second.Edit(), savedSecondPlayerId),
          "second fixed player slot can be selected");
    second.Edit().avatar.selectedPath = "/sdcard/Download/Second Player.vrm";
    second.Edit().avatar.visible = false;
    Check(second.Save(&error), "multiple Avatar player profiles save safely");
    SettingsService third(path);
    Check(third.Load().loadedExisting &&
              third.Get().activeAvatarPlayerProfileId == savedSecondPlayerId &&
              third.Get().avatarPlayerProfiles.size() == 5 &&
              third.Get().avatar.selectedPath == "/sdcard/Download/Second Player.vrm" &&
              !third.Get().avatar.visible,
          "active player identity and complete Avatar-only profile survive restart");

    auto preset = defaults.avatar;
    ApplyAvatarQualityPreset(preset, AvatarQualityPreset::Performance);
    Check(preset.maximumTextureDimension == 512 && !preset.normalMaps && !preset.rimLighting &&
              preset.outlines == AvatarOutlineMode::Off &&
              preset.materialStage == AvatarMaterialStage::Configured &&
              preset.lightingMode == AvatarLightingMode::Balanced &&
              preset.springBoneQuality == SpringBoneQuality::Low,
          "Performance preset assigns only visible individual avatar controls");
    ApplyAvatarQualityPreset(preset, AvatarQualityPreset::Quality);
    Check(preset.maximumTextureDimension == 2048 && preset.normalMaps && preset.rimLighting && preset.matcap &&
              preset.outlines == AvatarOutlineMode::Full && preset.springBoneQuality == SpringBoneQuality::High,
          "Quality preset assigns the documented high-fidelity controls");

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
    SettingsService migration(path);
    const auto migrated = migration.Load();
    Check(migrated.migrated, "schema zero runs migration hook");
    Check(migration.Get().schemaVersion == kCurrentSchemaVersion, "migration writes current schema");
    Check(migration.Get().camera.Primary().fovDegrees == 105.0F, "migration preserves recognized valid value");
    Check(!migration.Get().recording.gameplayOnly, "older settings migrate to continuous recording by default");
    Check(!migration.Get().recording.controllerShortcutEnabled,
          "older settings migrate with the controller shortcut disabled");
    Check(!migration.Get().recording.worldControlsVisible,
          "older settings migrate with movable recording controls disabled");

    Write(path, R"({"schemaVersion":20,"preview":{"position":{"x":0.0,"y":1.15,"z":2.1},"rotationDegrees":{"x":0.0,"y":180.0,"z":0.0}},"recording":{"worldControlsPosition":{"x":0.42,"y":1.25,"z":1.45},"worldControlsRotationDegrees":{"x":0.0,"y":180.0,"z":0.0}}})");
    SettingsService legacyWorldPanels(path);
    const auto legacyWorldPanelLoad = legacyWorldPanels.Load();
    Check(legacyWorldPanelLoad.migrated &&
              legacyWorldPanels.Get().preview.rotationDegrees.y == 0.0F &&
              legacyWorldPanels.Get().recording.worldControlsRotationDegrees.y == 0.0F,
          "schema 20 untouched world panels migrate from their reversed default face");

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
