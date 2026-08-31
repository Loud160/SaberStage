using System;
using System.Linq;
using System.Reflection;
using HMUI;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.UI;
using IPALogger = IPA.Logging.Logger;
using VRUIControls;

namespace SaberStage.PcPoseAnalyzer
{
    /// <summary>
    /// Headset-visible instructions for the controller-only capture workflow.
    /// The transparent panel is placed in the room once and can be dragged with
    /// Beat Saber's normal VR pointer. It is intentionally not head locked.
    /// </summary>
    internal sealed class VrControlOverlay : MonoBehaviour
    {
        private const float CameraRefreshSeconds = 0.5f;

        private IPALogger _logger;
        private Canvas _canvas;
        private Text _instructions;
        private bool _visible;
        private float _nextCameraRefresh;
        private int _boundCameraId;
        private Camera _boundCamera;
        private string _lastText;
        private bool _worldPoseInitialized;
        private VRGraphicRaycaster _vrRaycaster;
        private bool _vrRaycasterReady;
        private Material _nonBloomTextMaterial;

        internal void Initialize(bool initiallyVisible, IPALogger logger)
        {
            _logger = logger;
            _visible = initiallyVisible;
            BuildCanvas();
            RefreshVrCamera(force: true);
            SetCaptureState(false, null, false, false, "Idle", "unlabeled-pose");
        }

        internal bool ToggleVisible()
        {
            _visible = !_visible;
            if (_canvas != null)
            {
                _canvas.enabled = _visible && _boundCamera != null;
            }

            return _visible;
        }

        internal void SetCaptureState(
            bool avatarBound,
            string avatarName,
            bool calibrationActive,
            bool continuousActive,
            string snapshotState,
            string snapshotLabel)
        {
            if (_instructions == null)
            {
                return;
            }

            string status;
            string nextAction;

            if (!avatarBound)
            {
                status = "<color=#FFD166>WAITING FOR AVATAR</color>";
                nextAction = "Load the comparison avatar in Custom Avatars.";
            }
            else if (string.Equals(snapshotState, "Sampling", StringComparison.Ordinal))
            {
                status = "<color=#FF6666>RECORDING POSE - HOLD STILL</color>";
                nextAction = "Keep both hands, wrists, fingers, head, and torso still until the completion vibration.";
            }
            else if (string.Equals(snapshotState, "Countdown", StringComparison.Ordinal))
            {
                status = "<color=#FFE066>POSE COUNTDOWN</color>";
                nextAction = "Move into the requested pose. The stronger vibration marks the measurement window.";
            }
            else if (calibrationActive)
            {
                status = "<color=#56E08A>CALIBRATION TIMELINE RECORDING</color>";
                nextAction = "Complete PC avatar height and arm-span calibration, then press Y again to stop.";
            }
            else if (continuousActive)
            {
                status = "<color=#64C8FF>CONTINUOUS CAPTURE RECORDING</color>";
                nextAction = "Play or move normally. Hold Y for 1.25 seconds when you want to stop logging.";
            }
            else
            {
                status = "<color=#FFFFFF>READY</color>";
                nextAction = "Hold Y for 1.25 seconds to start continuous movement logging.";
            }

            string avatar = avatarBound ? avatarName ?? "bound" : "none";
            string label = string.IsNullOrWhiteSpace(snapshotLabel) ? "unlabeled-pose" : snapshotLabel;
            string text =
                "<b>SABERSTAGE POSE ANALYZER</b>\n" +
                status + "\n" +
                $"Avatar: {avatar}    Label: {label}\n\n" +
                "<b>PRIMARY MOVEMENT TEST</b>\n" +
                "1. Hold Y for 1.25 seconds to start logging.\n" +
                "2. Play a map or move naturally.\n" +
                "3. Hold Y again to stop logging.\n\n" +
                "<b>OPTIONAL CONTROLS</b>\n" +
                "A/X: capture one held pose    Tap Y: calibration start/stop\n" +
                "Tap B: screenshot    Hold B: hide/show panel\n\n" +
                $"<b>NEXT:</b> {nextAction}";

            if (!string.Equals(text, _lastText, StringComparison.Ordinal))
            {
                _lastText = text;
                _instructions.text = text;
            }
        }

        private void Update()
        {
            if (Time.unscaledTime >= _nextCameraRefresh)
            {
                _nextCameraRefresh = Time.unscaledTime + CameraRefreshSeconds;
                RefreshVrCamera(force: false);
                TryEnableVrRaycaster();
            }

        }

