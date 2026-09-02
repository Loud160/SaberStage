// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Coordinates PC avatar observation sessions and controller shortcuts.
// - It treats the PC avatar solver as a black box and records only observable input/output poses.

using System;
using System.IO;
using UnityEngine;
using UnityEngine.XR;
using IPALogger = IPA.Logging.Logger;

namespace SaberStage.PcPoseAnalyzer
{
    // A high execution order makes LateUpdate a best-effort observation point after
    // the avatar's IK work. The analyzer never calls or patches the IK solver.
    [DefaultExecutionOrder(32000)]
    internal sealed class AnalyzerHost : MonoBehaviour
    {
        private const float ControllerLongPressSeconds = 1.25f;

        private IPALogger _logger;
        private AnalyzerConfig _config;
        private PoseSessionWriter _writer;
        private AvatarBinding _avatar;
        private VrControlOverlay _overlay;

        private float _nextManagerScanTime;
        private float _nextContinuousSampleTime;
        private float _nextCalibrationSampleTime;
        private bool _continuousCapture;
        private bool _calibrationCapture;
        private float _lastObservedAvatarScale = float.NaN;

        private SnapshotState _snapshotState;
        private float _snapshotStateEndsAt;
        private int _snapshotSequence;
        private string _activeCaptureId;

        private bool _leftPrimaryPrevious;
        private bool _rightPrimaryPrevious;
        private bool _leftSecondaryPrevious;
        private bool _rightSecondaryPrevious;
        private float _leftSecondaryPressedAt;
        private float _rightSecondaryPressedAt;
        private bool _leftSecondaryLongPressHandled;
        private bool _rightSecondaryLongPressHandled;

        internal void Initialize(IPALogger logger)
        {
            _logger = logger;

            string userData = Path.Combine(Environment.CurrentDirectory, "UserData");
            string configPath = Path.Combine(userData, "SaberStagePoseAnalyzer.json");
            _config = AnalyzerConfig.LoadOrCreate(configPath);

            if (!_config.enabled)
            {
                _logger.Info($"PC Pose Analyzer is disabled in {configPath}.");
                enabled = false;
                return;
            }

            string sessionRoot = Path.Combine(userData, "SaberStagePoseAnalyzer");
            _writer = new PoseSessionWriter(sessionRoot, _logger);
            _writer.WriteSessionHeader(_config);

            _overlay = gameObject.AddComponent<VrControlOverlay>();
            _overlay.Initialize(_config.showVrControllerOverlay, _logger);

            _logger.Info($"PC Pose Analyzer session: {_writer.SessionDirectory}");
            _logger.Info("VR controls: A/X pose-window; tap Y calibration; hold Y continuous capture; tap B screenshot; hold B help overlay. Keyboard F8-F11 remains available as a fallback.");
        }

        private void Update()
        {
            if (_config == null || !_config.enabled)
            {
                return;
            }

            if (Time.unscaledTime >= _nextManagerScanTime)
            {
                _nextManagerScanTime = Time.unscaledTime + 1.0f;
                RefreshAvatarBinding();
            }

            bool snapshotPressed = Input.GetKeyDown(KeyCode.F9);
            bool continuousPressed = Input.GetKeyDown(KeyCode.F10);
            bool screenshotPressed = Input.GetKeyDown(KeyCode.F11);

            if (Input.GetKeyDown(KeyCode.F8))
            {
                ToggleCalibrationCapture("keyboard-f8");
            }

            if (_config.controllerButtonsEnabled)
            {
                ReadControllerButtons(
                    out bool leftPrimary,
                    out bool rightPrimary,
                    out bool leftSecondary,
                    out bool rightSecondary);

                snapshotPressed |= (leftPrimary && !_leftPrimaryPrevious) || (rightPrimary && !_rightPrimaryPrevious);

                // Quest Touch labels are X/Y on the left controller and A/B on
                // the right controller. Keep either primary button available for
                // pose capture so the tester can use the hand that is not being
                // measured. Tap/hold separation provides all other actions using
                // only face buttons; it does not depend on joystick-click support.
                ProcessLeftSecondaryButton(leftSecondary, ref continuousPressed);
                ProcessRightSecondaryButton(rightSecondary, ref screenshotPressed);

                _leftPrimaryPrevious = leftPrimary;
                _rightPrimaryPrevious = rightPrimary;
                _leftSecondaryPrevious = leftSecondary;
                _rightSecondaryPrevious = rightSecondary;
            }

            if (snapshotPressed)
            {
                BeginSnapshotCapture();
                SendControllerHaptic(0.25f, 0.08f);
            }

            if (continuousPressed)
            {
                _continuousCapture = !_continuousCapture;
                _writer?.WriteMarker(_continuousCapture ? "continuous-start" : "continuous-stop", null, _config.snapshotLabel, flush: true);
                _logger.Info($"Continuous pose capture {(_continuousCapture ? "started" : "stopped")}.");
                SendControllerHaptic(_continuousCapture ? 0.55f : 0.20f, _continuousCapture ? 0.16f : 0.08f);
            }

            if (screenshotPressed)
            {
                CaptureScreenshot("manual");
                SendControllerHaptic(0.35f, 0.10f);
            }

            AdvanceSnapshotState();
            _overlay?.SetCaptureState(
                _avatar != null && _avatar.IsValid,
                _avatar?.AvatarName,
                _calibrationCapture,
                _continuousCapture,
                _snapshotState.ToString(),
                _config.snapshotLabel);

            if (_avatar != null && _avatar.IsValid)
            {
                float scale = _avatar.RuntimeScale;
                if (float.IsNaN(_lastObservedAvatarScale) || Mathf.Abs(scale - _lastObservedAvatarScale) > 0.0001f)
                {
                    _lastObservedAvatarScale = scale;
                    WriteCalibrationObservation("runtime-scale-changed", flush: true);
                }
            }
        }

