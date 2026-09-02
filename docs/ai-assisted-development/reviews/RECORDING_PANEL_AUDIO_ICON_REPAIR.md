<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors -->
<!-- Part of SaberStage. See LICENSE and LICENSE-ADDITIONAL-TERMS.md. -->

# Recording panel audio icon repair

Date: 2026-09-02. Branch: `logger-hardening-and-repo-audit`.
Checkpoint `83a682e` predates both this work and the uncommitted chat joystick/
size follow-up. That chat work is preserved unchanged in this pass.

## Request and evidence

The user reports that pausing a stream makes the speaker icon solid white,
and requests 15% larger speaker/microphone buttons.
The user clarified during implementation that the artwork is already too large
relative to the buttons: enlarge the blue buttons only, not the images.

- The embedded speaker-mute PNG exactly matches the user's original
  `C:/Users/Owner/Downloads/volume_mute-50.png` (SHA-256
  `18f60dedba7451226f47572e756e8fc14b5a43573721c130cb958a1732dc9ab9`).
- `RecordingController::LivestreamSnapshot` already sets game-audio mute when
  AFK; `RefreshRecordingWorldPanel` already selects the matching muted PNG.
  The actual audio-mixing/pause behavior therefore needs no change.
- A read-only log pull records AFK transitions at 18:48:55 and 18:49:46. It is
  saved under `diagnostics/recording-panel-audio-icons/before-fix-native.log`.
  That old build does not log per-icon texture validity, so it cannot establish
  exactly when a texture was lost or prove that this was the observed cause.
- Source review found a lifetime defect: five decoded textures were held only
  by raw pointers in a static native cache. Unselected icons had no RawImage
  reference, no strong managed cache reference, and no DontUnloadUnusedAsset
  protection. The refresh path passed the cached pointer without validity checks.
  A missing texture can produce RawImage's white fallback instead of the PNG.

## Changes

- Retain all five icon variants with `SafePtrUnity<Texture2D>` and
  `HideFlags::DontUnloadUnusedAsset`, independently of panel visibility/state.
- Validate the cache on refresh; regenerate a destroyed texture from its embedded
  PNG. Decode/allocation failures are latched so repeated refreshes do not decode
  or log continuously. No texture is decoded during healthy steady-state updates.
- Guard icon binding against missing textures. A failed icon is not displayed
  as a misleading white rectangle; the existing blue button remains. Creation,
  recovery, and actual texture changes log asset name/instance ID for verification.
- Increase button width/height from 7.7 x 6.875 to 8.855 x 7.90625 (15% on each
  dimension). Keep artwork fixed at 3.8 square; do not apply that multiplier to
  images. Exclude each RawImage from the prefab's layout calculations so it
  cannot be stretched into the extra padding intended around the artwork.
- Preserve button centers, blue styling, existing mute actions/AFK guards,
  all other controls, and the rest of the layout. Compile-time bounds checks
  enforce clearance from neighboring buttons, the border, and the grab area.

## Validation

Tooling coverage checks retained/recoverable textures, guarded binding, AFK's
existing mute selection, button-only enlargement, fixed artwork, and compiled geometry checks.
**8/8 host suites and 50/50 tooling tests passed**, including the corrected
button-only sizing; see `diagnostics/recording-panel-audio-icons/host-final.log`
and `tooling-final.log` in that directory. The full ARM64 build succeeded, followed
by an incremental rebuild for the sizing clarification and private-logger ELF
verification (`android-button-only.log`). `git diff --check` passed.

Final binary SHA-256:
`81d13ad5d18ecad90ada7d5a7e13d6dfa9b5ab56ad9ddbea482a56058208b385`.
No deployment was requested in this turn; neither build was loaded onto the Quest.

Headset acceptance: pause after a map/scene change and verify the speaker switches
to the supplied mute artwork; resume restores the actual game-audio state;
manual mute/unmute and all microphone variants remain correct; both buttons are
larger without stealing adjacent button clicks. Build tests cannot prove this
visual behavior; use the new `Recording-panel ... icon` log lines if it recurs.
