#include "saberstage/settings/SettingsModel.hpp"
#include "saberstage/settings/SettingsService.hpp"
#include "saberstage/ui/MenuCopy.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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
    Check(defaults.preview.selectedCameraId == "primary", "preview targets the stable primary camera");
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
              defaults.broadcast.serverUrl.rfind("rtmp://", 0) == 0,
          "livestream defaults use Twitch's ordinary RTMP ingest endpoint");
    Check(defaults.avatar.maximumTextureDimension == 1024, "VRM textures default to the Quest-conscious 1024 cap");
    Check(defaults.avatar.qualityPreset == AvatarQualityPreset::Balanced &&
              defaults.avatar.toonLighting && defaults.avatar.normalMaps &&
              defaults.avatar.rimLighting && !defaults.avatar.matcap &&
              defaults.avatar.emission && defaults.avatar.animatedExpressions &&
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
    Check(defaults.avatar.selectedFile == "avatar.vrm", "avatar profile uses a stable mod-local default filename");
    Check(defaults.avatar.selectedPath.empty(), "avatar profile waits for an on-headset file selection");
    Check(saberstage::ui::copy::LongestLine(saberstage::ui::copy::kScaffoldDescription) <= 32,
          "every scaffold description line fits the narrow menu budget");
    Check(saberstage::ui::copy::LineCount(saberstage::ui::copy::kScaffoldDescription) <= 8,
          "scaffold description fits its reserved menu height");

    auto invalid = defaults;
    invalid.camera.Primary().profileId.clear();
    invalid.camera.Primary().fovDegrees = 500.0F;
    invalid.camera.Primary().requestedWidth = 1279;
    invalid.preview.scale = -1.0F;
    invalid.preview.selectedCameraId = "missing";
    invalid.preview.position.x = 2000.0F;
    invalid.recording.framesPerSecond = 1000;
    invalid.recording.bitrateBitsPerSecond = 20'000'000;
    invalid.recording.peakBitrateBitsPerSecond = 5'000'000;
    invalid.broadcast.reconnectAttempts = 1000;
    invalid.avatar.selectedFile = "../outside.vrm";
    invalid.avatar.selectedPath = "relative/outside.vrm";
    invalid.avatar.maximumTextureDimension = 8192;
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
    const auto validation = ValidateAndRepair(invalid);
    Check(validation.changed && validation.repairedFields >= 7, "invalid fields are repaired individually");
    Check(invalid.camera.Primary().fovDegrees == defaults.camera.Primary().fovDegrees, "invalid FOV repairs to default");
    Check((invalid.camera.Primary().requestedWidth & 1) == 0, "odd encoder dimension becomes even");
    Check(invalid.recording.framesPerSecond == 30, "recording FPS repairs to a supported hardware rate");
    Check(invalid.recording.peakBitrateBitsPerSecond == invalid.recording.bitrateBitsPerSecond,
          "recording peak bitrate repairs to at least the target bitrate");
    Check(invalid.broadcast.reconnectAttempts == defaults.broadcast.reconnectAttempts,
          "livestream reconnect count repairs to its bounded default");
    Check(invalid.avatar.materialStage == AvatarMaterialStage::Configured &&
              invalid.avatar.lightingMode == AvatarLightingMode::Balanced,
          "invalid avatar material diagnostics repair to configured balanced rendering");
    Check(invalid.avatar.sideStepLeanLimitPercent == defaults.avatar.sideStepLeanLimitPercent,
          "invalid side-step lean limit repairs to the original solver boundary");
    Check(invalid.avatar.plantedLegLeanLimitPercent == defaults.avatar.plantedLegLeanLimitPercent,
          "invalid planted-leg lean limit repairs to the original support boundary");
    Check(invalid.avatar.stanceWidthPercent == defaults.avatar.stanceWidthPercent &&
              invalid.avatar.backwardSpineCurveLimitPercent ==
                  defaults.avatar.backwardSpineCurveLimitPercent,
          "invalid stance and backward spine limits repair to original behavior");

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
    first.Edit().preview.visible = true;
    first.Edit().preview.position = {0.25F, 1.4F, 2.25F};
    first.Edit().preview.rotationDegrees = {5.0F, 175.0F, 0.0F};
    first.Edit().preview.scale = 1.5F;
    first.Edit().recording.gameplayOnly = true;
    first.Edit().recording.controllerShortcutEnabled = true;
    first.Edit().recording.worldControlsVisible = true;
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
    first.Edit().broadcast.serverUrl = "rtmps://a.rtmps.youtube.com/live2";
    first.Edit().broadcast.reconnectAttempts = 12;
    first.Edit().avatar.selectedFile = "Black Heart.vrm";
    first.Edit().avatar.selectedPath = "/sdcard/Download/Black Heart.vrm";
    first.Edit().avatar.maximumTextureDimension = 512;
    first.Edit().avatar.qualityPreset = AvatarQualityPreset::Custom;
    first.Edit().avatar.matcap = true;
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
    Check(second.Get().preview.visible && second.Get().preview.position.x == 0.25F &&
              second.Get().preview.rotationDegrees.y == 175.0F && second.Get().preview.scale == 1.5F,
          "floating preview pose, scale, and visibility survive restart");
    Check(second.Get().recording.gameplayOnly, "gameplay-only recording preference survives restart");
    Check(second.Get().recording.controllerShortcutEnabled,
          "controller recording shortcut preference survives restart");
    Check(second.Get().recording.worldControlsVisible &&
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
    Check(second.Get().broadcast.provider == LivestreamProvider::YouTube &&
              second.Get().broadcast.serverUrl == "rtmps://a.rtmps.youtube.com/live2" &&
              second.Get().broadcast.reconnectAttempts == 12,
          "livestream service, endpoint, and reconnect settings survive restart");
    Check(Read(path).find("streamKey") == std::string::npos,
          "stream keys are never persisted in settings JSON");
    Check(second.Get().avatar.selectedFile == "Black Heart.vrm" &&
              second.Get().avatar.selectedPath == "/sdcard/Download/Black Heart.vrm" &&
              second.Get().avatar.maximumTextureDimension == 512 &&
              second.Get().avatar.leftControllerToWrist.position.z == 0.03F &&
              second.Get().avatar.qualityPreset == AvatarQualityPreset::Custom &&
              second.Get().avatar.matcap && !second.Get().avatar.animatedExpressions &&
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
