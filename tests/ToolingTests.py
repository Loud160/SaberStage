#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
#
# Part of SaberStage.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

# File responsibility:
# - Validates repository scripts, manifests, licensing, and packaging contracts.
# - Tests prevent build-path drift that ordinary C++ unit tests cannot observe.

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
    def test_private_logger_is_pinned_isolated_and_collected(self):
        lock = json.loads((ROOT / "dependencies/native-logger.json").read_text(encoding="utf-8"))
        qpm = json.loads((ROOT / "qpm.json").read_text(encoding="utf-8"))
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        logging_header = (ROOT / "include/saberstage/Logging.hpp").read_text(encoding="utf-8")
        logging_source = (ROOT / "src/Logging.cpp").read_text(encoding="utf-8")
        build = (ROOT / "scripts/build.ps1").read_text(encoding="utf-8")
        collector = (ROOT / "scripts/quest_tool.py").read_text(encoding="utf-8")

        self.assertEqual(lock["schemaVersion"], 1)
        self.assertEqual(lock["version"], "1.0.0")
        self.assertRegex(lock["revision"], r"^[0-9a-f]{40}$")
        self.assertRegex(lock["archiveSha256"], r"^[0-9a-f]{64}$")
        self.assertEqual(
            lock["archiveUrl"],
            f"https://github.com/Loud160/NativeLoggerQuest/archive/{lock['revision']}.zip",
        )
        self.assertEqual(lock["sourceDirectory"], f"NativeLoggerQuest-{lock['revision']}")
        self.assertNotIn("paper2_scotland2", {item["id"] for item in qpm["dependencies"]})
        self.assertIn("NativeLoggerQuest::NativeLoggerQuest", cmake)
        self.assertIn("native_logger_quest_enable_paper2_abort_bridge", cmake)
        self.assertNotIn("paper2_scotland2", logging_header)
        self.assertNotIn("paper2_scotland2", logging_source)
        self.assertIn("saberstage-native.log", logging_source)
        self.assertIn("saberstage-native.previous.log", logging_source)
        self.assertIn("options.emitToLogcat = true", logging_source)
        self.assertIn("prepare-native-logger.py", build)
        self.assertIn("verify-native-library.py", build)
        self.assertIn('"saberstage-native.log"', collector)
        self.assertIn('"saberstage-native.previous.log"', collector)

    def test_error_ui_is_main_thread_owned_and_frontmost(self):
        manager = (ROOT / "src/ErrorManager.cpp").read_text(encoding="utf-8")
        manager_header = (ROOT / "include/saberstage/ErrorManager.hpp").read_text(encoding="utf-8")
        driver = (ROOT / "src/ErrorRuntimeDriver.cpp").read_text(encoding="utf-8")
        main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")

        self.assertIn("std::scoped_lock lock(mutex_)", manager)
        self.assertIn("pendingDialog_", manager)
        self.assertIn("SetAsLastSibling", manager)
        self.assertIn("get_isInTransition()", manager)
        self.assertIn("SafePtrUnity<HMUI::FlowCoordinator>", manager)
        self.assertIn("ErrorManager::Instance().TickMainThread()", driver)
        self.assertIn("DontDestroyOnLoad", driver)
        self.assertIn("NotifyMainFlowActivated", manager_header)
        self.assertIn("uiDiscoveryReady_", manager_header)
        self.assertIn("MainFlowCoordinator_DidActivate", main)
        self.assertLess(
            manager.index("if (!shouldResolveTarget) return;"),
            manager.index("auto target = ResolveTarget();"),
        )
        self.assertLess(
            main.index("MainFlowCoordinator_DidActivate(\n        self"),
            main.index("NotifyMainFlowActivated();"),
        )
        self.assertLess(
            main.index("INSTALL_HOOK(saberstage::Logging::Logger, MainFlowCoordinator_DidActivate)"),
            main.index("g_application = std::make_unique"),
        )
        self.assertLess(
            main.index("Logging::Logger.Initialize(VERSION)"),
            main.index("il2cpp_functions::Init()"),
        )
        self.assertIn("PauseMenuManager::Start SaberStage UI creation", main)
        self.assertIn("PauseMenuManager::OnDestroy SaberStage cleanup", main)

    def test_continuous_ui_edits_use_coalesced_settings_persistence(self):
        header = (ROOT / "include/saberstage/settings/SettingsService.hpp").read_text(encoding="utf-8")
        service = (ROOT / "src/settings/SettingsService.cpp").read_text(encoding="utf-8")
        menu = (ROOT / "src/ui/MenuController.cpp").read_text(encoding="utf-8")
        application = (ROOT / "src/app/ApplicationRoot.cpp").read_text(encoding="utf-8")

        self.assertIn("void RequestSave(", header)
        self.assertIn("bool TickPendingSave", header)
        self.assertIn("bool FlushPendingSave", header)
        self.assertIn("std::chrono::milliseconds(300)", header)
        self.assertIn("pendingSaveDue_ = std::chrono::steady_clock::now()", service)
        self.assertIn("std::chrono::seconds(1)", service)
        self.assertIn("root_.Settings().RequestSave();", menu)
        self.assertIn("root_.Settings().TickPendingSave", menu)
        self.assertIn("settings_.FlushPendingSave", application)

    def test_twitch_uses_registered_public_app_and_rotating_token_refresh(self):
        settings = (ROOT / "include/saberstage/settings/SettingsModel.hpp").read_text(encoding="utf-8")
        service = (ROOT / "src/broadcast/TwitchService.cpp").read_text(encoding="utf-8")
        menu = (ROOT / "src/ui/MenuController.cpp").read_text(encoding="utf-8")

        self.assertIn('"p6shnc5g4vtb46a7xd26d1uh6xr6ee"', settings)
        self.assertIn("TwitchTokenNeedsRefresh", settings)
        self.assertIn('"&grant_type=refresh_token&refresh_token="', service)
        self.assertIn("UrlEncode(refreshToken)", service)
        self.assertIn("pendingCredentials_.refreshed", service)
        self.assertIn("ClearSensitiveString(account.refreshToken)", service)
        self.assertIn('av_dict_set(&options, "tls_verify", "1", 0);', service)
        self.assertIn('av_dict_set(&options, "ca_file", "/system/etc/security/cacerts/", 0);', service)
        self.assertIn('"Twitch IRC TLS transport opened; authenticating chat account"', service)
        self.assertIn("avio_read_partial", service)
        self.assertIn("Twitch chat authentication timed out after 15 seconds", service)
        self.assertIn("Twitch IRC authentication commands sent", service)
        self.assertIn("Twitch stream title updated successfully", service)
        self.assertIn('line.find(" 001 ")', service)
        self.assertIn("BinaryOptionHex(body)", service)
        self.assertIn('av_dict_set(&options, "post_data", binaryPostData.c_str(), 0);', service)
        self.assertNotIn('av_dict_set(&options, "post_data", std::string(body).c_str(), 0);', service)
        self.assertIn('if (method == "PATCH")', service)
        self.assertIn(
            "HTTP 4xx/5xx responses make that call\n"
            "    // fail. Once a PATCH reaches this point",
            service,
        )
        self.assertIn("twitchAuthorizationAwaitingCompletion_", menu)
        self.assertIn("if (twitchAuthorizationModal_) twitchAuthorizationModal_->Hide();", menu)
        self.assertIn('"Twitch account connected.\\n\\nSigned in as " + twitch.login', menu)
        self.assertIn('recordingView_, {52.0F, 22.0F}', menu)
        self.assertNotIn("client_secret", service.lower())
        self.assertNotIn("Twitch App Client ID", menu)
        self.assertIn("no Client ID or secret entry is required", menu)
        self.assertIn("pendingLiveTwitchTitleUpdate_", menu)
        self.assertIn("update the active stream without ending it", menu)

    def test_twitch_oauth_tokens_use_android_keystore_and_never_serialize_plaintext(self):
        model = (ROOT / "include/saberstage/settings/SettingsModel.hpp").read_text(encoding="utf-8")
        settings_service = (ROOT / "src/settings/SettingsService.cpp").read_text(encoding="utf-8")
        twitch_service = (ROOT / "src/broadcast/TwitchService.cpp").read_text(encoding="utf-8")
        keystore = (ROOT / "src/security/AndroidKeystore.cpp").read_text(encoding="utf-8")

        self.assertIn("protectedTokenEnvelope", model)
        self.assertIn('"AndroidKeyStore"', keystore)
        self.assertIn('"AES/GCM/NoPadding"', keystore)
        self.assertIn('"android/security/keystore/KeyGenParameterSpec$Builder"', keystore)
        self.assertIn('"setRandomizedEncryptionRequired"', keystore)
        self.assertIn("ProtectRuntimeTokens", twitch_service)
        self.assertIn("RestoreSavedTokens", twitch_service)

        encoder = settings_service[settings_service.index("std::string Encode("):]
        self.assertIn('"protectedTokenEnvelope"', encoder)
        self.assertNotIn('"accessToken",', encoder)
        self.assertNotIn('"refreshToken",', encoder)

    def test_twitch_title_metadata_cannot_block_stream_start(self):
        menu = (ROOT / "src/ui/MenuController.cpp").read_text(encoding="utf-8")
        service = (ROOT / "src/broadcast/TwitchService.cpp").read_text(encoding="utf-8")
        start = menu.index("void MenuController::TryStartLivestreamWithTitle()")
        end = menu.index("void MenuController::RefreshRecordingStatus()", start)
        implementation = menu[start:end]

        self.assertLess(
            implementation.index("StartLivestream(&error)"),
            implementation.index("BeginTitleUpdate(destination.streamTitle"),
        )
        self.assertIn("The stream started, but Twitch could not apply its saved title", implementation)
        self.assertNotIn("pendingTwitchStreamStart_", implementation)
        self.assertIn('if (method == "PATCH")', service)
        self.assertLess(
            service.index('if (method == "PATCH")'),
            service.index("while (response.size() < kMaximumHttpResponseBytes)"),
        )
        self.assertNotIn('"http_code"', service)
        self.assertIn('"STREAM IS LIVE\\n\\nThe video and audio broadcast is still running.', menu)
        self.assertIn("ShowLivestreamActionError(twitch.titleUpdateStatus, true)", menu)

    def test_twitch_chat_panel_resizes_scrolls_and_reports_live_viewers(self):
        settings = (ROOT / "include/saberstage/settings/SettingsModel.hpp").read_text(encoding="utf-8")
        service = (ROOT / "src/broadcast/TwitchService.cpp").read_text(encoding="utf-8")
        menu = (ROOT / "src/ui/MenuController.cpp").read_text(encoding="utf-8")

        self.assertIn("float width = 70.0F", settings)
        self.assertIn("float height = 58.0F", settings)
        self.assertIn('"Resize Panel"', menu)
        self.assertIn('"Lock Size"', menu)
        self.assertIn('"Chat Control"', menu)
        self.assertIn('"Resize Panel", "PlayButton"', menu)
        self.assertIn('"Chat Control", "PlayButton"', menu)
        self.assertIn("colors.set_disabledColor(colors.get_normalColor())", menu)
        self.assertIn("CreateScrollView(parent)", menu)
        self.assertIn(
            "GetComponentsInChildren<UnityEngine::UI::Graphic*>(true)", menu
        )
        self.assertIn(
            "graphic->GetComponentInParent<\n                    UnityEngine::UI::Button*>()",
            menu,
        )
        self.assertIn("if (!IsAlive(button)) graphic->set_raycastTarget(false)", menu)
        self.assertIn("chatWorldPanelScrollView_->____isHoveredByPointer", menu)
        self.assertNotIn("UpdateChatWorldPanelScrollIndicator", menu)
        self.assertNotIn("chatWorldPanelScrollTrack_", menu)
        self.assertNotIn("chatWorldPanelScrollHandle_", menu)
        self.assertIn("pointer->get_pointingOver()", menu)
        self.assertIn(
            "pointerOverPanel && !bodyGrabbed && chatWorldPanelContentOverflows_",
            menu,
        )
        self.assertNotIn("kChatJoystickDeadZone", menu)
        self.assertNotIn("____verticalScrollIndicator->get_gameObject()->SetActive(false)", menu)
        self.assertIn(
            "row->set_horizontalAlignment(TMPro::HorizontalAlignmentOptions::Left)",
            menu,
        )
        self.assertIn(
            "row->set_verticalAlignment(TMPro::VerticalAlignmentOptions::Top)",
            menu,
        )
        self.assertIn("chatWorldPanelContentOverflows_", menu)
        self.assertIn("chatWorldPanelScrollView_->ScrollTo(0.0F, false)", menu)
        self.assertIn("ScrollToEnd(false)", menu)
        self.assertIn("chatWorldPanelFollowLive_", menu)
        self.assertIn('"SaberStage Twitch Chat Resize Handle"', menu)
        self.assertIn('"SaberStage Twitch Chat Visible Resize Grip "', menu)
        self.assertIn("chatWorldPanelResizeGripStrokes_", menu)
        self.assertIn("set_active(chatWorldPanelResizeEditing_)", menu)
        self.assertIn("chatWorldPanelResizeHandleScreen_->handle->set_layer(5)", menu)
        self.assertIn("constexpr std::size_t kChatVirtualRowPoolSize = 32", menu)
        self.assertIn("chatWorldPanelRows_.reserve(kChatVirtualRowPoolSize)", menu)
        self.assertIn("row->set_color({0.92F, 0.95F, 1.0F, 1.0F})", menu)
        self.assertIn("row->set_enableWordWrapping(true)", menu)
        self.assertIn("layout->set_enabled(false)", menu)
        self.assertIn("fitter->set_enabled(false)", menu)
        self.assertIn("void MenuController::ReflowChatWorldPanelText()", menu)
        self.assertIn("GetPreferredValues(", menu)
        self.assertIn("void MenuController::RefreshVirtualizedChatRows()", menu)
        self.assertIn("rect->set_anchorMin({0.5F, 1.0F})", menu)
        self.assertIn("rect->set_anchorMax({0.5F, 1.0F})", menu)
        self.assertIn("rect->set_sizeDelta({textWidth, entry.height})", menu)
        self.assertIn("chatWorldPanelRowsDirty_", menu)
        self.assertIn("chatWorldPanelRenderedScrollPosition_", menu)
        self.assertIn("kChatDataRefreshIntervalSeconds = 0.10F", menu)
        self.assertIn("ReflowChatWorldPanelText();", menu)
        self.assertIn("settings.width = kChatPanelSize.x", menu)
        self.assertIn("settings.height = kChatPanelSize.y", menu)
        self.assertIn("helix/streams?user_id=", service)
        self.assertIn("viewer_count", service)
        self.assertIn("kMaximumChatMessages = 128", service)
        self.assertIn("readWait >= std::chrono::milliseconds(750)", service)
        self.assertIn("context->error = 0", service)
        self.assertIn("context->eof_reached = 0", service)
        self.assertIn("Twitch IRC idle TLS timeout handled as an empty chat interval", service)

    def test_chat_scrollbar_diagnostics_observe_without_mutating_ui(self):
        source = (ROOT / "src/ui/ChatPanelDiagnostics.cpp").read_text(encoding="utf-8")
        menu = (ROOT / "src/ui/MenuController.cpp").read_text(encoding="utf-8")
        self.assertIn('"saberstage/Logging.hpp"', source)
        self.assertIn("_verticalScrollIndicator", source)
        self.assertIn('LogLayout("outer-content", outer)', source)
        self.assertIn('LogLayout("inner-content", inner)', source)
        self.assertIn("content-height-overwritten", source)
        self.assertIn("get_absoluteDepth()", source)
        self.assertIn("get_hasRectClipping()", source)
        self.assertIn("GetInheritedAlpha()", source)
        self.assertIn("get_cullingMask()", source)
        self.assertNotIn("->set_", source)
        self.assertNotIn("->SetContentSize(", source)
        self.assertNotIn("->get_materialForRendering(", source)
        self.assertNotIn("ForceRebuildLayoutImmediate", source)
        self.assertNotIn("Logger.Flush", source)
        self.assertNotIn("get_text()", source)
        self.assertNotIn("std::thread", source)
        self.assertNotIn(".ptr()", source.replace("UnityW::ptr()", "checked accessor"))
        self.assertIn("object ? object.unsafePtr() : nullptr", source)
        self.assertIn("operationSite={}:{}", source)
        self.assertIn("std::chrono::seconds(5)", source)
        self.assertIn('SetOperation("read VR pointer hit GameObject")', menu)
        self.assertIn("ReportUpdateFailure(exception.what())", menu)
        # Observation must precede resizing/reflow, or our own write would hide
        # the very native-layout overwrite these diagnostics are measuring.
        tick = menu[menu.index("void MenuController::TickChatWorldPanel() noexcept"):]
        self.assertLess(tick.index("chatWorldPanelDiagnostics_.Tick("),
                        tick.index("TickChatWorldPanelResize();"))
        self.assertIn("contentHeight, chatWorldPanelScrollView_->get_contentSize()", menu)
        destroy = menu[menu.index("void MenuController::DestroyChatWorldPanel() noexcept"):]
        self.assertLess(destroy.index("chatWorldPanelDiagnostics_.Reset();"),
                        destroy.index("UnityEngine::Object::Destroy(screenObject)"))

    def test_chat_virtual_content_has_one_layout_owner_and_visible_native_controls(self):
        menu = (ROOT / "src/ui/MenuController.cpp").read_text(encoding="utf-8")
        chat = menu[menu.index("void MenuController::EnsureChatWorldPanel()"):
                    menu.index("void MenuController::EnsureStandinProxy(")]
        self.assertIn("disableContentLayout(scrollContent)", chat)
        self.assertIn("disableContentLayout(outerContent->get_gameObject())", chat)
        self.assertIn("GetComponent<BSML::ScrollViewContent*>()", chat)
        self.assertIn("driver->set_enabled(false)", chat)
        self.assertIn("fitter->set_enabled(false)", chat)
        self.assertIn("layout->set_enabled(false)", chat)
        self.assertIn("outerContent->set_sizeDelta({textWidth, contentHeight})", chat)
        self.assertIn("innerContent->set_sizeDelta({textWidth, contentHeight})", chat)
        self.assertIn("innerContent->set_anchoredPosition({0.0F, 0.0F})", chat)
        self.assertIn("CalculateChatPanelScrollGeometry(viewportWidth, pageHeight, offset)", chat)
        self.assertIn("chatWorldPanelScrollGeometry_.pageHeight", chat)
        self.assertIn("chatWorldPanelScrollView_->get_scrollPageSize()", chat)
        self.assertNotIn("chat.height - kChatPanelHeaderHeight - 3.0F", chat)
        controls = chat[chat.index("void MenuController::RefreshChatWorldPanelScrollControls()"):
                        chat.index("void MenuController::ReflowChatWorldPanelText()")]
        self.assertIn("_verticalScrollIndicator", controls)
        self.assertIn("_pageUpButton", controls)
        self.assertIn("_pageDownButton", controls)
        self.assertIn("!object->get_activeSelf()) object->set_active(true)", controls)
        self.assertIn("target->set_raycastTarget(true)", controls)
        self.assertIn("UpdateVerticalScrollIndicator(", controls)
        self.assertNotIn("CreateImage", controls)
        self.assertNotIn("AddComponent", controls)
        self.assertNotIn("set_interactable(true)", controls)
        self.assertIn("setRect(chatWorldPanelBackground_, {0.0F, 0.0F}", chat)
        self.assertNotIn("set_localPosition({})", chat)

    def test_chat_missing_pointer_does_not_abort_message_processing(self):
        menu = (ROOT / "src/ui/MenuController.cpp").read_text(encoding="utf-8")
        tick = menu[menu.index("void MenuController::TickChatWorldPanel() noexcept"):
                    menu.index("void MenuController::EnsureStandinProxy(")]
        input_path = tick[tick.index('SetOperation("read current UI event system")'):
                          tick.index('SetOperation("assign native chat scroll hover state")')]
        self.assertIn("if (eventSystem)", input_path)
        self.assertIn("if (inputModule && inputModule->_vrPointer)", input_path)
        self.assertIn("if (pointedObject)", input_path)
        self.assertNotIn(".ptr()", input_path)
        self.assertNotIn("return;", input_path)
        self.assertLess(tick.index("pointerOverPanel && !bodyGrabbed"),
                        tick.index("root_.Twitch().Snapshot()"))

    def test_support_settings_redacts_every_service_key(self):
        source = {
            "broadcast": {
                "destinations": {
                    "twitch": {"serverUrl": "rtmps://twitch.example/app", "streamKey": "twitch-secret"},
                    "youtube": {"serverUrl": "rtmps://youtube.example/live", "streamKey": "youtube-secret"},
                    "kick": {"serverUrl": "rtmps://kick.example/app", "streamKey": ""},
                    "custom": {"serverUrl": "rtmp://custom.example/live", "stream_key": "custom-secret"},
                },
                "twitchAccount": {
                    "clientId": "public-client-id",
                    "accessToken": "oauth-access-secret",
                    "refresh_token": "oauth-refresh-secret",
                    "protectedTokenEnvelope": "ak1:encrypted-secret",
                    "login": "safe-login",
                },
            }
        }
        redacted = QUEST_TOOL.redact_settings_credentials(json.dumps(source))
        self.assertNotIn("twitch-secret", redacted)
        self.assertNotIn("youtube-secret", redacted)
        self.assertNotIn("custom-secret", redacted)
        self.assertNotIn("oauth-access-secret", redacted)
        self.assertNotIn("oauth-refresh-secret", redacted)
        self.assertNotIn("ak1:encrypted-secret", redacted)
        result = json.loads(redacted)
        self.assertEqual(result["broadcast"]["destinations"]["twitch"]["streamKey"], "<redacted>")
        self.assertEqual(result["broadcast"]["destinations"]["kick"]["streamKey"], "")
        self.assertEqual(result["broadcast"]["destinations"]["custom"]["stream_key"], "<redacted>")
        self.assertEqual(result["broadcast"]["twitchAccount"]["accessToken"], "<redacted>")
        self.assertEqual(result["broadcast"]["twitchAccount"]["refresh_token"], "<redacted>")
        self.assertEqual(result["broadcast"]["twitchAccount"]["protectedTokenEnvelope"], "<redacted>")
        self.assertEqual(result["broadcast"]["twitchAccount"]["clientId"], "public-client-id")
        self.assertEqual(result["broadcast"]["twitchAccount"]["login"], "safe-login")

    def test_support_settings_omits_unparseable_content_instead_of_leaking_it(self):
        secret = "private-value-that-must-not-survive"
        redacted = QUEST_TOOL.redact_settings_credentials('{"streamKey":"' + secret)
        self.assertNotIn(secret, redacted)
        self.assertIn("omitted", redacted)

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
        self.assertTrue({"beatsaber-hook", "scotland2", "bsml", "custom-types", "hollywood"} <= dependencies)
        self.assertNotIn("paper2_scotland2", dependencies)

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

    def test_direct_capture_reports_each_pipeline_stage_and_falls_back_before_saving_empty_video(self):
        direct = (ROOT / "src/recording/DirectFfmpegCapture.cpp").read_text(encoding="utf-8")
        controller = (ROOT / "src/recording/RecordingController.cpp").read_text(encoding="utf-8")
        header = (ROOT / "include/saberstage/recording/DirectFfmpegCapture.hpp").read_text(encoding="utf-8")

        self.assertIn("DirectCaptureDiagnostics", header)
        self.assertIn("EGL_RECORDABLE_ANDROID", direct)
        self.assertIn("NoCurrentContext", direct)
        self.assertIn("NoEncoderOutput", direct)
        self.assertIn("surfaceFramesPresented", direct)
        self.assertIn("encodedPackets", direct)
        self.assertIn("Direct FFmpeg capture failed at", direct)
        self.assertIn("result == AVERROR(EAGAIN)", direct)
        self.assertIn("continue;", direct)
        self.assertIn("HandleDirectCaptureHealth", controller)
        self.assertIn("StopVideoSegment(false)", controller)
        self.assertIn("activeBackend_ = settings::RecordingBackend::Hollywood", controller)
        self.assertIn("Direct encoder was unavailable; recording is continuing with Hollywood.", controller)
        self.assertIn("Capture stream finalization check", controller)

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

    def test_default_afk_image_is_embedded_and_decoded_by_unity(self):
        image = ROOT / "assets/saberstage_afk_image.png"
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        source = (ROOT / "src/recording/AfkMediaSource.cpp").read_text(encoding="utf-8")
        menu = (ROOT / "src/ui/MenuController.cpp").read_text(encoding="utf-8")

        self.assertTrue(image.is_file())
        self.assertGreater(image.stat().st_size, 100_000)
        self.assertIn("saberstage_afk_image.png", cmake)
        self.assertIn("saberstage_afk_image.o", cmake)
        self.assertIn("_binary_saberstage_afk_image_png_start", source)
        self.assertIn("_binary_saberstage_afk_image_png_end", source)
        self.assertIn("ImageConversion::LoadImage(texture_, ManagedArray(bytes), false)", source)
        self.assertIn("Built-in SaberStage AFK image", source)
        self.assertIn("Pause screen: built-in SaberStage AFK image", menu)

    def test_movable_recording_panel_is_compact_persistent_and_capture_excluded(self):
        menu = (ROOT / "src/ui/MenuController.cpp").read_text(encoding="utf-8")
        settings = (ROOT / "src/settings/SettingsService.cpp").read_text(encoding="utf-8")
        self.assertIn('"Floating Recording Controls"', menu)
        # The floating panel selects one output mode and exposes explicit text
        # buttons; world-space hover hints previously rendered as blank boxes,
        # so explanations remain in the ordinary menu rather than this panel.
        self.assertIn('"STOP"', menu)
        self.assertIn('"PAUSE"', menu)
        self.assertIn('"START"', menu)
        self.assertIn('"Stream Control"', menu)
        self.assertIn('"Record"', menu)
        self.assertIn('"Stream"', menu)
        self.assertNotIn("AddHoverHint(\n            recordingWorldPanel", menu)
        self.assertIn("RecordingOutputTypeName(snapshot.outputType)", menu)
        self.assertIn("RecordingElapsed(elapsed)", menu)
        self.assertIn('"Current Frame Loss: 0   Total Frames Lost: 0"', menu)
        self.assertIn('"REC --.- FPS   HMD AVG --.- FPS"', menu)
        self.assertIn("recordingWorldPanelHmdSampledFrames_", menu)
        self.assertIn("recordingWorldPanelHmdTotalFrameSeconds_", menu)
        # A thin full-panel movement collider sits behind the UI. Buttons and
        # toggles remain the nearer hit targets while uncovered black backdrop
        # regions can move the panel. The panel remains height-variant.
        self.assertIn("HideAndFitWorldPanelHandleBehindContent", menu)
        self.assertIn("RecordingPanelSize(", menu)
        self.assertIn("kRecordingPanelButtonBandHeight", menu)
        self.assertIn('"Panel FPS Counters"', menu)
        self.assertIn("encodedFrameCount", menu)
        self.assertIn("RegisterCaptureExcludedRoot(screenObject)", menu)
        self.assertNotIn("SaberStage Recording Grab Bar", menu)
        self.assertNotIn("HideAndPlaceWorldPanelHandleInPadding", menu)
        self.assertIn('"worldControlsVisible"', settings)
        self.assertIn('"worldControlsShowFps"', settings)
        self.assertIn('"worldControlsStreamMode"', settings)
        self.assertIn('"worldControlsPosition"', settings)
        self.assertIn('"worldControlsRotationDegrees"', settings)
        self.assertIn('"SaberStage Recording Panel Input Shield"', menu)
        self.assertIn('"SaberStage Recording Panel Blue Border"', menu)
        self.assertIn('"SaberStage Recording Panel Bottom Border"', menu)
        self.assertIn("recordingBorderThickness", menu)
        self.assertIn("inputShield->set_raycastTarget(false)", menu)
        self.assertIn("border->set_raycastTarget(false)", menu)
        self.assertIn("{2.5F, modeY}", menu)
        self.assertNotIn("recordingWorldPanelModeBackground_", menu)
        self.assertIn('streamMode ? "Stream Control" : "Recording Control"', menu)
        self.assertIn('"SaberStage Movable Livestream Microphone Mute"', menu)
        self.assertIn('"SaberStage Movable Livestream Game Sound Mute"', menu)
        self.assertIn("EmbeddedRecordingPanelIcons()", menu)
        self.assertIn("controlIcons.microphoneUnavailable", menu)
        self.assertIn("controlIcons.microphoneMuted", menu)
        self.assertIn("controlIcons.microphoneActive", menu)
        self.assertIn("controlIcons.gameAudioMuted", menu)
        self.assertIn("controlIcons.gameAudioActive", menu)
        self.assertNotIn('"Microphone Capsule"', menu)
        self.assertNotIn('"Microphone Unavailable X"', menu)
        self.assertIn("RecordingWorldPanelMicrophoneAction", menu)
        self.assertIn("RecordingWorldPanelGameAudioAction", menu)
        self.assertGreaterEqual(menu.count("->get_gameObject()->SetActive(true)"), 2)
        self.assertIn("recordingWorldPanelGameAudioButton_->set_interactable(true)", menu)
        self.assertIn("recordingWorldPanelMicrophoneButton_->set_interactable(true)", menu)

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

    def test_spectator_camera_excludes_both_preview_monitors(self):
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
        # Both camera monitors are excluded from the spectator frame. The
        # movable popout was once deliberately left camera-visible as a
        # recursion diagnostic; that produced an infinite picture-in-picture
        # feed in recordings and must never come back.
        self.assertIn("cullPreview(floatingImage_)", preview)
        self.assertIn("cacheRoot(floatingScreen_->get_gameObject().ptr())", preview)
        self.assertNotIn("UnityEngine::GameObject* floatingCaptureRoot_", preview)
        # The popout owns its own opaque material instance; sharing one
        # material between the HMUI docked canvas and the world-space
        # FloatingScreen canvas lets UI pipeline state leak between monitors.
        self.assertIn("floatingMaterial_", preview)
        self.assertIn("Canvas::ForceUpdateCanvases()", preview)
        self.assertIn("renderer->set_cull(false)", preview)
        self.assertIn("CreateWorldSpaceVideoSurface", preview)
        self.assertIn("PublicRawImageTag{}.Create(parent)", preview)
        self.assertIn("image->set_material(material)", preview)
        self.assertIn("floatingImage_->set_texture(texture)", preview)
        self.assertIn("floatingImage_->SetMaterialDirty()", preview)
        self.assertIn("PersistFloatingPoseNow();", preview)
        self.assertIn("Creation restores the persisted pose", preview)
        self.assertNotIn("MovePreviewPoseInFrontOfPlayer(preview);", preview)
        # The camera monitor must participate in Canvas UI ordering. A Geometry
        # queue pass renders before the panel's backdrop and is then hidden by
        # that backdrop even though the texture and multiview shader are valid.
        preview_shader = (ROOT / "tools/avatar-shader/Assets/SaberStageVideoPreview.shader").read_text(
            encoding="utf-8"
        )
        self.assertIn('"Queue"="Transparent"', preview_shader)
        self.assertIn("ZWrite Off", preview_shader)
        self.assertIn("Blend SrcAlpha OneMinusSrcAlpha", preview_shader)
        self.assertNotIn('"Queue"="Geometry"', preview_shader)
        non_bloom_shader = (ROOT / "tools/avatar-shader/Assets/SaberStageNonBloomUI.shader").read_text(
            encoding="utf-8"
        )
        self.assertIn('Shader "SaberStage/NonBloomUI"', non_bloom_shader)
        self.assertIn("Blend SrcAlpha OneMinusSrcAlpha, Zero Zero", non_bloom_shader)
        self.assertIn("STEREO_MULTIVIEW_ON", non_bloom_shader)
        self.assertIn("saberstage-non-bloom-ui", (
            ROOT / "tools/avatar-shader/Assets/Editor/BuildSaberStageAvatarShaders.cs"
        ).read_text(encoding="utf-8"))
        self.assertIn("EmbeddedNonBloomUiShader", preview)
        self.assertNotIn("YawDegrees(FromUnity(headRotation)) + 180.0F", preview)
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

    def test_avatar_setup_is_one_click_calibration_gated_and_task_grouped(self):
        menu = (ROOT / "src/ui/MenuController.cpp").read_text(encoding="utf-8")
        application = (ROOT / "src/app/ApplicationRoot.cpp").read_text(encoding="utf-8")
        self.assertIn(
            'std::array<std::string_view, 4> tabNames{"Setup", "Display", "Quality", "Fit"}',
            menu,
        )
        for control in (
            '"Enable Avatar"',
            '"Basic Calibration"',
            '"Advanced Calibration"',
            '"Reset Profile Calibration"',
            '"Choose Avatar File"',
            '"Load Avatar"',
            '"Unload Avatar"',
            '"Bind Tracking"',
            '"Reset Avatar Pose"',
            '"Clear Avatar Data"',
        ):
            self.assertIn(control, menu)
        self.assertIn("LoadSelectedAvatar(false, &error)", menu)
        self.assertIn("LoadVrmAvatar(\n            path,", menu)
        self.assertIn("root_.Avatar().ApplyAvatarSettings(avatarSettings)", menu)
        self.assertIn("root_.Avatar().RecalibrateNeutral()", menu)
        self.assertIn("Calibration staging only; avatar hidden", menu)
        self.assertIn("if (!avatar_->PlayerProfile().valid)", application)
        self.assertIn("CreateCenterThreeColumnRow(container->get_transform())", menu)
        self.assertIn('setupQuickActions,\n        "",', menu)
        self.assertIn('CreateCenterPanelSubheader(container->get_transform(), "Avatar Controls")', menu)
        self.assertIn("constexpr float kCenterThreeColumnRowWidth = 116.0F;", menu)
        self.assertIn("constexpr float kCenterThreeColumnWidth = 38.0F;", menu)
        self.assertNotIn("Choose a player profile and avatar once.", menu)
        self.assertNotIn('CreateCenterPanelSubheader(container->get_transform(), "Tracking Recovery")', menu)
        self.assertNotIn("To change avatars: 1) Choose Avatar File", menu)
        self.assertNotIn('CreateUIButton(loadActions, "Attach Tracking"', menu)

    def test_recording_side_panel_overrides_center_panel_prefab_widths(self):
        menu = (ROOT / "src/ui/MenuController.cpp").read_text(encoding="utf-8")
        self.assertIn("layout->set_preferredWidth(kRightPanelRowWidth)", menu)
        self.assertIn(
            "active_->livestreamKeyInput_, 512, kRightPanelRowWidth - 10.0F",
            menu,
        )
        self.assertIn('CreateUIButton(\n        streamKeyInputRow, "Set"', menu)
        self.assertIn('CreateUIButton(\n        serverInputRow, "Set"', menu)
        self.assertIn('CreateUIButton(actions, "Use Once"', menu)
        self.assertIn('CreateUIButton(actions, "Save in Settings"', menu)
        self.assertIn('CreateRightPanelSubheader(livestreamPage->get_transform(), "Server Address")', menu)
        self.assertIn('CreateRightPanelSubheader(livestreamPage->get_transform(), "Stream Key")', menu)
        self.assertIn('"Show Stream Key"', menu)
        self.assertIn("display.assign(display.size(), '*')", menu)
        self.assertIn("ShowLivestreamActionError(error)", menu)
        self.assertIn(
            "broadcast::CanStart(livestream.state) && livestream.streamKeyConfigured",
            menu,
        )
        self.assertNotIn(
            "broadcast::CanStart(livestream.state) && livestream.streamKeyConfigured &&",
            menu,
        )
        self.assertIn("rows->set_childForceExpandWidth(false)", menu)
        self.assertIn("scroll->set_sizeDelta({0.0F, -13.0F})", menu)
        self.assertIn("BSML::SliderSetting* ConstrainRightPanelRow", menu)
        self.assertIn("kRightPanelLabelFraction", menu)
        self.assertIn("constexpr float kRightPanelRowWidth = 54.0F", menu)
        # Preserve the page's native centered placement. Live Stream now sizes
        # its other rows from Service's actual outer row on tab activation;
        # changing the page to UpperLeft would move that fixed reference too.
        self.assertIn("rows->set_childAlignment(UnityEngine::TextAnchor::UpperCenter)", menu)
        self.assertNotIn("LeftAlignLivestreamSettingRows", menu)
        self.assertIn("FitRectToParentRegion(", menu)
        self.assertIn('livestreamTransport, "Start Stream", "PlayButton"', menu)
        self.assertIn('livestreamTransport, "Stop Stream", "PlayButton"', menu)
        self.assertIn("ConfigureRightPanelButton(active_->startRecordingButton_)", menu)

    def test_livestream_layout_uses_untouched_service_outer_row(self):
        menu = (ROOT / "src/ui/MenuController.cpp").read_text(encoding="utf-8")
        reference_start = menu.index("    auto* provider = WithHint")
        reference_end = menu.index(
            "    active_->livestreamProviderFeatureText_ = BSML::Lite::CreateText",
            reference_start,
        )
        # The user explicitly froze this reference widget. Catch accidental
        # changes to its creation, constraints or event setup in later fixes.
        self.assertEqual(
            hashlib.sha256(menu[reference_start:reference_end].encode()).hexdigest(),
            "f3e165787f6c4a348777db640b7af82caf881216b6ebce44d22e0cbd6e02947b",
        )
        apply = menu.split("void MenuController::ApplyLivestreamReferenceLayout()", 1)[1]
        apply = apply.split("void MenuController::ShowRecordingTab", 1)[0]
        self.assertIn("reference->get_parent() != content", apply)
        self.assertIn('reference->Find("Label")', apply)
        self.assertIn("referenceBounds.get_width()", apply)
        self.assertIn("label->get_margin().x", apply)
        self.assertIn("selectorBounds.get_xMax()", apply)
        self.assertEqual(apply.count("if (child == reference) continue;"), 2)
        self.assertLess(
            apply.index("SetLivestreamRowWidth(child->get_gameObject(), rowWidth)"),
            apply.index("FitLivestreamActionRow(group, rowWidth, leftInset, rightInset)"),
        )
        self.assertNotIn("kRightPanelRowWidth", apply)
        self.assertNotIn("referenceRect->set_", apply)
        self.assertNotIn("labelRect->set_", apply)
        self.assertNotIn("selectorRect->set_", apply)
        self.assertIn("margin.x = leftInset", apply)
        self.assertIn("margin.z = rightInset", apply)
        self.assertIn("slider->slider->UpdateVisuals()", apply)
        horizontal = menu.split("void FitLivestreamHorizontalSpan(", 1)[1]
        horizontal = horizontal.split("void FitLivestreamToggle", 1)[0]
        self.assertIn("anchorMin.x = 0.0F", horizontal)
        self.assertIn("offsetMax.x = -rightInset", horizontal)
        self.assertNotIn(".y =", horizontal)
        # This is a one-time tab-entry layout, not work repeated by every UI
        # tick, chat message, audio slider callback or stream-status refresh.
        self.assertEqual(menu.count("ApplyLivestreamReferenceLayout();"), 1)
        show = menu.split("void MenuController::ShowRecordingTab(int index)", 1)[1]
        self.assertIn("if (selectedRecordingTab_ == 1) ApplyLivestreamReferenceLayout();", show)
        self.assertLess(show.index("SetActive("), show.index("ApplyLivestreamReferenceLayout();"))

    def test_livestream_action_groups_fit_inside_service_content_edges(self):
        menu = (ROOT / "src/ui/MenuController.cpp").read_text(encoding="utf-8")
        fit = menu.split("void FitLivestreamActionRow(", 1)[1]
        fit = fit.split("void ConfigureCalibrationPanelText", 1)[0]
        self.assertIn("std::lround(leftInset)", fit)
        self.assertIn("std::lround(rightInset)", fit)
        self.assertIn("rowWidth - leftPadding - rightPadding", fit)
        self.assertIn("SetLivestreamRowWidth(object, width)", fit)
        self.assertIn("FitLivestreamToggle(toggle, 0.0F, 0.0F)", fit)
        # Single-action rows also need a Service-width outer group; widening
        # the button itself to that group width would cover the native insets.
        for name, caption in (
            ("titleActions", "Set Stream Title"),
            ("chooseAfkActions", "Choose AFK Picture or GIF"),
            ("builtInAfkActions", "Use Built-in AFK Screen"),
            ("clearKeyActions", "Clear Stream Key"),
        ):
            self.assertIn(
                f"auto* {name} = CreateRightPanelInputActionRow(livestreamPage->get_transform());",
                menu,
            )
            self.assertIn(f'{name}, "{caption}"', menu)

    def test_livestream_uses_ffmpeg_native_annex_b_to_flv_conversion(self):
        sink = (ROOT / "src/broadcast/DirectLivestreamSink.cpp").read_text(encoding="utf-8")
        self.assertIn("BuildAnnexBExtradata", sink)
        self.assertIn('av_dict_set(&headerOptions, "flvflags", "no_duration_filesize", 0)', sink)
        self.assertIn("av_packet_rescale_ts(output, encoderTimeBase, videoStream_->time_base)", sink)
        self.assertIn("Livestream FLV header accepted: video time base", sink)
        self.assertIn("Livestream first video packet written", sink)
        self.assertIn("audioCodec_->initial_padding", sink)
        self.assertIn("Livestream first audio packet written", sink)
        self.assertNotIn("BuildAvcc", sink)
        self.assertNotIn("AnnexBAccessUnitToAvcc", sink)

    def test_go_live_does_not_create_an_implicit_local_recording(self):
        controller = (ROOT / "src/recording/RecordingController.cpp").read_text(encoding="utf-8")
        audio = (ROOT / "src/recording/RealtimeAudioCapture.cpp").read_text(encoding="utf-8")
        menu = (ROOT / "src/ui/MenuController.cpp").read_text(encoding="utf-8")
        self.assertIn("StartCapture(error, true, true, false)", controller)
        self.assertIn("if (writeLocalOutput) {", controller)
        self.assertIn("audioCapture_->OpenConsumerOnly", controller)
        self.assertIn("if (videoWriter_ && !videoWriter_->TrySubmit", controller)
        self.assertIn("Stream-only capture stopped without creating local media files", controller)
        self.assertIn("writeWaveFile_", audio)
        self.assertIn("Going live does not start or save a local recording", menu)
        self.assertNotIn("starts a local safety recording", menu)

    def test_livestream_wake_guard_is_scoped_and_restores_previous_timeout(self):
        header = (ROOT / "include/saberstage/recording/RecordingController.hpp").read_text(encoding="utf-8")
        controller = (ROOT / "src/recording/RecordingController.cpp").read_text(encoding="utf-8")
        menu = (ROOT / "src/ui/MenuController.cpp").read_text(encoding="utf-8")

        self.assertIn("previousSleepTimeout_", header)
        self.assertIn("EnableLivestreamWakeGuard();", controller)
        self.assertIn("DisableLivestreamWakeGuard();", controller)
        self.assertIn('"get_sleepTimeout"', controller)
        self.assertIn('"set_sleepTimeout"', controller)
        self.assertIn("SleepTimeout.NeverSleep", controller)
        self.assertIn("restoreValue", controller)
        self.assertIn('"Keep Headset Awake"', menu)
        self.assertIn("broadcast.keepHeadsetAwake", menu)
        self.assertIn("Hollywood::SetScreenOn(true)", controller)
        self.assertIn("Hollywood::SetScreenOn(false)", controller)
        self.assertIn("livestreamProximityGuardActive_", header)

    def test_livestream_audio_mix_is_stream_only_bounded_and_permission_aware(self):
        header = (ROOT / "include/saberstage/recording/RecordingController.hpp").read_text(encoding="utf-8")
        microphone = (ROOT / "src/recording/MicrophoneCapture.cpp").read_text(encoding="utf-8")
        audio_capture = (ROOT / "src/recording/RealtimeAudioCapture.cpp").read_text(encoding="utf-8")
        controller = (ROOT / "src/recording/RecordingController.cpp").read_text(encoding="utf-8")
        menu = (ROOT / "src/ui/MenuController.cpp").read_text(encoding="utf-8")
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")

        self.assertIn("-ldl", cmake)
        self.assertIn('dlopen("libaaudio.so"', microphone)
        self.assertIn('"AAudioStreamBuilder_setDirection"', microphone)
        self.assertIn('"AAudioStreamBuilder_setDataCallback"', microphone)
        self.assertIn("kMicrophoneRingFrames", microphone)
        self.assertNotIn("std::mutex", microphone)
        self.assertIn("livestreamMixScratch_", header)
        self.assertIn("SubmitLivestreamAudioLocked", controller)
        self.assertIn("std::clamp(\n                gameSample + microphoneSample", controller)
        self.assertIn("android.permission.RECORD_AUDIO", controller)
        self.assertIn('"Game Sound"', menu)
        self.assertIn('"Game Sound Volume"', menu)
        self.assertIn('"Quest Microphone"', menu)
        self.assertIn('"Microphone Volume"', menu)
        self.assertIn("without Microphone Access", menu)
        self.assertIn("repatch Beat Saber", menu)
        self.assertIn("The microphone is never added to local recordings", menu)
        self.assertIn("SetLivestreamGameAudioVolumePercent(value)", menu)
        self.assertIn("SetLivestreamMicrophoneVolumePercent(value)", menu)
        self.assertNotIn(
            "livestreamSettingsEditable && audioMix.gameAudioEnabled", menu
        )
        self.assertNotIn(
            "livestreamSettingsEditable && audioMix.microphoneEnabled", menu
        )
        self.assertIn("livestreamMicrophoneMuted_ = true", controller)
        self.assertIn("livestreamMicrophoneMuted_ = false", controller)
        self.assertIn("SetLivestreamMicrophoneMuted", controller)
        self.assertIn("snapshot.microphoneAvailable", controller)
        self.assertIn("snapshot.microphoneMuted", controller)
        self.assertIn("SetLivestreamGameAudioMuted", controller)
        self.assertIn("SetLocalRecordingGameAudioMuted", controller)
        self.assertIn("localRecordingGameAudioMuted_", header)
        self.assertIn("SetFileMuted", controller)
        self.assertIn("SetFileMuted", audio_capture)
        self.assertIn("fileMuted_", audio_capture)
        self.assertIn("muteFileBatch", audio_capture)
        self.assertIn("snapshot.gameAudioAvailable", controller)
        self.assertIn("snapshot.gameAudioMuted", controller)
        self.assertIn("!livestreamGameAudioMuted_", controller)
        for icon in (
            "saberstage_mic_active.png",
            "saberstage_mic_muted.png",
            "saberstage_mic_unavailable.png",
            "saberstage_game_audio_active.png",
            "saberstage_game_audio_muted.png",
        ):
            self.assertTrue((ROOT / "assets" / icon).is_file())
            self.assertIn(icon.removesuffix(".png"), cmake)

    def test_twitch_is_the_only_supported_service_preset_for_now(self):
        menu = (ROOT / "src/ui/MenuController.cpp").read_text(encoding="utf-8")
        self.assertIn('"YouTube (Not Supported)"', menu)
        self.assertIn('"Kick (Not Supported)"', menu)
        self.assertIn(
            "provider == settings::LivestreamProvider::YouTube ||",
            menu,
        )
        self.assertIn(
            "provider == settings::LivestreamProvider::Kick",
            menu,
        )
        self.assertIn("cannot be used yet. Twitch is the first fully supported service", menu)

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

    def test_project_license_and_first_party_headers_are_complete(self):
        license_text = (ROOT / "LICENSE").read_text(encoding="utf-8")
        additional_terms = (ROOT / "LICENSE-ADDITIONAL-TERMS.md").read_text(
            encoding="utf-8")
        self.assertIn("GNU GENERAL PUBLIC LICENSE", license_text)
        self.assertIn("Version 3, 29 June 2007", license_text)
        self.assertIn("SaberStage GPLv3 section 7 terms", additional_terms)
        self.assertIn("https://github.com/Loud160/SaberStage", additional_terms)
        self.assertTrue((ROOT / "INBOUND_LICENSE.md").is_file())
        self.assertTrue((ROOT / "DCO.txt").is_file())

        # Match Big Screen's license boundary: require the full project header
        # on comment-capable first-party source and build files, while leaving
        # JSON, Markdown, generated Unity metadata, and third-party material in
        # their native formats and under their own notices.
        first_party_patterns = {
            ROOT / "include": ("*.c", "*.cpp", "*.h", "*.hpp"),
            ROOT / "src": ("*.c", "*.cpp", "*.h", "*.hpp"),
            ROOT / "scripts": ("*.ps1", "*.sh", "*.py"),
            ROOT / "tests": ("*.c", "*.cpp", "*.h", "*.hpp", "*.py"),
            # Unity writes third-party packages under tools/avatar-shader/Library
            # during a shader build. Restrict tooling checks to the two authored
            # source trees so a local build cache can never become license input.
            ROOT / "tools" / "avatar-shader" / "Assets": ("*.cs", "*.shader"),
            ROOT / "tools" / "pc-pose-analyzer": ("*.cs", "*.ps1"),
        }
        generated_directory_names = {
            "Library", "Temp", "Logs", "obj", "bin", "build", "build-host",
        }
        first_party_files = {
            ROOT / "CMakeLists.txt",
            ROOT / "Build-And-Deploy.bat",
            ROOT / "Build-And-Deploy-Linux.sh",
            ROOT / "Collect-SaberStage-Logs.bat",
            ROOT / "Collect-SaberStage-Logs-Linux.sh",
            ROOT / "Remove-SaberStage.bat",
            ROOT / "Remove-SaberStage-Linux.sh",
        }
        for directory, patterns in first_party_patterns.items():
            for pattern in patterns:
                first_party_files.update(
                    path for path in directory.rglob(pattern)
                    if not generated_directory_names.intersection(
                        path.relative_to(ROOT).parts)
                )

        self.assertTrue(first_party_files)
        for source_file in sorted(first_party_files):
            preamble = "\n".join(
                source_file.read_text(encoding="utf-8").splitlines()[:12])
            self.assertEqual(
                preamble.count("SPDX-License-Identifier: GPL-3.0-only"),
                1,
                source_file,
            )
            self.assertIn("LICENSE-ADDITIONAL-TERMS.md", preamble, source_file)


if __name__ == "__main__":
    unittest.main(verbosity=2)
