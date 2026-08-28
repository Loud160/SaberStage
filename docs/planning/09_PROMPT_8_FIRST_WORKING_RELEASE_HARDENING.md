# Prompt 8 — Harden and accept the first working SaberStage release

Treat the camera, movable preview, game audio, local recorder, and recording controls as one complete Quest-only product stage.

Do not begin companion, TV, avatar, compositor, livestream, chat, or Discord implementation in this phase.

## Required user outcome

A user can:

1. install and launch SaberStage;
2. position a single independent Camera2-familiar third-person camera anywhere practical and configure its transform, FOV, and requested resolution;
3. optionally enable smooth anchored-float motion without changing the saved base placement;
4. load and run a supported Camera2 movement script with deterministic song-time behavior;
5. show, place, resize, hide, and restore the in-game preview panel;
6. record real gameplay from the selected camera with synchronized game audio;
7. pause/resume/stop safely;
8. retrieve and play the resulting file;
9. restart Beat Saber and find camera, motion, script, preview, and recording preferences restored without routine recalibration.

If any part of this flow is not working on a real Quest, do not call this stage complete.

## Measurement-driven Quest 2 pass

Compare:

A. baseline Beat Saber;
B. spectator render only;
C. preview visible;
D. hardware encode only;
E. local A/V recording;
F. preview plus local recording;
G. heavy Chroma/Noodle/particle maps;
H. concurrent hardware video decode from another mod where practical.

Capture:

- HMD FPS/frame time;
- requested/output recording FPS;
- dropped spectator frames;
- encoder queue/latency;
- file throughput;
- A/V drift;
- memory;
- CPU/GPU indicators;
- thermal behavior during sustained sessions.

Verify bounded queues, no burst catch-up rendering, no gameplay-thread storage work, and gameplay priority under overload.

## Reliability pass

Test at minimum:

- 20 sequential recordings;
- 20+ minute recording;
- menu → map → menu;
- repeated map transitions;
- preview show/hide/move/reset;
- anchored-float toggle, motion bounds, head-jitter rejection, recenter, and base-placement preservation;
- Camera2 movement scripts controlling position/rotation/FOV across play, pause, restart, practice/seek where supported, malformed input, and script completion;
- duplicate rapid controls;
- recording pause independent of gameplay pause;
- low storage and write failure;
- encoder initialization failure;
- Beat Saber shutdown during recording;
- Quest recenter;
- full game restart and automatic restoration.

Audit ownership, scene lifetime, workers, locks, encoder surface, audio, muxing, config migration, reset paths, and memory growth. Simplify broken lifecycle models rather than stacking conditional patches.

Update README documentation to describe only behavior verified on device. Report host tests, Quest tests, media validation, performance results, known limitations, and the exact next phase.

Stop for explicit acceptance before remote-stream or companion work.
