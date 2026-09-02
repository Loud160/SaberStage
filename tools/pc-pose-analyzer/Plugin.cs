// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Registers the PC pose analyzer with IPA and forwards game lifecycle events.
// - The plugin is development tooling and is not packaged in the Quest mod.

using IPA;
using UnityEngine;
using IPALogger = IPA.Logging.Logger;

namespace SaberStage.PcPoseAnalyzer
{
    [Plugin(RuntimeOptions.DynamicInit)]
    public sealed class Plugin
    {
        private readonly IPALogger _logger;
        private GameObject _host;

        [Init]
        public Plugin(IPALogger logger)
        {
            _logger = logger;
        }

        [OnEnable]
        public void OnEnable()
        {
            if (_host != null)
            {
                return;
            }

            _host = new GameObject("SaberStage PC Pose Analyzer");
            Object.DontDestroyOnLoad(_host);
            AnalyzerHost analyzer = _host.AddComponent<AnalyzerHost>();
            analyzer.Initialize(_logger);
            _logger.Info("PC Pose Analyzer enabled. FinalIK is treated as a black box; only observable targets and resulting avatar transforms are sampled.");
        }

        [OnDisable]
        public void OnDisable()
        {
            if (_host != null)
            {
                Object.Destroy(_host);
                _host = null;
            }

            _logger.Info("PC Pose Analyzer disabled.");
        }
    }
}