        private void BuildCanvas()
        {
            var canvasObject = new GameObject("SaberStage Pose Analyzer VR Help", typeof(RectTransform));
            canvasObject.transform.SetParent(transform, false);

            _canvas = canvasObject.AddComponent<Canvas>();
            // Do not use ScreenSpaceCamera here. Assigning a diagnostic canvas to
            // Beat Saber's changing startup/menu cameras disrupted the legacy
            // Oculus headset output even though the desktop mirror kept drawing.
            // A WorldSpace canvas is an ordinary scene object: it follows the
            // selected view transform without reading or modifying camera state.
            _canvas.renderMode = RenderMode.WorldSpace;
            _canvas.overrideSorting = true;
            _canvas.sortingOrder = 32000;
            _vrRaycaster = canvasObject.AddComponent<VRGraphicRaycaster>();
            // Dynamic UI components are not automatically injected by the game's
            // scene container. Keep this raycaster disabled until its required
            // PhysicsRaycaster service has been copied from a live game canvas.
            _vrRaycaster.enabled = false;

            RectTransform canvasRect = canvasObject.GetComponent<RectTransform>();
            canvasRect.sizeDelta = new Vector2(820.0f, 680.0f);
            canvasRect.localScale = Vector3.one * 0.00115f;

            var panelObject = new GameObject("Instructions Panel", typeof(RectTransform));
            panelObject.transform.SetParent(canvasObject.transform, false);
            RectTransform panelRect = panelObject.GetComponent<RectTransform>();
            panelRect.anchorMin = Vector2.zero;
            panelRect.anchorMax = Vector2.one;
            panelRect.offsetMin = Vector2.zero;
            panelRect.offsetMax = Vector2.zero;

            // Beat Saber's EmptyBoxGraphic registers a valid rectangular depth
            // with its VR raycaster while drawing only degenerate, transparent
            // geometry. It provides a full-size grab surface without restoring
            // the bloom-reactive white background.
            var grabGraphic = panelObject.AddComponent<EmptyBoxGraphic>();
            grabGraphic.color = Color.clear;
            grabGraphic.raycastTarget = true;
            var dragSurface = panelObject.AddComponent<WorldPanelDragHandler>();
            dragSurface.Initialize(canvasRect);

            var textObject = new GameObject("Instructions", typeof(RectTransform));
            textObject.transform.SetParent(panelObject.transform, false);
            RectTransform textRect = textObject.GetComponent<RectTransform>();
            textRect.anchorMin = Vector2.zero;
            textRect.anchorMax = Vector2.one;
            textRect.offsetMin = new Vector2(28.0f, 24.0f);
            textRect.offsetMax = new Vector2(-28.0f, -24.0f);

            _instructions = textObject.AddComponent<Text>();
            _instructions.font = Resources.GetBuiltinResource<Font>("Arial.ttf");
            _instructions.fontSize = 27;
            _instructions.lineSpacing = 1.05f;
            _instructions.alignment = TextAnchor.UpperLeft;
            _instructions.horizontalOverflow = HorizontalWrapMode.Wrap;
            _instructions.verticalOverflow = VerticalWrapMode.Truncate;
            _instructions.supportRichText = true;
            // Only the panel background is transparent. Keep the glyphs fully
            // opaque and outline them so the instructions remain readable over
            // both bright and dark Beat Saber environments.
            _instructions.color = Color.white;
            Shader uiShader = Shader.Find("UI/Default");
            if (uiShader != null)
            {
                _nonBloomTextMaterial = new Material(uiShader)
                {
                    name = "SaberStage Pose Analyzer Non-Bloom Text",
                };
                // Beat Saber's bloom composite uses the framebuffer alpha written
                // by UI materials as glow weight. Preserve visible RGB while
                // suppressing alpha writes so this diagnostic text stays crisp.
                if (_nonBloomTextMaterial.HasProperty("_ColorMask"))
                {
                    _nonBloomTextMaterial.SetInt("_ColorMask", 7); // RGB only
                }
                _instructions.material = _nonBloomTextMaterial;
            }
            _instructions.raycastTarget = false;
            var textOutline = textObject.AddComponent<Outline>();
            textOutline.effectColor = new Color(0.0f, 0.0f, 0.0f, 1.0f);
            textOutline.effectDistance = new Vector2(1.5f, -1.5f);
            textOutline.useGraphicAlpha = false;
        }

        private void RefreshVrCamera(bool force)
        {
            if (!force && _boundCamera != null && _boundCamera.isActiveAndEnabled &&
                _boundCamera.GetInstanceID() == _boundCameraId)
            {
                return;
            }

            Camera mainCamera = Camera.main;
            Camera candidate = mainCamera != null && mainCamera.isActiveAndEnabled
                ? mainCamera
                : Camera.allCameras
                .Where(camera => camera != null && camera.isActiveAndEnabled)
                .Where(camera => !IsAuxiliaryCamera(camera.name))
                .OrderByDescending(camera => camera.name.IndexOf("Main", StringComparison.OrdinalIgnoreCase) >= 0)
                .ThenByDescending(camera => camera.depth)
                .FirstOrDefault();

            if (candidate == null)
            {
                if (_canvas != null)
                {
                    _canvas.enabled = false;
                }
                _boundCamera = null;
                _boundCameraId = 0;
                return;
            }

            _canvas.enabled = _visible;
            _boundCamera = candidate;
            _boundCameraId = candidate.GetInstanceID();
            if (!_worldPoseInitialized)
            {
                PlacePanelInFrontOfView();
            }
            _logger?.Debug($"Pose analyzer movable world-space help initialized from view transform '{candidate.name}' without modifying camera state.");
        }

