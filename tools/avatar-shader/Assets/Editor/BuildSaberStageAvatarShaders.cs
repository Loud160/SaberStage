// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Builds the Android avatar shader AssetBundle from the Unity editor project.
// - An explicit shader list prevents Unity stripping Quest multiview variants SaberStage requires.

using System;
using System.IO;
using System.Linq;
using UnityEditor;
using UnityEditor.XR.Management;
using UnityEditor.XR.Management.Metadata;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.XR.Management;
using Unity.XR.Oculus;

internal static class BuildSaberStageAvatarShaders
{
    private const string OutputVariable = "SABERSTAGE_AVATAR_SHADER_OUTPUT";

    private static void RequireAndroidMultiview()
    {
        XRGeneralSettingsPerBuildTarget targets;
        EditorBuildSettings.TryGetConfigObject(XRGeneralSettings.k_SettingsKey, out targets);
        if (targets == null) {
            targets = ScriptableObject.CreateInstance<XRGeneralSettingsPerBuildTarget>();
            if (!AssetDatabase.IsValidFolder("Assets/XR")) AssetDatabase.CreateFolder("Assets", "XR");
            AssetDatabase.CreateAsset(targets, "Assets/XR/XRGeneralSettings.asset");
            EditorBuildSettings.AddConfigObject(XRGeneralSettings.k_SettingsKey, targets, true);
        }
        var settings = targets.SettingsForBuildTarget(BuildTargetGroup.Android);
        if (settings == null) {
            settings = ScriptableObject.CreateInstance<XRGeneralSettings>();
            targets.SetSettingsForBuildTarget(BuildTargetGroup.Android, settings);
            AssetDatabase.AddObjectToAsset(settings, targets);
        }
        if (settings.Manager == null) {
            settings.Manager = ScriptableObject.CreateInstance<XRManagerSettings>();
            AssetDatabase.AddObjectToAsset(settings.Manager, targets);
        }
        if (!(settings.Manager.activeLoaders != null && settings.Manager.activeLoaders.Any(loader => loader is OculusLoader)) &&
            !XRPackageMetadataStore.AssignLoader(settings.Manager, typeof(OculusLoader).FullName, BuildTargetGroup.Android))
            throw new InvalidOperationException("Oculus XR loader could not be enabled; refusing to strip the Quest stereo variants.");

        OculusSettings oculus;
        EditorBuildSettings.TryGetConfigObject("Unity.XR.Oculus.Settings", out oculus);
        if (oculus == null) {
            oculus = ScriptableObject.CreateInstance<OculusSettings>();
            AssetDatabase.CreateAsset(oculus, "Assets/XR/OculusSettings.asset");
            EditorBuildSettings.AddConfigObject("Unity.XR.Oculus.Settings", oculus, true);
        }
        oculus.m_StereoRenderingModeAndroid = OculusSettings.StereoRenderingModeAndroid.Multiview;
        EditorUtility.SetDirty(oculus);
        AssetDatabase.SaveAssets();
    }

    public static void BuildAndroid()
    {
        var output = Environment.GetEnvironmentVariable(OutputVariable);
        if (string.IsNullOrWhiteSpace(output)) throw new InvalidOperationException(OutputVariable + " is not set.");
        RequireAndroidMultiview();
        PlayerSettings.SetUseDefaultGraphicsAPIs(BuildTarget.Android, false);
        PlayerSettings.SetGraphicsAPIs(BuildTarget.Android, new[] { GraphicsDeviceType.Vulkan, GraphicsDeviceType.OpenGLES3 });
        Directory.CreateDirectory(output);
        var build = new AssetBundleBuild {
            assetBundleName = "saberstage_avatar_shaders",
            assetNames = new[] {
                "Assets/SaberStageMToon.shader",
                "Assets/SaberStageMToonOutline.shader",
                "Assets/SaberStageVideoPreview.shader",
                "Assets/SaberStageGripTarget.shader",
                "Assets/SaberStageNonBloomUI.shader"
            },
            addressableNames = new[] {
                "saberstage-mtoon",
                "saberstage-mtoon-outline",
                "saberstage-video-preview",
                "saberstage-grip-target",
                "saberstage-non-bloom-ui"
            }
        };
        var manifest = BuildPipeline.BuildAssetBundles(output, new[] { build },
            BuildAssetBundleOptions.ChunkBasedCompression |
            BuildAssetBundleOptions.DeterministicAssetBundle |
            BuildAssetBundleOptions.ForceRebuildAssetBundle |
            BuildAssetBundleOptions.StrictMode, BuildTarget.Android);
        if (manifest == null || !File.Exists(Path.Combine(output, "saberstage_avatar_shaders")))
            throw new InvalidOperationException("Unity did not create the SaberStage Android avatar shader bundle.");
    }
}