        private void LateUpdate()
        {
            if (_writer == null || _avatar == null || !_avatar.IsValid)
            {
                return;
            }

            bool takeContinuousSample = _continuousCapture && Time.unscaledTime >= _nextContinuousSampleTime;
            bool takeSnapshotSample = _snapshotState == SnapshotState.Sampling;
            bool takeCalibrationSample = _calibrationCapture && Time.unscaledTime >= _nextCalibrationSampleTime;

            if (!takeContinuousSample && !takeSnapshotSample && !takeCalibrationSample)
            {
                return;
            }

            if (takeContinuousSample)
            {
                _nextContinuousSampleTime = Time.unscaledTime + (1.0f / _config.continuousSampleRateHz);
            }

            if (takeContinuousSample || takeSnapshotSample)
            {
                PoseSample sample = PoseSampler.TryCreateSample(
                    _avatar,
                    takeSnapshotSample ? _activeCaptureId : null,
                    takeSnapshotSample ? "snapshot-window" : "continuous");

                if (sample != null)
                {
                    _writer.WriteSample(sample);
                }
            }

            if (takeCalibrationSample)
            {
                _nextCalibrationSampleTime = Time.unscaledTime + (1.0f / _config.calibrationSampleRateHz);
                WriteCalibrationObservation("calibration-timeline", flush: false);
            }
        }

        private void BeginSnapshotCapture()
        {
            if (_writer == null)
            {
                return;
            }

            _snapshotSequence++;
            _activeCaptureId = $"capture-{_snapshotSequence:D4}";
            _snapshotState = SnapshotState.Countdown;
            _snapshotStateEndsAt = Time.unscaledTime + _config.snapshotCountdownSeconds;

            _writer.WriteMarker("snapshot-countdown", _activeCaptureId, _config.snapshotLabel);
            _logger.Info($"{_activeCaptureId}: hold '{_config.snapshotLabel}' after the {_config.snapshotCountdownSeconds:0.0}s countdown.");

            if (_config.snapshotCountdownSeconds <= 0.0f)
            {
                BeginSnapshotWindow();
            }
        }

        private void AdvanceSnapshotState()
        {
            if (_snapshotState == SnapshotState.Countdown && Time.unscaledTime >= _snapshotStateEndsAt)
            {
                BeginSnapshotWindow();
            }
            else if (_snapshotState == SnapshotState.Sampling && Time.unscaledTime >= _snapshotStateEndsAt)
            {
                string captureId = _activeCaptureId;
                _snapshotState = SnapshotState.Idle;
                _activeCaptureId = null;
                _writer.WriteMarker("snapshot-complete", captureId, _config.snapshotLabel, flush: true);
                _logger.Info($"{captureId}: pose window captured.");
                SendControllerHaptic(0.40f, 0.10f);

                if (_config.captureScreenshot)
                {
                    CaptureScreenshot(captureId);
                }
            }
        }

        private void BeginSnapshotWindow()
        {
            _snapshotState = SnapshotState.Sampling;
            _snapshotStateEndsAt = Time.unscaledTime + _config.snapshotWindowSeconds;
            _writer.WriteMarker("snapshot-window-start", _activeCaptureId, _config.snapshotLabel, flush: true);
            // A stronger pulse marks the exact point at which the tester must hold
            // the requested pose still. This avoids needing to see the desktop.
            SendControllerHaptic(0.70f, 0.12f);
        }

        private void ToggleCalibrationCapture(string source)
        {
            _calibrationCapture = !_calibrationCapture;
            string marker = _calibrationCapture ? "calibration-timeline-start" : "calibration-timeline-stop";
            string detail = $"{source}; avatar={_avatar?.AvatarName ?? "not-bound"}";
            _writer?.WriteMarker(marker, null, detail, flush: true);
            _logger.Info($"Calibration timeline capture {(_calibrationCapture ? "started" : "stopped")}.");
            SendControllerHaptic(_calibrationCapture ? 0.60f : 0.20f, _calibrationCapture ? 0.18f : 0.08f);
        }

