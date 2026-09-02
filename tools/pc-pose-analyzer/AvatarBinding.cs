// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Discovers and binds the active PC avatar humanoid skeleton.
// - Binding is refreshed when avatar instances change so samplers do not retain destroyed transforms.

using System;
using System.Linq;
using System.Reflection;
using UnityEngine;
using IPALogger = IPA.Logging.Logger;

namespace SaberStage.PcPoseAnalyzer
{
    internal sealed class AvatarBinding
    {
        private readonly Component _spawnedAvatar;
        private readonly Animator _animator;
        private readonly object _input;
        private readonly MethodInfo _tryGetTransform;
        private readonly MethodInfo _tryGetFingerCurl;
        private readonly object _headUse;
        private readonly object _leftHandUse;
        private readonly object _rightHandUse;
        private readonly object[] _headArguments;
        private readonly object[] _leftArguments;
        private readonly object[] _rightArguments;
        private readonly object[] _leftCurlArguments;
        private readonly object[] _rightCurlArguments;

        private AvatarBinding(Component spawnedAvatar, Animator animator, object input, MethodInfo tryGetTransform, Type deviceUseType, string avatarName)
        {
            _spawnedAvatar = spawnedAvatar;
            _animator = animator;
            _input = input;
            _tryGetTransform = tryGetTransform;
            _tryGetFingerCurl = input.GetType().GetMethod("TryGetFingerCurl", BindingFlags.Instance | BindingFlags.Public);
            _headUse = Enum.Parse(deviceUseType, "Head");
            _leftHandUse = Enum.Parse(deviceUseType, "LeftHand");
            _rightHandUse = Enum.Parse(deviceUseType, "RightHand");
            _headArguments = new[] { _headUse, null };
            _leftArguments = new[] { _leftHandUse, null };
            _rightArguments = new[] { _rightHandUse, null };
            _leftCurlArguments = new[] { _leftHandUse, null };
            _rightCurlArguments = new[] { _rightHandUse, null };
            AvatarName = avatarName;
        }

        public int InstanceId => _spawnedAvatar != null ? _spawnedAvatar.GetInstanceID() : 0;
        public string AvatarName { get; }
        public bool IsValid => _spawnedAvatar != null && _animator != null && _input != null;
        public Animator Animator => _animator;
        public float RuntimeScale => ReadFloatProperty(_spawnedAvatar, "scale");

        public static AvatarBinding TryFindCurrent(IPALogger logger)
        {
            try
            {
                Assembly customAvatarAssembly = AppDomain.CurrentDomain.GetAssemblies()
                    .FirstOrDefault(assembly => string.Equals(assembly.GetName().Name, "CustomAvatar", StringComparison.OrdinalIgnoreCase));

                Type managerType = customAvatarAssembly?.GetType("CustomAvatar.Player.PlayerAvatarManager", throwOnError: false);
                if (managerType == null)
                {
                    return null;
                }

                UnityEngine.Object manager = UnityEngine.Object.FindObjectOfType(managerType);
                if (manager == null)
                {
                    return null;
                }

                object spawnedObject = managerType.GetProperty("currentlySpawnedAvatar", BindingFlags.Instance | BindingFlags.Public)?.GetValue(manager);
                if (!(spawnedObject is Component spawned))
                {
                    return null;
                }

                Animator animator = spawned.GetComponentInChildren<Animator>();
                object input = spawned.GetType().GetProperty("input", BindingFlags.Instance | BindingFlags.Public)?.GetValue(spawned);
                MethodInfo tryGetTransform = input?.GetType().GetMethod("TryGetTransform", BindingFlags.Instance | BindingFlags.Public);

                if (animator == null || tryGetTransform == null)
                {
                    logger.Warn("The current Custom Avatars avatar does not expose the expected Animator/input surface; pose capture is paused.");
                    return null;
                }

                ParameterInfo[] parameters = tryGetTransform.GetParameters();
                if (parameters.Length != 2 || !parameters[0].ParameterType.IsEnum || !parameters[1].IsOut)
                {
                    logger.Warn("Custom Avatars TryGetTransform has an unknown signature; pose capture is paused.");
                    return null;
                }

                string avatarName = TryReadAvatarName(spawned) ?? spawned.name;
                return new AvatarBinding(spawned, animator, input, tryGetTransform, parameters[0].ParameterType, avatarName);
            }
            catch (Exception ex)
            {
                logger.Warn($"Could not bind the Custom Avatars observable runtime surface: {ex.GetType().Name}: {ex.Message}");
                return null;
            }
        }

        public bool TryGetTargets(out Transform head, out Transform leftHand, out Transform rightHand)
        {
            head = InvokeTryGetTransform(_headArguments);
            leftHand = InvokeTryGetTransform(_leftArguments);
            rightHand = InvokeTryGetTransform(_rightArguments);
            return head != null && leftHand != null && rightHand != null;
        }

