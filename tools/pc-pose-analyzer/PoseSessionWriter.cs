// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Writes pose-analysis sessions and metadata to durable files.
// - Session boundaries keep incomplete or unrelated runs from being combined accidentally.

using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Threading;
using Newtonsoft.Json;
using UnityEngine;
using IPALogger = IPA.Logging.Logger;

namespace SaberStage.PcPoseAnalyzer
{
    internal sealed class PoseSessionWriter : IDisposable
    {
        private readonly IPALogger _logger;
        private readonly BlockingCollection<QueuedLine> _queue = new(new ConcurrentQueue<QueuedLine>());
        private readonly Thread _thread;
        private readonly StreamWriter _writer;
        private bool _disposed;
        private bool _poseSchemaValidated;

        public PoseSessionWriter(string root, IPALogger logger)
        {
            _logger = logger;
            SessionDirectory = Path.Combine(root, DateTime.UtcNow.ToString("yyyyMMdd-HHmmss"));
            Directory.CreateDirectory(SessionDirectory);

            string dataPath = Path.Combine(SessionDirectory, "pose-data.jsonl");
            _writer = new StreamWriter(new FileStream(dataPath, FileMode.CreateNew, FileAccess.Write, FileShare.Read), new System.Text.UTF8Encoding(false), 64 * 1024)
            {
                AutoFlush = false,
            };

            _thread = new Thread(WriterLoop)
            {
                IsBackground = true,
                Name = "SaberStage Pose Analyzer Writer",
                Priority = System.Threading.ThreadPriority.BelowNormal,
            };
            _thread.Start();
        }

        public string SessionDirectory { get; }

        public void WriteSessionHeader(AnalyzerConfig config)
        {
            Assembly analyzer = typeof(PoseSessionWriter).Assembly;
            Assembly customAvatar = AppDomain.CurrentDomain.GetAssemblies().FirstOrDefault(item => item.GetName().Name == "CustomAvatar");
            Assembly game = AppDomain.CurrentDomain.GetAssemblies().FirstOrDefault(item => item.GetName().Name == "Main");

            var header = new SessionHeader
            {
                createdUtc = DateTime.UtcNow.ToString("O"),
                analyzerVersion = FileVersionInfo.GetVersionInfo(analyzer.Location).ProductVersion ?? analyzer.GetName().Version.ToString(),
                gameVersion = game?.GetName().Version?.ToString() ?? Application.version,
                customAvatarVersion = customAvatar?.GetName().Version?.ToString() ?? "not-loaded-at-session-start",
                continuousSampleRateHz = config.continuousSampleRateHz,
                calibrationSampleRateHz = config.calibrationSampleRateHz,
                controllerButtonsEnabled = config.controllerButtonsEnabled,
                vrControllerOverlayEnabled = config.showVrControllerOverlay,
            };
            Enqueue(header, flush: true);
        }

        public void WriteMarker(string marker, string captureId, string detail, bool flush = false)
        {
            var record = new MarkerRecord
            {
                createdUtc = DateTime.UtcNow.ToString("O"),
                frame = Time.frameCount,
                unscaledTime = Time.unscaledTime,
                marker = marker,
                captureId = captureId,
                detail = detail,
            };
            Enqueue(record, flush);
        }

        public void WriteSample(PoseSample sample)
        {
            Enqueue(sample, flush: false);
        }

        public void WriteCalibration(CalibrationRecord record, bool flush)
        {
            Enqueue(record, flush);
        }

        private void Enqueue(object record, bool flush)
        {
            if (_disposed || _queue.IsAddingCompleted)
            {
                return;
            }

            _queue.Add(new QueuedLine(record, flush));
        }

        private void WriterLoop()
        {
            try
            {
                var lastFlush = Stopwatch.StartNew();
                foreach (QueuedLine item in _queue.GetConsumingEnumerable())
                {
                    // Unity's JsonUtility silently omitted every nested custom
                    // pose object even though it emitted the sample metadata.
                    // Serialize the immutable DTO snapshot on this background
                    // thread with the Newtonsoft runtime already shipped by the
                    // game, keeping both serialization and disk I/O off Unity's
                    // frame thread.
                    string line = JsonConvert.SerializeObject(item.Record, Formatting.None);
                    ValidatePoseSchemaOnce(item.Record, line);
                    _writer.WriteLine(line);

                    if (item.Flush || lastFlush.ElapsedMilliseconds >= 500 || _queue.Count == 0)
                    {
                        _writer.Flush();
                        lastFlush.Restart();
                    }
                }

                _writer.Flush();
            }
            catch (Exception ex)
            {
                _logger.Error($"Pose analyzer writer stopped: {ex.GetType().Name}: {ex.Message}");
            }
        }

        private void ValidatePoseSchemaOnce(object record, string json)
        {
            if (_poseSchemaValidated || !(record is PoseSample))
            {
                return;
            }

            string[] requiredSections =
            {
                "\"torso\":",
                "\"targets\":",
                "\"bones\":",
                "\"leftArm\":",
                "\"rightArm\":",
                "\"spine\":",
                "\"fingers\":",
            };

            string missing = requiredSections.FirstOrDefault(section =>
                json.IndexOf(section, StringComparison.Ordinal) < 0);
            if (missing != null)
            {
                throw new InvalidDataException(
                    $"Pose serialization self-check failed; missing nested section {missing}");
            }

            _poseSchemaValidated = true;
            _logger.Info("Pose analyzer nested target, bone, arm, spine, and finger serialization validated.");
        }

        public void Dispose()
        {
            if (_disposed)
            {
                return;
            }

            _disposed = true;
            _queue.CompleteAdding();
            bool writerStopped = _thread.Join(2000);
            if (!writerStopped)
            {
                _logger.Warn("Pose analyzer writer did not finish within two seconds; the newest diagnostic samples may be incomplete.");
            }

            // Do not dispose objects that a still-running writer thread may be using.
            // It is a background thread and the process is already shutting down.
            if (writerStopped)
            {
                _writer.Dispose();
                _queue.Dispose();
            }
        }

        private readonly struct QueuedLine
        {
            public QueuedLine(object record, bool flush)
            {
                Record = record;
                Flush = flush;
            }

            public object Record { get; }
            public bool Flush { get; }
        }
    }
}