        private void ProcessLeftSecondaryButton(bool pressed, ref bool continuousPressed)
        {
            if (pressed && !_leftSecondaryPrevious)
            {
                _leftSecondaryPressedAt = Time.unscaledTime;
                _leftSecondaryLongPressHandled = false;
            }

            if (pressed && !_leftSecondaryLongPressHandled &&
                Time.unscaledTime - _leftSecondaryPressedAt >= ControllerLongPressSeconds)
            {
                _leftSecondaryLongPressHandled = true;
                continuousPressed = true;
            }

            if (!pressed && _leftSecondaryPrevious && !_leftSecondaryLongPressHandled)
            {
                ToggleCalibrationCapture("controller-y-tap");
            }
        }

        private void ProcessRightSecondaryButton(bool pressed, ref bool screenshotPressed)
        {
            if (pressed && !_rightSecondaryPrevious)
            {
                _rightSecondaryPressedAt = Time.unscaledTime;
                _rightSecondaryLongPressHandled = false;
            }

            if (pressed && !_rightSecondaryLongPressHandled &&
                Time.unscaledTime - _rightSecondaryPressedAt >= ControllerLongPressSeconds)
            {
                _rightSecondaryLongPressHandled = true;
                bool nowVisible = _overlay?.ToggleVisible() ?? false;
                _writer?.WriteMarker("vr-help-overlay", null, nowVisible ? "shown" : "hidden", flush: true);
                SendControllerHaptic(nowVisible ? 0.45f : 0.20f, 0.08f);
            }

            if (!pressed && _rightSecondaryPrevious && !_rightSecondaryLongPressHandled)
            {
                screenshotPressed = true;
            }
        }

        private void RefreshAvatarBinding()
        {
            AvatarBinding candidate = AvatarBinding.TryFindCurrent(_logger);

            if (candidate == null)
            {
                if (_avatar != null)
                {
                    _writer?.WriteMarker("avatar-unbound", null, _avatar.AvatarName, flush: true);
                    _logger.Info("PC Pose Analyzer is waiting for a Custom Avatars avatar.");
                }

                _avatar = null;
                return;
            }

            if (_avatar != null && _avatar.InstanceId == candidate.InstanceId)
            {
                return;
            }

            _avatar = candidate;
            _lastObservedAvatarScale = float.NaN;
            _writer?.WriteMarker("avatar-bound", null, _avatar.AvatarName, flush: true);
            WriteCalibrationObservation("avatar-bound", flush: true);
            _logger.Info($"PC Pose Analyzer bound to '{_avatar.AvatarName}'.");
        }

        private void WriteCalibrationObservation(string reason, bool flush)
        {
            CalibrationRecord record = _avatar?.TryCreateCalibrationRecord(reason);
            if (record != null)
            {
                _writer?.WriteCalibration(record, flush);
            }
        }

        private void CaptureScreenshot(string captureId)
        {
            if (_writer == null)
            {
                return;
            }

            string safeId = string.IsNullOrWhiteSpace(captureId) ? "capture" : captureId;
            string fileName = $"{DateTime.UtcNow:yyyyMMdd-HHmmss-fff}-{safeId}.png";
            string path = Path.Combine(_writer.SessionDirectory, fileName);
            ScreenCapture.CaptureScreenshot(path);
            _writer.WriteMarker("screenshot-requested", captureId, fileName, flush: true);
        }

        private static void ReadControllerButtons(
            out bool leftPrimary,
            out bool rightPrimary,
            out bool leftSecondary,
            out bool rightSecondary)
        {
            InputDevice left = InputDevices.GetDeviceAtXRNode(XRNode.LeftHand);
            InputDevice right = InputDevices.GetDeviceAtXRNode(XRNode.RightHand);

            left.TryGetFeatureValue(CommonUsages.primaryButton, out leftPrimary);
            left.TryGetFeatureValue(CommonUsages.secondaryButton, out leftSecondary);
            right.TryGetFeatureValue(CommonUsages.primaryButton, out rightPrimary);
            right.TryGetFeatureValue(CommonUsages.secondaryButton, out rightSecondary);
        }

        private static void SendControllerHaptic(float amplitude, float duration)
        {
            // Haptics are best-effort because not every PC XR backend exposes an
            // impulse channel. Capture never depends on feedback being available.
            InputDevices.GetDeviceAtXRNode(XRNode.LeftHand).SendHapticImpulse(0u, amplitude, duration);
            InputDevices.GetDeviceAtXRNode(XRNode.RightHand).SendHapticImpulse(0u, amplitude, duration);
        }

        private void OnDestroy()
        {
            _writer?.Dispose();
            _writer = null;
        }

        private enum SnapshotState
        {
            Idle,
            Countdown,
            Sampling,
        }
    }
}
