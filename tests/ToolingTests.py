#!/usr/bin/env python3
"""Host-only safety and repository invariant tests for the Prompt 2 scaffold."""

from __future__ import annotations

import copy
import contextlib
import hashlib
import io
import importlib.util
import json
import pathlib
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location("saberstage_quest_tool", ROOT / "scripts/quest_tool.py")
assert SPEC is not None and SPEC.loader is not None
QUEST_TOOL = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(QUEST_TOOL)


class FakeAdb:
    def __init__(self, receipt=None, remote_hash=None):
        self.receipt = copy.deepcopy(receipt)
        self.remote_hash = remote_hash
        self.receipt_writes = []
        self.pushes = []
        self.shell_commands = []

    def read_json(self, remote):
        self.assert_path(remote, QUEST_TOOL.RECEIPT)
        return copy.deepcopy(self.receipt)

    def hash(self, remote):
        self.assert_path(remote, QUEST_TOOL.REMOTE_LIBRARY)
        return self.remote_hash

    def write_json(self, value, remote):
        self.assert_path(remote, QUEST_TOOL.RECEIPT)
        self.receipt = copy.deepcopy(value)
        self.receipt_writes.append(copy.deepcopy(value))

    def push(self, local, remote):
        self.assert_path(remote, QUEST_TOOL.REMOTE_LIBRARY)
        self.pushes.append((pathlib.Path(local), remote))
        self.remote_hash = hashlib.sha256(pathlib.Path(local).read_bytes()).hexdigest()

    def shell(self, command, check=True):
        self.shell_commands.append((command, check))
        return ""

    @staticmethod
    def assert_path(actual, expected):
        if actual != expected:
            raise AssertionError(f"unexpected remote path {actual!r}; expected {expected!r}")


def complete_receipt(value="abc"):
    return {
        "schemaVersion": 1,
        "state": "complete",
        "path": QUEST_TOOL.REMOTE_LIBRARY,
        "installedSha256": value,
    }


class ReceiptSafetyTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="saberstage-tool-tests-")
        self.previous_root = QUEST_TOOL.ROOT
        QUEST_TOOL.ROOT = pathlib.Path(self.temporary.name)
        (QUEST_TOOL.ROOT / "build").mkdir()
        self.library = QUEST_TOOL.ROOT / "build/libsaberstage.so"
        self.library.write_bytes(b"SaberStage test library")
        self.expected_hash = hashlib.sha256(self.library.read_bytes()).hexdigest()

    def tearDown(self):
        QUEST_TOOL.ROOT = self.previous_root
        self.temporary.cleanup()

    def test_deploy_refuses_unreceipted_existing_library(self):
        adb = FakeAdb(receipt=None, remote_hash="unowned")
        with self.assertRaisesRegex(QUEST_TOOL.ToolError, "MBF-managed"):
            QUEST_TOOL.deploy(adb)
        self.assertEqual(adb.receipt_writes, [])
        self.assertEqual(adb.pushes, [])
        self.assertEqual(adb.shell_commands, [])

    def test_deploy_refuses_changed_receipt_owned_library(self):
        adb = FakeAdb(receipt=complete_receipt("receipt-hash"), remote_hash="changed-hash")
        with self.assertRaisesRegex(QUEST_TOOL.ToolError, "changed since source deployment"):
            QUEST_TOOL.deploy(adb)
        self.assertEqual(adb.receipt_writes, [])
        self.assertEqual(adb.pushes, [])

    def test_deploy_writes_planned_then_complete_receipt(self):
        adb = FakeAdb()
        with contextlib.redirect_stdout(io.StringIO()):
            QUEST_TOOL.deploy(adb)
        self.assertEqual([item["state"] for item in adb.receipt_writes], ["planned", "complete"])
        self.assertEqual(adb.receipt_writes[-1]["installedSha256"], self.expected_hash)
        self.assertEqual(adb.pushes, [(self.library, QUEST_TOOL.REMOTE_LIBRARY)])
        commands = [command for command, _ in adb.shell_commands]
        self.assertEqual(commands[-2], f"am force-stop {QUEST_TOOL.PACKAGE}")
        self.assertEqual(commands[-1], f"monkey -p {QUEST_TOOL.PACKAGE} -c android.intent.category.LAUNCHER 1")

    def test_remove_refuses_unreceipted_existing_library(self):
        adb = FakeAdb(receipt=None, remote_hash="unowned")
        with self.assertRaisesRegex(QUEST_TOOL.ToolError, "MBF-managed"):
            QUEST_TOOL.remove(adb)
        self.assertEqual(adb.shell_commands, [])

    def test_remove_refuses_hash_mismatch(self):
        adb = FakeAdb(receipt=complete_receipt("receipt-hash"), remote_hash="changed-hash")
        with self.assertRaisesRegex(QUEST_TOOL.ToolError, "no longer matches"):
            QUEST_TOOL.remove(adb)
        self.assertEqual(adb.shell_commands, [])

    def test_remove_targets_only_receipt_owned_source_files(self):
        adb = FakeAdb(receipt=complete_receipt("owned"), remote_hash="owned")
        with contextlib.redirect_stdout(io.StringIO()):
            QUEST_TOOL.remove(adb)
        commands = [command for command, _ in adb.shell_commands]
        self.assertEqual(
            commands[0],
            f"rm -f '{QUEST_TOOL.REMOTE_LIBRARY}' '{QUEST_TOOL.RECEIPT}'",
        )
        self.assertNotIn("/Mods/SaberStage/settings.json", commands[0])
        self.assertNotIn("BigScreen", commands[0])


