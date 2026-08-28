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
    const auto validation = ValidateAndRepair(invalid);
    Check(validation.changed && validation.repairedFields >= 7, "invalid fields are repaired individually");
    Check(invalid.camera.Primary().fovDegrees == defaults.camera.Primary().fovDegrees, "invalid FOV repairs to default");
    Check((invalid.camera.Primary().requestedWidth & 1) == 0, "odd encoder dimension becomes even");

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
