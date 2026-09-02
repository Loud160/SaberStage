<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors -->
<!-- Part of SaberStage. See LICENSE and LICENSE-ADDITIONAL-TERMS.md. -->

# Chat panel scrollbar investigation

Date: 2026-09-02. Baseline: `1a70f3b`, branch `logger-hardening-and-repo-audit`.

## Scope and current status

The user first requested detailed logging using Native Logger Quest and an
explanation for the missing chat scrollbar, then authorized fixing the confirmed
defects. The repair retains the existing native scrollbar and chat-update
diagnostics. No unrelated menus, recording/streaming behavior, or avatar code
are changed.

The instrumented Quest run now confirms an inactive native indicator, content
height collapsing to zero, and exceptions aborting chat updates on no-hit input.
See the runtime findings below. The targeted code repair is now implemented;
post-repair headset verification is still required. Build and host tests alone
do not prove that a world-space UI control is visible.

## Repair implementation

After the user requested "fix this":

- `EnsureChatWorldPanel()` disables the native outer ContentSizeFitter,
  VerticalLayoutGroup, and BSML::ScrollViewContent, as well as the inner layout,
  before creating pooled rows. No competing layout writer remains active on the
  virtual content. The native ScrollView, viewport, scrollbar, and buttons remain.
- `ReflowChatWorldPanelText()` explicitly sizes **both** content containers to
  the measured virtual-list height. The inner container is top-centered at zero
  offset; HMUI alone scrolls the outer container. Resizing/reflow preserves the
  reading position, clamped to the new range when rewrapping shortens the list.
- `RefreshChatWorldPanelScrollControls()` updates the native button states and
  handle geometry, then restores the inherited inactive indicator/page-button
  objects. Page buttons use their original target graphics for raycasts. They
  are not forced interactable when the native scroll range is empty.
- `ChatPanelScrollGeometry.hpp` centralizes text width, content height, overflow,
  and scroll extent using the **actual viewport**. The captured viewport was
  six units shorter than the surrounding scroll root. Cached geometry also drives
  pooled-row visibility, so wrapping, scrolling, and clipping use the same area.
- Optional EventSystem, input module, VRPointer, hit object, and transform
  wrappers are checked before access. No-hit input now sets hover false while
  message polling and live-tail updates continue. A missing movement handle no
  longer prevents stale native hover state from being cleared.
- Chat background and resize-grip zero positions use explicit initialized Unity
  vectors. The observer's missing-rect fallback is explicitly zeroed as well;
  generated Unity value-type default constructors leave their fields uninitialized.

Per-frame row updates still use the existing 32-row pool and cached dimensions;
Twitch snapshots remain throttled to 10 Hz. Layout ownership is configured once
per panel creation; content geometry/visibility is updated on reflow, not by
stacking a per-frame correction over the native layout. The diagnostic observer
and its bounded logging remain enabled for the next verification session.

### Repair verification

Local host validation: **8/8 CTest suites** and **46/46 tooling tests** pass.
`ChatPanelScrollGeometryTests.cpp` covers the recorded 68.196-unit page with
96.100- and 139.170-unit histories, exact-fit/empty pages, the six-unit padding
gap, resize/reflow, retained history larger than the render pool, and invalid
dimensions. Tooling checks cover both layout owners, native-control restoration,
explicit zero vectors, and the non-throwing input path continuing to message refresh.

The ARM64 build and private-logger ELF validation passed. Repair binary SHA-256:

`036309991665af02374ae8d834848bc6ff1fd1631b31a92ba3d42c7b0cddc4aa`

Build log: `diagnostics/chat-scrollbar-repair-android.log`. Host/tooling logs:
`diagnostics/chat-scrollbar-repair-host.log` and
`diagnostics/chat-scrollbar-repair-tooling-final.log`. All local validation passed.
After the user authorized loading, the repaired build was deployed to Quest 2
`1WMHH840QJ1046` on 2026-09-02. Beat Saber was stopped before replacement, and
the receipt-owned deployment verified the mod hash above plus all three private
FFmpeg library hashes. Beat Saber remains stopped for the user to start testing.

On-headset acceptance remains: visible native scrollbar; no content-height overwrite after
settling; latest messages continue once full; joystick/page buttons can scroll
up and back down; no-hit pointers do not generate exceptions; resize/reopen keeps
text wrapped and the black backdrop inside the panel. Short chat must not scroll
prematurely. No additional layout redesign is part of this repair.