class RepositoryInvariantTests(unittest.TestCase):
    def test_manifest_identity_and_dependencies(self):
        qpm = json.loads((ROOT / "qpm.json").read_text(encoding="utf-8"))
        template = json.loads((ROOT / "mod.template.json").read_text(encoding="utf-8"))
        self.assertEqual(qpm["info"]["id"], "saberstage")
        self.assertEqual(qpm["info"]["version"], "0.1.0")
        self.assertEqual(template["name"], "SaberStage")
        self.assertEqual(template["author"], "Loud160 (AKA Whisp)")
        self.assertEqual(template["packageVersion"], "1.40.8_7379")
        dependencies = {item["id"] for item in qpm["dependencies"]}
        self.assertTrue({"beatsaber-hook", "scotland2", "bsml", "custom-types", "hollywood", "paper2_scotland2"} <= dependencies)

    def test_device_reset_fixture_has_only_planned_nondefaults(self):
        fixture = json.loads((ROOT / "tests/fixtures/prompt2-device-settings.json").read_text(encoding="utf-8"))
        self.assertEqual(fixture["schemaVersion"], 1)
        self.assertIs(fixture["general"]["diagnosticsEnabled"], False)
        self.assertEqual(fixture["camera"]["fovDegrees"], 92.0)
        self.assertEqual(fixture["camera"]["profileId"], "primary")
        self.assertIs(fixture["preview"]["visible"], False)
        self.assertFalse(any(fixture[name]["enabled"] for name in ("companion", "avatar", "scenes", "broadcast", "chat")))

    def test_left_camera_menu_uses_native_side_panel_controls_only(self):
        source = (ROOT / "src/ui/MenuController.cpp").read_text(encoding="utf-8")
        flow = (ROOT / "src/ui/MenuFlowCoordinator.cpp").read_text(encoding="utf-8")
        self.assertIn("RegisterMainMenuFlowCoordinator", source)
        self.assertNotIn("RegisterSettingsMenu", source)
        self.assertGreaterEqual(source.count("CreateUIButton"), 4)
        self.assertIn('"Field of View"', source)
        self.assertIn('"Output Resolution"', source)
        self.assertIn('"Show Movable Preview"', source)
        self.assertIn('"Reset Camera"', source)
        self.assertIn("set_childControlHeight(true)", source)
        self.assertIn("set_childForceExpandHeight(false)", source)
        self.assertIn("set_enableWordWrapping(true)", source)
        self.assertIn("CreateTextSegmentedControl", source)
        self.assertIn("CreateScrollableSettingsContainer", source)
        self.assertIn("BSML::ExternalComponents", source)
        self.assertIn("SelectCellWithNumber(0)", source)
        self.assertIn("ForceUpdateCanvases", source)
        self.assertIn("setting->slider->UpdateVisuals()", source)
        self.assertNotIn('"Close"', source)
        self.assertNotIn("Closes SaberStage", source)
        self.assertIn("tabsRect->set_anchoredPosition({0.0F, -1.5F})", source)
        self.assertIn("scroll->set_sizeDelta({0.0F, -13.0F})", source)
        self.assertIn("ProvideInitialViewControllers", flow)
        self.assertRegex(
            flow,
            r"ProvideInitialViewControllers\(\s*settingsViewController,\s*cameraListViewController,\s*recordingViewController,\s*previewViewController,\s*nullptr",
        )
        self.assertIn("ExplicitRegister", flow)
        self.assertNotIn("Register::AutoRegister", flow)
        self.assertIn('SetTitle("SaberStage | Primary"', flow)
        self.assertIn("set_showBackButton(true)", flow)
        self.assertNotIn("panelRect->set_sizeDelta", source)
        self.assertIn("BuildCameraListPanel", source)
        self.assertIn("BuildTabbedSettings", source)
        self.assertNotIn("tab-selector", source)
        self.assertIn("BuildPreviewPanel", source)
        self.assertIn("RawImageTag", source)
        self.assertIn('"Camera", "Place", "Motion", "Preview"', source)
        self.assertIn("SetEditorPreviewActive(true)", flow)
        self.assertNotIn("RelocateNativeMainScreen", flow)

    def test_recording_uses_primary_camera_and_a_real_mp4_finalization_path(self):
        controller = (ROOT / "src/recording/RecordingController.cpp").read_text(encoding="utf-8")
        menu = (ROOT / "src/ui/MenuController.cpp").read_text(encoding="utf-8")
        pause_menu = (ROOT / "src/ui/PauseMenuRecordingControls.cpp").read_text(encoding="utf-8")
        main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        camera = (ROOT / "src/camera/CameraManager.cpp").read_text(encoding="utf-8")
        self.assertIn("Hollywood::CameraCapture", controller)
        self.assertIn("Hollywood::AudioCapture", controller)
        self.assertIn("Hollywood::MuxFilesSync", controller)
        self.assertIn("RecordingState::Armed", controller)
        self.assertIn("settings_.Get().recording.gameplayOnly", controller)
        self.assertIn("gameplayOnlySession_", controller)
        self.assertIn('name == "GameCore"', controller)
        self.assertIn('kRecordingDemandId = "recording"', controller)
        self.assertIn("BeginExternalRenderOutput", controller)
        self.assertIn("SetExternalOutputTexture(videoCapture_->texture)", controller)
        self.assertIn('".partial.h264"', controller)
        self.assertIn('".partial.wav"', controller)
        self.assertIn('".partial.mp4"', controller)
        self.assertIn("std::filesystem::rename(partialOutput, finalOutput)", controller)
        self.assertIn('"Start Recording"', menu)
        self.assertIn('"Pause Recording"', menu)
        self.assertIn('"Resume Recording"', menu)
        self.assertIn('"Stop & Save"', menu)
        self.assertIn('"Start Recording"', pause_menu)
        self.assertIn('"Pause Recording"', pause_menu)
        self.assertIn('"Resume Recording"', pause_menu)
        self.assertIn('"Stop & Save"', pause_menu)
        self.assertIn("PauseMenuManager_ShowMenu", main)
        self.assertIn("PauseMenuRecordingControls", main)
        self.assertIn('"Record", "Files"', menu)
        self.assertIn('"Gameplay Only"', menu)
        self.assertIn("continuously through menus, songs, and results", menu)
        self.assertIn("externalOutputActive_", camera)
        self.assertIn("Keeping spectator encoder alive across scene transition", camera)
        self.assertIn("RefreshRuntimeCameraFromMain", camera)
        self.assertIn("SpectatorRenderGuard", camera)
        self.assertIn("runtimeCameraReadyHandler_", camera)
        self.assertIn("SaberStage Persistent Game Audio Capture", controller)
        self.assertIn("DontDestroyOnLoad(audioObject_)", controller)
        self.assertIn("RefreshAudioListenerOwnership", controller)
        self.assertIn("RecordingState::Paused", controller)
        self.assertIn("StartVideoSegment()", controller)
        self.assertIn("StopVideoSegment()", controller)
        self.assertIn("accumulatedPaused_", (ROOT / "include/saberstage/recording/RecordingController.hpp").read_text(encoding="utf-8"))

    def test_avatar_picker_uses_absolute_headset_paths_without_filename_entry(self):
        menu = (ROOT / "src/ui/MenuController.cpp").read_text(encoding="utf-8")
        settings = (ROOT / "src/settings/SettingsModel.cpp").read_text(encoding="utf-8")
        self.assertIn('"Choose Avatar File"', menu)
        self.assertIn('"Shared Storage"', menu)
        self.assertIn('"System Root"', menu)
        self.assertIn("std::filesystem::directory_iterator", menu)
        self.assertIn("selectedPath", menu)
        self.assertNotIn("CreateStringSetting(container", menu)
        self.assertIn("selected.is_absolute()", settings)

    def test_recording_buttons_have_explicit_native_layout_size(self):
        menu = (ROOT / "src/ui/MenuController.cpp").read_text(encoding="utf-8")
        self.assertIn('CreateUIButton(recordPage, "Start Recording"', menu)
        self.assertIn('CreateUIButton(recordPage, "Stop & Save"', menu)
        self.assertIn('CreateUIButton(recordPage, "Pause Recording"', menu)
        self.assertIn('CreateUIButton(recordPage, "Resume Recording"', menu)
        self.assertIn("ConfigureLayout(active_->startRecordingButton_", menu)
        self.assertIn("ConfigureLayout(active_->stopRecordingButton_", menu)

    def test_avatar_is_mandatory_in_primary_camera_and_preview_uses_primary_output(self):
        profile = (ROOT / "src/camera/CameraProfile.cpp").read_text(encoding="utf-8")
        layers = (ROOT / "include/saberstage/camera/CameraProfile.hpp").read_text(encoding="utf-8")
        preview = (ROOT / "src/preview/PreviewManager.cpp").read_text(encoding="utf-8")
        runtime = (ROOT / "src/avatar/vrm/VrmUnityRuntime.cpp").read_text(encoding="utf-8")
        self.assertIn("kAvatarLayerMask", layers)
        self.assertIn("kUiLayerMask | kAvatarLayerMask", profile)
        self.assertIn("OutputTexture(camera::kPrimaryCameraId)", preview)
        self.assertGreaterEqual(runtime.count("set_layer(options_.avatarLayer)"), 2)

    def test_avatar_runtime_has_menu_tracking_fallback_and_vrm_texture_st_order(self):
        manager = (ROOT / "src/avatar/AvatarManager.cpp").read_text(encoding="utf-8")
        runtime = (ROOT / "src/avatar/vrm/VrmUnityRuntime.cpp").read_text(encoding="utf-8")
        self.assertIn("FindObjectsOfTypeAll<GlobalNamespace::VRController*>", manager)
        self.assertIn("UnityEngine::Camera::get_main()", manager)
        self.assertIn("SampleControllerPose", manager)
        self.assertIn("tracking_->get_isActiveAndEnabled()", manager)
        self.assertIn("currentHead.ptr() != headTransform_", manager)
        self.assertIn("!controller->get_poseValid()", manager)
        self.assertIn("transformReference ? transformReference.ptr() : nullptr", manager)
        self.assertIn("set_mainTextureOffset({transform->second.x, transform->second.y})", runtime)
        self.assertIn("set_mainTextureScale({transform->second.z, transform->second.w})", runtime)

    def test_recording_retrieval_is_nondestructive_and_timestamped(self):
        tooling = (ROOT / "scripts/quest_tool.py").read_text(encoding="utf-8")
        self.assertIn('subparsers.add_parser("pull-recordings")', tooling)
        self.assertIn("adb.pull(REMOTE_RECORDINGS, destination)", tooling)
        self.assertNotIn("rm -rf", tooling)

    def test_recording_audio_ownership_does_not_retain_unity_wrappers_across_scenes(self):
        controller = (ROOT / "src/recording/RecordingController.cpp").read_text(encoding="utf-8")
        header = (ROOT / "include/saberstage/recording/RecordingController.hpp").read_text(encoding="utf-8")
        self.assertIn("disabledAudioListenerIds_", header)
        self.assertNotIn("std::vector<UnityEngine::AudioListener*>", header)
        self.assertIn("listener->GetInstanceID()", controller)
        self.assertIn("FindObjectsOfTypeAll<UnityEngine::AudioListener*>()", controller)
        self.assertIn("RestoreAudioListenerOwnership();", controller)

    def test_spectator_camera_excludes_preview_and_transitional_ui_surfaces(self):
        camera = (ROOT / "src/camera/CameraManager.cpp").read_text(encoding="utf-8")
        profile = (ROOT / "include/saberstage/camera/CameraProfile.hpp").read_text(encoding="utf-8")
        preview = (ROOT / "src/preview/PreviewManager.cpp").read_text(encoding="utf-8")
        guard = (ROOT / "src/camera/SpectatorRenderGuard.cpp").read_text(encoding="utf-8")
        self.assertIn("excludedLayersMask = 0", profile)
        self.assertIn("kStandardSpectatorLayersMask", profile)
        self.assertIn("ResolveSpectatorCullingMask(profile, mainMask)", camera)
        self.assertIn("ResetWorldToCameraMatrix()", camera)
        self.assertIn("set_useOcclusionCulling(false)", camera)
        self.assertIn("set_pixelRect", camera)
        self.assertIn("RenderSpectatorFrame()", camera)
        self.assertIn("SetCaptureExclusionHandler", camera)
        self.assertIn("Object::Instantiate(mainCamera_)", camera)
        self.assertIn("MainEffectController", camera)
        self.assertIn("ImageEffectController", camera)
        self.assertIn("SetCaptureExcluded(excluded)", preview)
        self.assertIn("captureExclusionDepth_", preview)
        self.assertIn("GetComponentsInChildren<UnityEngine::CanvasRenderer*>(true)", preview)
        self.assertIn("renderer->set_cull(true)", preview)
        self.assertIn("captureCullSnapshot_", preview)
        self.assertIn("CreateWorldSpaceRawImage", preview)
        self.assertIn("object->AddComponent<UnityEngine::UI::RawImage*>()", preview)
        self.assertNotIn("dockedCaptureRoot_->SetActive(false)", preview)
        self.assertNotIn("floatingCaptureRoot_->SetActive(false)", preview)
        self.assertNotIn('#include "UnityEngine/CanvasGroup.hpp"', preview)
        self.assertNotIn("set_alpha(0.0F)", preview)
        self.assertNotIn("object->set_layer(kCaptureExcludedLayer)", preview)
        self.assertIn("RestoreCaptureRoots()", preview)
        self.assertIn('Shader::Find("Unlit/Texture")', preview)
        self.assertIn("previewMaterial_->set_mainTexture(texture)", preview)
        self.assertIn("image->set_material(previewMaterial_)", preview)
        self.assertIn("kFirstPersonLayerMask", profile)
        self.assertNotIn("(1 << 6) |  // first-person avatar", profile)
        self.assertIn("set_depth(1.0F)", camera)
        self.assertIn("ResetCullingMatrix", guard)
        self.assertIn("get_isInTransition()", guard)
        self.assertIn("cachedViewControllers", guard)
        self.assertIn("RefreshViewControllerCacheIfNeeded", guard)
        self.assertIn("group->set_alpha(0.0F)", guard)
        self.assertIn("RestoreTransitioningViewControllers()", guard)
        self.assertNotIn("set_enabled(false)", guard)

    def test_floating_ui_waits_for_bsml_menu_services(self):
        source = (ROOT / "src/preview/PreviewManager.cpp").read_text(encoding="utf-8")
        self.assertIn("FloatingUiServicesReady", source)
        self.assertIn('sceneName == "GameLoader"', source)
        self.assertIn("BSML::Helpers::GetDiContainer() != nullptr", source)
        self.assertIn("if (!existed && FloatingUiServicesReady()) CreateFloatingPreview();", source)
        self.assertIn("if (!FloatingUiServicesReady()) return;", source)

    def test_development_launchers_are_present(self):
        expected = {
            "Build-And-Deploy.bat",
            "Build-And-Deploy-Linux.sh",
            "Collect-SaberStage-Logs.bat",
            "Collect-SaberStage-Logs-Linux.sh",
            "Remove-SaberStage.bat",
            "Remove-SaberStage-Linux.sh",
        }
        self.assertEqual({path.name for path in ROOT.iterdir() if path.name in expected}, expected)
        linux_build = (ROOT / "Build-And-Deploy-Linux.sh").read_text(encoding="utf-8")
        self.assertIn("trap cleanup EXIT", linux_build)

    def test_no_final_project_license_was_chosen(self):
        self.assertFalse((ROOT / "LICENSE").exists())
        self.assertFalse((ROOT / "LICENSE.md").exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