        private void TryEnableVrRaycaster()
        {
            if (_vrRaycasterReady || _vrRaycaster == null)
            {
                return;
            }

            const BindingFlags flags = BindingFlags.Instance | BindingFlags.NonPublic;
            FieldInfo physicsRaycasterField = typeof(VRGraphicRaycaster).GetField("_physicsRaycaster", flags);
            if (physicsRaycasterField == null)
            {
                _logger?.Error("Pose analyzer could not locate VRGraphicRaycaster's injected physics service field; panel dragging remains disabled.");
                return;
            }

            VRGraphicRaycaster source = Resources.FindObjectsOfTypeAll<VRGraphicRaycaster>()
                .FirstOrDefault(candidate =>
                    candidate != null &&
                    candidate != _vrRaycaster &&
                    physicsRaycasterField.GetValue(candidate) != null);
            if (source == null)
            {
                return;
            }

            physicsRaycasterField.SetValue(_vrRaycaster, physicsRaycasterField.GetValue(source));
            _vrRaycaster.enabled = true;
            _vrRaycasterReady = true;
            _logger?.Info("Pose analyzer world panel grab surface connected to Beat Saber's VR pointer.");
        }

        private void PlacePanelInFrontOfView()
        {
            if (_canvas == null || _boundCamera == null)
            {
                return;
            }

            // Establish the initial room-space pose once. The canvas remains under
            // the persistent analyzer host and is never updated from head movement
            // afterward; the user can reposition it by dragging the panel.
            RectTransform canvasRect = _canvas.GetComponent<RectTransform>();
            canvasRect.position = _boundCamera.transform.TransformPoint(new Vector3(0.0f, 0.0f, 1.45f));
            canvasRect.rotation = _boundCamera.transform.rotation;
            canvasRect.localScale = Vector3.one * 0.00115f;
            _worldPoseInitialized = true;
        }

        private static bool IsAuxiliaryCamera(string cameraName)
        {
            if (string.IsNullOrEmpty(cameraName))
            {
                return false;
            }

            return cameraName.IndexOf("Camera2", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   cameraName.IndexOf("Spectator", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   cameraName.IndexOf("Mirror", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   cameraName.IndexOf("ThirdPerson", StringComparison.OrdinalIgnoreCase) >= 0;
        }

        private void OnDestroy()
        {
            if (_nonBloomTextMaterial != null)
            {
                Destroy(_nonBloomTextMaterial);
                _nonBloomTextMaterial = null;
            }
        }

    }

    /// <summary>
    /// Drag behavior attached to the game's invisible EmptyBoxGraphic hit target.
    /// Rendering and pointer registration remain separate from movement logic.
    /// </summary>
    internal sealed class WorldPanelDragHandler : MonoBehaviour, IInitializePotentialDragHandler, IBeginDragHandler, IDragHandler, IEndDragHandler
    {
        private RectTransform _panel;
        private Vector3 _lastWorldHit;
        private bool _dragging;

        internal void Initialize(RectTransform panel)
        {
            _panel = panel;
        }

        public void OnInitializePotentialDrag(PointerEventData eventData)
        {
            eventData.useDragThreshold = false;
        }

        public void OnBeginDrag(PointerEventData eventData)
        {
            if (_panel == null || !TryGetWorldHit(eventData, out _lastWorldHit))
            {
                return;
            }

            _dragging = true;
        }

        public void OnDrag(PointerEventData eventData)
        {
            if (!_dragging || _panel == null || !TryGetWorldHit(eventData, out Vector3 worldHit))
            {
                return;
            }

            _panel.position += worldHit - _lastWorldHit;
            _lastWorldHit = worldHit;
        }

        public void OnEndDrag(PointerEventData eventData)
        {
            _dragging = false;
        }

        private static bool TryGetWorldHit(PointerEventData eventData, out Vector3 worldHit)
        {
            worldHit = eventData.pointerCurrentRaycast.worldPosition;
            if (worldHit != Vector3.zero)
            {
                return true;
            }

            worldHit = eventData.pointerPressRaycast.worldPosition;
            return worldHit != Vector3.zero;
        }
    }
}