        public CalibrationRecord TryCreateCalibrationRecord(string reason)
        {
            if (!TryGetTargets(out Transform headTarget, out Transform leftTarget, out Transform rightTarget))
            {
                return null;
            }

            object prefab = _spawnedAvatar.GetType().GetProperty("prefab", BindingFlags.Instance | BindingFlags.Public)?.GetValue(_spawnedAvatar);
            float authoredEyeHeight = ReadFloatProperty(prefab, "eyeHeight");
            float authoredArmSpan = ReadFloatProperty(prefab, "armSpan");
            float runtimeScale = ReadFloatProperty(_spawnedAvatar, "scale");
            float absoluteScale = ReadFloatProperty(_spawnedAvatar, "absoluteScale");
            float scaledEyeHeight = ReadFloatProperty(_spawnedAvatar, "scaledEyeHeight");

            Transform headBone = _animator.GetBoneTransform(HumanBodyBones.Head);
            Transform leftWrist = _animator.GetBoneTransform(HumanBodyBones.LeftHand);
            Transform rightWrist = _animator.GetBoneTransform(HumanBodyBones.RightHand);

            Vector3 avatarUp = _spawnedAvatar.transform.up;
            float targetEyeAboveRoot = Vector3.Dot(headTarget.position - _spawnedAvatar.transform.position, avatarUp);
            float outputEyeAboveRoot = headBone != null ? Vector3.Dot(headBone.position - _spawnedAvatar.transform.position, avatarUp) : 0.0f;
            float controllerSpan = Vector3.Distance(leftTarget.position, rightTarget.position);
            float outputWristSpan = leftWrist != null && rightWrist != null ? Vector3.Distance(leftWrist.position, rightWrist.position) : 0.0f;

            return new CalibrationRecord
            {
                createdUtc = DateTime.UtcNow.ToString("O"),
                frame = Time.frameCount,
                unscaledTime = Time.unscaledTime,
                reason = reason,
                avatarName = AvatarName,
                authoredEyeHeightMeters = authoredEyeHeight,
                authoredArmSpanMeters = authoredArmSpan,
                runtimeScale = runtimeScale,
                absoluteScale = absoluteScale,
                reportedScaledEyeHeightMeters = scaledEyeHeight,
                observedHeadTargetAboveAvatarRootMeters = targetEyeAboveRoot,
                observedOutputHeadAboveAvatarRootMeters = outputEyeAboveRoot,
                observedControllerSpanMeters = controllerSpan,
                observedOutputWristSpanMeters = outputWristSpan,
                heightScaleCandidate = authoredEyeHeight > 0.0001f ? targetEyeAboveRoot / authoredEyeHeight : 0.0f,
                armSpanScaleCandidate = authoredArmSpan > 0.0001f ? controllerSpan / authoredArmSpan : 0.0f,
                avatarRootWorld = new SerializableVector3(_spawnedAvatar.transform.position),
                headTargetWorld = new SerializableVector3(headTarget.position),
                leftTargetWorld = new SerializableVector3(leftTarget.position),
                rightTargetWorld = new SerializableVector3(rightTarget.position),
            };
        }

        public FingerInputSetData ReadFingerInputs()
        {
            return new FingerInputSetData
            {
                left = InvokeTryGetFingerCurl(_leftCurlArguments),
                right = InvokeTryGetFingerCurl(_rightCurlArguments),
            };
        }

        private Transform InvokeTryGetTransform(object[] arguments)
        {
            arguments[1] = null;
            bool available = (bool)_tryGetTransform.Invoke(_input, arguments);
            return available ? arguments[1] as Transform : null;
        }

        private FingerCurlData InvokeTryGetFingerCurl(object[] arguments)
        {
            if (_tryGetFingerCurl == null)
            {
                return new FingerCurlData { available = false, appliedFallbackCurl = 1.0f };
            }

            arguments[1] = null;
            bool available = (bool)_tryGetFingerCurl.Invoke(_input, arguments);
            if (!available || arguments[1] == null)
            {
                // Custom Avatars visibly applies its fully-closed fallback when no
                // per-finger source is available. Record the boundary result rather
                // than pretending a tracked curl was supplied.
                return new FingerCurlData { available = false, appliedFallbackCurl = 1.0f };
            }

            object curl = arguments[1];
            return new FingerCurlData
            {
                available = true,
                thumb = ReadFloatProperty(curl, "thumb"),
                index = ReadFloatProperty(curl, "index"),
                middle = ReadFloatProperty(curl, "middle"),
                ring = ReadFloatProperty(curl, "ring"),
                little = ReadFloatProperty(curl, "little"),
                appliedFallbackCurl = 0.0f,
            };
        }

        private static string TryReadAvatarName(Component spawned)
        {
            object prefab = spawned.GetType().GetProperty("prefab", BindingFlags.Instance | BindingFlags.Public)?.GetValue(spawned);
            object descriptor = prefab?.GetType().GetProperty("descriptor", BindingFlags.Instance | BindingFlags.Public)?.GetValue(prefab);
            return descriptor?.GetType().GetProperty("name", BindingFlags.Instance | BindingFlags.Public)?.GetValue(descriptor) as string;
        }

        private static float ReadFloatProperty(object instance, string name)
        {
            if (instance == null)
            {
                return 0.0f;
            }

            object value = instance.GetType().GetProperty(name, BindingFlags.Instance | BindingFlags.Public)?.GetValue(instance);
            return value is float number ? number : 0.0f;
        }
    }
}