## Confirmed runtime findings: user overflow test

After deployment, the user opened the panel and sent Twitch messages until it
filled and stopped displaying new messages. The read-only capture is:

`diagnostics/chat-scrollbar-investigation/SaberStage-Support-Logs-20260902-173800.zip`

The archive confirms installed SHA-256
`d64496f2799be0cb213f6181e3f8dc9a8222b40d2b875f3cc435e45a74575c1f`.
New diagnostic records begin at `17:36:28`. The current log also retains older
records; do not mistake earlier generic catch messages for this build's output.
No observer-failure records occurred in the instrumented run.

### 1. The scrollbar exists, but its GameObject is inactive

At `17:36:54.903`, after the message list overflowed:

```text
node role=indicator name=VerticalScrollIndicator layer=5 activeSelf=false activeHierarchy=false
rect role=indicator size=(1.60,58.20) panelBounds=(28.20,-33.60)-(29.80,24.60)
graphic role=indicator enabled=true culled=false clipped=false depth=25 alpha=1.000 inheritedAlpha=1.000
node role=indicator-handle name=Handle layer=5 activeSelf=true activeHierarchy=false
graphic role=indicator-handle enabled=true culled=false clipped=false depth=26 alpha=1.000 inheritedAlpha=1.000
```

The panel bounds are `(-35.50,-43.60)-(35.50,43.60)`. The indicator is inside
those bounds, on the UI layer, and has valid nonzero dimensions. Its parent
ScrollBar object is active. The indicator itself is inactive, so its child
handle cannot render. It was already inactive in the initial native-clone
snapshot and remained inactive after overflow. This is not merely an indicator
drawn behind the background or one missing from the hierarchy. The logs do not
identify every native caller that might set visibility; do not claim that the
height overwrite is the only reason its active state stayed false.

### 2. The real native scroll range is erased after every content update

At `17:36:54.901`:

```text
entries=3 logicalOverflow=true requestedHeight=96.100 immediateReadback=96.100 observedHeight=0.000 pageHeight=68.196 position=0.000
```

At `17:37:25.095`:

```text
entries=4 logicalOverflow=true requestedHeight=139.170 immediateReadback=139.170 observedHeight=0.000 pageHeight=68.196 position=0.000
```

Both inner and outer content RectTransforms measure `(0.00,0.00)`. The outer
VerticalLayoutGroup, ContentSizeFitter (PreferredSize on both axes), and
BSML::ScrollViewContent are enabled. The inner VerticalLayoutGroup is disabled.
Combined with the verified BSML implementation below, this confirms that the
wrong layout owner was disabled: native layout/size updates collapse the virtual
list's dimensions after SaberStage writes the correct measured height.
HMUI has no remaining scroll distance, explaining why filling the visible area
does not advance to later rows even when entries have been received.

### 3. Normal missing input objects abort the rest of the chat update

The first operation-level exception is at `17:36:28.283`:

```text
operation=read current UI event system
error=6UnityWIN11UnityEngine12EventSystems11EventSystemEE is holding a null Object
```

Subsequent errors identify `operation=read VR pointer hit GameObject`, with the
GameObject null-wrapper exception predicted by source inspection. The last
captured failure counter at `17:37:31.278` is **2,942**. The no-hit read occurs
before hover assignment, follow-live, row recycling, and the Twitch snapshot
refresh. Those later operations are skipped whenever the exception occurs.
The native event system and optional input-module/pointer references also need
non-throwing presence checks; changing only the final hit-object access is not
a complete correction of this input path.

### 4. A separate, concrete background-position defect

The black backdrop has an invalid local Y near `-1.18e19` and is culled/clipped.
`UpdateChatWorldPanelLayout()` passes `{}` as its position to `setRect`.
The pinned generated `UnityEngine::Vector2::Vector2()` constructor is empty;
it does not initialize x/y. Thus this default construction is not a zero vector.
Use an explicitly initialized `{0.0F, 0.0F}` for this panel's center position.
This explains invalid backdrop geometry, not the indicator's inactive flag.

### Narrow repair targets

- Establish one owner for virtual content geometry, including both inner and
  outer sizes, and stop the competing outer fitter/layout/content-driver writes.
- Restore and maintain the existing native indicator/page controls' intended
  active state; do not introduce a second scrollbar or move unrelated menus.
- Check optional UnityW references before unwrapping so missing UI input is
  ordinary state, not an exception that skips chat processing.
