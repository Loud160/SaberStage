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
