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
        self.remote_hashes = (
            dict(remote_hash)
            if isinstance(remote_hash, dict)
            else ({QUEST_TOOL.REMOTE_LIBRARY: remote_hash} if remote_hash is not None else {})
        )
        self.receipt_writes = []
        self.pushes = []
        self.shell_commands = []

    def read_json(self, remote):
        self.assert_path(remote, QUEST_TOOL.RECEIPT)
        return copy.deepcopy(self.receipt)

    def hash(self, remote):
        if remote not in {item[1] for item in QUEST_TOOL.deployment_payloads()}:
            raise AssertionError(f"unexpected deployment path {remote!r}")
        return self.remote_hashes.get(remote)

    def write_json(self, value, remote):
        self.assert_path(remote, QUEST_TOOL.RECEIPT)
        self.receipt = copy.deepcopy(value)
        self.receipt_writes.append(copy.deepcopy(value))

    def push(self, local, remote):
        if remote not in {item[1] for item in QUEST_TOOL.deployment_payloads()}:
            raise AssertionError(f"unexpected deployment path {remote!r}")
        self.pushes.append((pathlib.Path(local), remote))
        self.remote_hashes[remote] = hashlib.sha256(pathlib.Path(local).read_bytes()).hexdigest()

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
        self.payloads = QUEST_TOOL.deployment_payloads()
        for index, (local, _) in enumerate(self.payloads):
            local.parent.mkdir(parents=True, exist_ok=True)
            local.write_bytes(f"SaberStage test payload {index}".encode())
        self.library = self.payloads[0][0]
        self.expected_hashes = {
            remote: hashlib.sha256(local.read_bytes()).hexdigest()
            for local, remote in self.payloads
        }

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
        self.assertEqual(adb.receipt_writes[-1]["schemaVersion"], 2)
        self.assertEqual(
            {item["path"]: item["installedSha256"] for item in adb.receipt_writes[-1]["files"]},
            self.expected_hashes,
        )
        self.assertEqual(adb.pushes, list(self.payloads))
        commands = [command for command, _ in adb.shell_commands]
        self.assertEqual(commands[-2], f"am force-stop {QUEST_TOOL.PACKAGE}")
        self.assertEqual(commands[-1], f"monkey -p {QUEST_TOOL.PACKAGE} -c android.intent.category.LAUNCHER 1")

    def test_deploy_can_verify_without_launching_game(self):
        adb = FakeAdb()
        with contextlib.redirect_stdout(io.StringIO()):
            QUEST_TOOL.deploy(adb, launch=False)
        self.assertEqual(adb.shell_commands, [])

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
        self.assertEqual(
            set(template["libraryFiles"]),
            {"libavformat-saberstage9.so", "libavcodec-saberstage9.so", "libavutil-saberstage9.so"},
        )
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
        self.assertIn("RealtimeAudioCapture", controller)
        self.assertIn("DirectFfmpegCapture", controller)
        self.assertIn("MuxSaberStageRecording", controller)
        self.assertNotIn("Hollywood::MuxFilesSync", controller)
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
        self.assertIn('"Record", "Live Stream", "Files"', menu)
        self.assertIn('"Gameplay Only"', menu)
        self.assertIn("record menus, results, and songs continuously", menu)
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
        self.assertIn("ConfigureRightPanelButton(active_->startRecordingButton_", menu)
        self.assertIn("ConfigureRightPanelButton(active_->stopRecordingButton_", menu)

    def test_movable_recording_panel_is_compact_persistent_and_capture_excluded(self):
        menu = (ROOT / "src/ui/MenuController.cpp").read_text(encoding="utf-8")
        settings = (ROOT / "src/settings/SettingsService.cpp").read_text(encoding="utf-8")
        self.assertIn('"Floating Recording Controls"', menu)
        self.assertIn('"▶"', menu)
        self.assertIn('"Ⅱ"', menu)
        self.assertIn('"■"', menu)
        self.assertIn("RecordingOutputTypeName(snapshot.outputType)", menu)
        self.assertIn("RecordingElapsed(snapshot.elapsedSeconds)", menu)
        self.assertIn("HideAndPlaceWorldPanelHandleInPadding", menu)
        self.assertIn("kRecordingPanelBodySize", menu)
        self.assertIn("RegisterCaptureExcludedRoot(screenObject)", menu)
        self.assertNotIn("SaberStage Recording Grab Bar", menu)
        self.assertIn('"worldControlsVisible"', settings)
        self.assertIn('"worldControlsPosition"', settings)
        self.assertIn('"worldControlsRotationDegrees"', settings)

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
        application = (ROOT / "src/app/ApplicationRoot.cpp").read_text(encoding="utf-8")
        self.assertIn('subparsers.add_parser("pull-recordings")', tooling)
        self.assertIn('REMOTE_RECORDINGS = "/sdcard/Oculus/VideoShots"', tooling)
        self.assertIn('kQuestVideoShotsDirectory{"/sdcard/Oculus/VideoShots"}', application)
        self.assertIn("SaberStage_*.mp4", tooling)
        self.assertIn("adb.pull(remote, destination / pathlib.PurePosixPath(remote).name)", tooling)
        self.assertNotIn("rm -rf", tooling)

    def test_recording_audio_ownership_does_not_retain_unity_wrappers_across_scenes(self):
        controller = (ROOT / "src/recording/RecordingController.cpp").read_text(encoding="utf-8")
        header = (ROOT / "include/saberstage/recording/RecordingController.hpp").read_text(encoding="utf-8")
        self.assertIn("disabledAudioListenerIds_", header)
        self.assertNotIn("std::vector<UnityEngine::AudioListener*>", header)
        self.assertIn("listener->GetInstanceID()", controller)
        self.assertIn("FindObjectsOfTypeAll<UnityEngine::AudioListener*>()", controller)
        self.assertIn("RestoreAudioListenerOwnership();", controller)

    def test_spectator_camera_keeps_popout_visible_but_excludes_docked_preview(self):
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
        self.assertIn("cachedCaptureCanvasGroups_", preview)
        self.assertIn("group->set_alpha(0.0F)", preview)
        self.assertIn("captureCanvasGroupSnapshot_", preview)
        self.assertIn("capturePreviewCullSnapshot_", preview)
        self.assertIn("cullPreview(dockedImage_)", preview)
        self.assertNotIn("cullPreview(floatingImage_)", preview)
        self.assertNotIn("cacheRoot(floatingCaptureRoot_)", preview)
        self.assertNotIn("UnityEngine::GameObject* floatingCaptureRoot_", preview)
        self.assertIn("Canvas::ForceUpdateCanvases()", preview)
        self.assertIn("renderer->set_cull(false)", preview)
        self.assertIn("CreateWorldSpaceVideoSurface", preview)
        self.assertIn("PublicRawImageTag{}.Create(parent)", preview)
        self.assertIn("image->set_material(material)", preview)
        self.assertIn("floatingImage_->set_texture(texture)", preview)
        self.assertIn("floatingImage_->SetMaterialDirty()", preview)
        self.assertNotIn("GameObject::CreatePrimitive(UnityEngine::PrimitiveType::Quad)", preview)
        self.assertIn("GetComponentsInChildren<UnityEngine::MeshRenderer*>(true)", preview)
        self.assertIn("captureMeshSnapshot_", preview)
        self.assertNotIn("dockedCaptureRoot_->SetActive(false)", preview)
        self.assertNotIn("floatingCaptureRoot_->SetActive(false)", preview)
        self.assertIn('#include "UnityEngine/CanvasGroup.hpp"', preview)
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

    def test_calibration_wizard_uses_bigscreen_panel_and_is_review_gated(self):
        menu = (ROOT / "src/ui/MenuController.cpp").read_text(encoding="utf-8")
        avatar = (ROOT / "src/avatar/AvatarManager.cpp").read_text(encoding="utf-8")
        session = (ROOT / "src/avatar/calibration/PlayerCalibrationSession.cpp").read_text(encoding="utf-8")
        self.assertIn('"SaberStage Player Calibration"', menu)
        self.assertIn("HideAndFitCalibrationPanelHandleAboveControls", menu)
        self.assertIn("calibrationPanelScreen_->set_HandleSide(BSML::Side::Top)", menu)
        self.assertIn("calibrationPanelScreen_->set_HighlightHandle(false)", menu)
        self.assertIn("a physics hit there wins over Unity's UI raycast", menu)
        self.assertIn('"Recenter Panel",', menu)
        self.assertIn('introductionActions->get_transform(), "Start Automatic"', menu)
        self.assertIn('introductionActions->get_transform(), "Start Step-by-Step"', menu)
        self.assertIn("WAITING FOR AVATAR TRACKING", menu)
        self.assertIn("NATURAL READY POSE", menu)
        self.assertIn("MOVE INTO THE POSE DURING THIS COUNTDOWN", menu)
        self.assertIn("DO NOT MOVE DURING THIS COUNTDOWN", menu)
        self.assertIn("MEASURING - HOLD STILL", menu)
        self.assertIn("MEASURING - MOVE NOW", menu)
        self.assertIn("calibrationPanelAutomaticStartButton_->set_interactable(trackingReady)", menu)
        self.assertIn("calibrationPanelStepByStepStartButton_->set_interactable(trackingReady)", menu)
        self.assertIn("IsPlayerCalibrationReady()", menu)
        self.assertIn("Preparing only opens the explanatory calibration wizard", avatar)
        self.assertIn('if (!bound_ || !trackingWasReady_) {\n            if (error) *error = "valid HMD/controller tracking is required to start calibration";', avatar)
        self.assertIn('stepStartActions->get_transform(), "Start Step"', menu)
        self.assertIn('continueActions->get_transform(), "Continue"', menu)
        self.assertIn('reviewActions->get_transform(), "Complete"', menu)
        self.assertIn(
            'createActionContainer("SaberStage Calibration Review Actions", 2.0F)', menu)
        self.assertIn("ConfigureCalibrationButton(reviewComplete, {30.0F, 7.0F})", menu)
        self.assertIn("BSML::Lite::SetButtonTextSize(button, 3.4F)", menu)
        self.assertIn("kCalibrationPanelScale", menu)
        self.assertIn("ConfigureCalibrationPanelText", menu)
        self.assertIn("const float contentWidth = kCalibrationPanelSize.x - 8.0F", menu)
        self.assertIn("auto* rootLayout = BSML::Lite::CreateVerticalLayoutGroup(panelParent)", menu)
        self.assertIn("NeutralizeContentSizeFitter(rootLayout)", menu)
        self.assertIn("rootRect->set_sizeDelta(kCalibrationPanelSize)", menu)
        self.assertIn("SaberStage \" VERSION \"  |  Build \" SABERSTAGE_BUILD_NUMBER", menu)
        self.assertIn("calibrationPanelGeometryAuditFrames_ = 2", menu)
        self.assertIn("LogCalibrationPanelGeometry()", menu)
        self.assertIn("rootLayout->set_childControlWidth(true)", menu)
        self.assertIn("ConfigureLayout(text, -1.0F, preferredHeight", menu)
        self.assertNotIn('"SaberStage Calibration Content"', menu)
        self.assertNotIn("curved->SetRadius(10000.0F)", menu)
        self.assertNotIn("text->SetAllDirty()", menu)
        self.assertIn("photographed single glyph and line-shaped button captions", menu)
        self.assertNotIn("UpdateCalibrationPanelFollow", menu)
        self.assertNotIn("kCalibrationPanelFollowSeconds", menu)
        self.assertIn("horizontalHeadRotation", menu)
        self.assertNotIn("headEuler.y + 180.0F", menu)
        self.assertIn("transform->SetPositionAndRotation(targetPosition, targetRotation)", menu)
        self.assertIn("PlayCalibrationClip(calibrationTickClip_)", avatar)
        self.assertIn("PlayCalibrationClip(calibrationShutterClip_)", avatar)
        self.assertIn("set_ignoreListenerPause(true)", avatar)
        self.assertNotIn("PlayOneShot(calibration", avatar)
        self.assertIn("status_.phase = CalibrationPhase::Review", session)
        self.assertIn("status_.phase = CalibrationPhase::Introduction", session)
        self.assertIn("status_.phase = CalibrationPhase::AwaitingStepStart", session)
        self.assertIn("status_.phase = CalibrationPhase::AwaitingContinue", session)
        self.assertIn("bool PlayerCalibrationSession::Complete", session)
        self.assertLess(
            session.index("bool PlayerCalibrationSession::Complete"),
            session.index("SavePlayerCalibrationProfile(profilePath_, profile_"),
        )

    def test_recording_side_panel_overrides_center_panel_prefab_widths(self):
        menu = (ROOT / "src/ui/MenuController.cpp").read_text(encoding="utf-8")
        self.assertIn("layout->set_preferredWidth(48.0F)", menu)
        self.assertIn("ConfigureFullWidthRightPanelInput(active_->livestreamKeyInput_, 512)", menu)
        self.assertIn('CreateRightPanelSubheader(livestreamPage->get_transform(), "Server Address")', menu)
        self.assertIn('CreateRightPanelSubheader(livestreamPage->get_transform(), "Stream Key")', menu)
        self.assertIn('"Show Stream Key"', menu)
        self.assertIn("display.assign(display.size(), '*')", menu)
        self.assertIn("rows->set_childForceExpandWidth(false)", menu)
        self.assertIn("scroll->set_sizeDelta({-6.0F, -22.0F})", menu)
        self.assertIn("ConfigureRightPanelButton(active_->startRecordingButton_)", menu)

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