- Explicitly initialize the chat backdrop's center coordinate.
- Keep diagnostics for the verification run. Native content height should remain
  equal to its last requested size after layout settles; overflow must produce
  a nonzero scroll range, an active native indicator, and updating latest rows.

At the end of the collection stage only this document changed. The later
user-authorized repair is described above; these findings describe the original
diagnostic build, not a post-repair capture.

## Evidence collected before instrumentation

The read-only support capture is locally retained at:

`diagnostics/chat-scrollbar-investigation/SaberStage-Support-Logs-20260902-171853.zip`

Its `source-install-files.txt` confirms the installed mod SHA-256 was:

`4c7a43eccdd25bbde6c15bc8eeb96ef46f4efe72cebbc9f83d9200c6eebd9880`

The archive's current `saberstage-native.log` contains **293** chat-update
exception records. One exact record (joined onto one line) is:

```text
[2026-09-02 17:15:37.128][ERROR][SaberStage][tid=537946036720] Twitch chat panel update failed: 6UnityWIN11UnityEngine10GameObjectEE is holding a null Object (MenuController.cpp:7057 in void saberstage::ui::MenuController::TickChatWorldPanel())
```

That source location is the outer catch, not the throwing operation. It cannot
by itself prove which access failed or whether it caused the missing graphic.
Support archives remain local/untracked and are not bundled with this document.

## Source findings

### The native scrollbar is present in the creation path

`MenuController::EnsureChatWorldPanel()` calls `BSML::Lite::CreateScrollView()`.
Pinned BSML version: `0.4.55` in `qpm.shared.json`.

The verified reference checkout is
`C:/Users/Owner/source/repos/Quest-BSML`, tag `v0.4.55`:

- `src/BSML/Tags/ScrollViewTag.cpp` clones the game's EULA TextPageScrollView.
  It retains the page buttons and `VerticalScrollIndicator`, then assigns them
  to the replacement `BSML::ScrollView` component.
- `src/BSML-Lite/Creation/Layout.cpp` returns the tag's inner content container.
- `src/BSML/Components/ScrollViewContent.cpp` updates the native scroll size
  from its first child's actual RectTransform height when its layout is dirty.

SaberStage does not explicitly remove that indicator. Disabling
`Graphic.raycastTarget` affects input interception, not graphic visibility.
Actual object creation, active state, layer, clipping, and draw order still
need runtime inspection.

### Two layout owners can disagree about scrollable height

The native hierarchy is:

```text
BSMLScrollView (native page buttons and VerticalScrollIndicator)
  Viewport
    BSMLScrollViewContent             <- HMUI contentTransform
      ContentSizeFitter, VerticalLayoutGroup, BSML::ScrollViewContent
      BSMLScrollViewContentContainer  <- CreateScrollView return value
        disabled VerticalLayoutGroup
        pooled chat text rows
```

SaberStage disables layout on the **inner** returned object, but the active
fitter/layout/content driver is on the **outer** object. SaberStage computes
the virtual list height and calls `SetContentSize(contentHeight)`. BSML can
subsequently call `SetContentSize(inner.rect.height)` using the independently
sized inner container. The pooled rows are manually positioned; their heights
are not assigned to that inner container as a total list height.

This is a concrete competing-writer path, not yet proof that it explains the
missing graphic in this session. The diagnostic records requested height,
immediate native readback, later observed height, both container rectangles,
and all three outer layout components to resolve that question.

### A pointer null check happens too late

The current update code evaluates:

```cpp
auto* pointedObject = pointer->get_pointingOver().ptr();
if (IsAlive(pointedObject)) { /* ... */ }
```

`extern/includes/bs-cordl/include/cordl_internals/unity-utils.hpp` implements
`UnityW::ptr()` by **throwing** when the wrapper is empty/destroyed. Therefore
an ordinary no-hit pointer state throws before reaching the null check. This
access was added in `1a70f3b`. It is a definite defect and a strong candidate
for the recorded GameObject exceptions, but the old catch does not establish
the exact runtime source. The new operation marker distinguishes this access
from row activation, resize-handle updates, and other GameObject accesses.

The diagnostic observer itself checks wrapper liveness before unwrapping;
missing references must be loggable without the observer causing another
null-wrapper exception. The initial diagnostic build preserved the failure for
identification; the subsequently authorized repair fixes the input path.

## What the new logs capture

Implementation: `src/ui/ChatPanelDiagnostics.cpp`, with policy/context in
`include/saberstage/ui/ChatPanelDiagnostics.hpp`. Integration is confined to
the chat functions in `src/ui/MenuController.cpp`.

