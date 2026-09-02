// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Defines persisted settings for the PC pose-analysis companion plugin.
// - Configuration keeps capture controls and output paths independent from analyzer logic.

using System;
using System.IO;
using UnityEngine;

namespace SaberStage.PcPoseAnalyzer
{
    [Serializable]
    internal sealed class AnalyzerConfig
    {
        public bool enabled = true;
        // The analyzer is operated while the tester is standing away from the PC
        // with both controllers in hand. Controller capture is therefore the
        // default workflow; keyboard shortcuts remain only as a desktop fallback.
        public bool controllerButtonsEnabled = true;
        public bool showVrControllerOverlay = true;
        public bool captureScreenshot = true;
        public float continuousSampleRateHz = 30.0f;
        public float calibrationSampleRateHz = 10.0f;
        public float snapshotCountdownSeconds = 1.5f;
        public float snapshotWindowSeconds = 0.75f;
        public string snapshotLabel = "unlabeled-pose";

        public static AnalyzerConfig LoadOrCreate(string path)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(path));

            if (File.Exists(path))
            {
                try
                {
                    AnalyzerConfig loaded = JsonUtility.FromJson<AnalyzerConfig>(File.ReadAllText(path));
                    if (loaded != null)
                    {
                        loaded.Sanitize();
                        return loaded;
                    }
                }
                catch
                {
                    // The caller logs the replacement through the normal startup summary.
                }
            }

            var config = new AnalyzerConfig();
            config.Sanitize();
            File.WriteAllText(path, JsonUtility.ToJson(config, true));
            return config;
        }

        private void Sanitize()
        {
            continuousSampleRateHz = Mathf.Clamp(continuousSampleRateHz, 1.0f, 120.0f);
            calibrationSampleRateHz = Mathf.Clamp(calibrationSampleRateHz, 1.0f, 30.0f);
            snapshotCountdownSeconds = Mathf.Clamp(snapshotCountdownSeconds, 0.0f, 10.0f);
            snapshotWindowSeconds = Mathf.Clamp(snapshotWindowSeconds, 0.1f, 5.0f);
            snapshotLabel = string.IsNullOrWhiteSpace(snapshotLabel) ? "unlabeled-pose" : snapshotLabel.Trim();
        }
    }
}
