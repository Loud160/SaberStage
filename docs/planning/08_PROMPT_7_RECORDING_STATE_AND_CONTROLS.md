# Prompt 7 — Production recording state machine, controls, and presets

> Partial checkpoint: a native tabbed right-side `Record`/`Files` panel now provides Start, Pause, Resume, Stop & Save, optional Gameplay Only mode, 30/60 FPS, 4/8/12 Mbps, live elapsed status, output location, and last-saved filename. Start begins immediately and continues across scene changes by default; Gameplay Only arms in menus and stops after the song. Pause closes the current hardware-video segment and suspends audio capture without writing paused time; Resume appends a fresh encoder segment beginning with a new keyframe while continuing the same final session. Legal/illegal state transitions have host tests. Beat Saber's gameplay pause menu now also receives compact native Start/Pause/Resume and Stop & Save controls, placed opposite Big Screen's controls so the two mods do not overlap. An opt-in, release-triggered in-game chord uses both thumbsticks: 0.75 seconds toggles start/pause/resume and 2.5 seconds stops and saves, with host tests covering thresholds, release behavior, and repeat suppression. Concatenated-segment muxing, A/V timing, pause-menu layout, OVR input behavior, and all runtime controls still require headset/media validation. Song-result feedback and unobtrusive in-game status remain future work.

Implement complete recording controls using the explicit state machine.

## Controls must work from

1. main/mod menu;
2. Beat Saber pause menu;
3. active gameplay through a configurable safe controller shortcut/chord.

Support:

- Start
- Pause
- Resume
- Stop

Expose understandable recording settings:

- 720p30;
- 1080p30;
- 1080p60 only when measured safe;
- bitrate presets and validated advanced bitrate;
- game audio enable/disable where supported;
- selected camera/profile;
- output status and path;
- controller shortcuts.
- optional `Gameplay Only`, default off.

Persist these choices. Settings that cannot safely change during active recording must be disabled or deferred with a clear explanation.

## Required behavior

- start from menu;
- continue one session through menu, loading, map, results, and later menu navigation until explicitly stopped;
- start during map;
- pause recording while gameplay continues;
- resume;
- pause Beat Saber while recording continues;
- pause recording while Beat Saber is paused;
- resume either state in either order;
- stop from menu;
- stop from pause menu;
- stop via in-map shortcut;
- song completion (continue by default; stop only in Gameplay Only mode);
- restart/quit (continue through in-game scene changes; safely finalize on application shutdown);
- duplicate rapid input.

No race may leave orphaned encoder/audio/file state.

Recording pause removes paused capture time.

On resume request/produce an appropriate keyframe.

## HMD-only feedback

Show unobtrusive:

- recording;
- paused;
- elapsed recorded time;
- start/stop success/failure.

Do not include this status in the broadcast output by default.

Persist shortcuts and user choices.

Add tests for all legal/illegal state transitions.