- `ChatPanelDiag snapshot reason=native-created`: hierarchy before SaberStage
  adjusts it. This snapshot is explicitly **not settled**.
- `reason=settled-or-state-change`: samples before this update's resize/reflow,
  so our next height write cannot conceal another component's previous write.
- Requested/immediate/later content height, time since write, write count,
  native page height/scroll position/hover, bounded entry and row-pool counts.
- Indicator and handle existence, IDs, enabled/active state, rect dimensions,
  anchors/pivot, layer, sibling index, local depth/scale and bounds in panel units.
- Graphic alpha, CanvasRenderer alpha/inherited alpha, culling/clipping,
  absolute draw depth, already-bound material/shader/render queue.
- Ancestor Canvas sorting/render mode/camera mask, CanvasGroup alpha and
  RectMask2D/stencil Mask state. Draw depths are only directly comparable within
  the same canvas; depth alone does not prove occlusion across canvases.
- Native page-button state; outer/inner layout/fitter/content-driver identity,
  enabled state, fit modes, and the driver's cached inner reference.
- `content-height-overwritten`: only emitted if the write first read back
  correctly and a subsequent observation differs beyond subpixel tolerance.
- `no-native-scroll-range`: the measured message list overflows but HMUI reports
  no usable scroll range. This is not emitted merely because a short chat fits.
- Update exceptions include the operation name, row slot when relevant,
  operation source location, actual exception text, total/suppressed counts.

No chat text, authors, OAuth tokens, stream keys, or connection URLs are logged
by these diagnostics. No synchronous logger flush, worker-thread Unity access,
forced layout rebuild, material generation, or scene-wide traversal is added.

Sampling is at most twice per second. Detailed dumps occur after creation and
settling, then at most once per five seconds when health/size state changes.
Stable state produces a compact heartbeat every 30 seconds. Traversal is bounded
to ten indicator ancestors and 24 indicator subtree nodes. Repeated update
errors are rate-limited to one record per five seconds, with suppressed counts.
An exception inside the observer disables observation for that panel instance
and logs the actual error; it does not disable or alter the chat panel.

## Next on-device checks

1. With permission to interrupt Beat Saber, stop the game and use the existing
   receipt/hash-verified deploy path. Do not replace the loaded mod in place.
2. Open chat and leave it visible for at least five seconds. Keep it open both
   when a small amount of text fits and when existing chat history overflows.
3. Point at the chat area, then away into empty space. Try the joystick while
   pointing at the panel. The update-error operation identifies where a failed
   tick stops before reaching native scrolling or message reflow.
4. Resize once, then leave it settled. Compare initial and settled scrollbar
   bounds to viewport/panel mask bounds and backdrop depth/alpha.
5. Collect support logs; search for `ChatPanelDiag` and
   `Twitch chat panel update failed`. Do not infer visibility from an empty-chat
   range alone or claim an occlusion cause from source order alone.

If the overwrite is confirmed, repair ownership of the existing native content
geometry coherently rather than adding another scrollbar or per-frame sizing
patch. If the graphic is clipped/hidden, correct that actual hierarchy/mask/
material cause while retaining the native control. Fix the pointer null access
at its source rather than relying on the outer catch for ordinary no-hit input.

## Validation

`tests/ChatPanelDiagnosticsTests.cpp` tests sampling and detail-log budgets,
settling/reset behavior, heartbeat intervals, non-finite inputs, and the evidence
required to distinguish an overwritten height from a failed initial write.
`tests/ToolingTests.py` checks the observer remains read-only, logs through the
local logger, avoids throwing unwraps, and samples before resize/reflow.

The initial implementation stage left runtime verification pending; the later
user overflow test and confirmed findings are recorded near the top of this file.

Local validation completed: **7/7 host suites**, **44/44 tooling tests**, ARM64
build, and private-logger ELF dependency verification passed. The first ARM64
link exposed a missing generated Vector3 implementation include in the new
observer; explicit Unity value-type includes corrected it and the rebuild passed.
After the user authorized stopping/loading, deployment to Quest 2
`1WMHH840QJ1046` succeeded. Beat Saber was stopped before replacement; the
receipt-owned mod and all three private FFmpeg library hashes were verified.
No automatic game launch was requested. Diagnostic mod SHA-256:

`d64496f2799be0cb213f6181e3f8dc9a8222b40d2b875f3cc435e45a74575c1f`
