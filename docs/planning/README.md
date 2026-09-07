# SaberStage Planning Prompt Set

These files describe **SaberStage**, a clean-room native Quest Beat Saber camera, recording, and broadcast system with a cross-platform desktop companion.

Use the files in one long-running Codex conversation, in order. Treat each prompt as a gated phase: complete, test, review, and explicitly approve it before continuing.

Start with:

`00_PROJECT_CHARTER.md`

## Product sequence

### Foundation

1. Repository and architecture
2. Minimal Quest mod scaffold

### First working release — useful entirely on Quest

3. Camera2-familiar independent camera
4. Movable in-game preview and camera UX
5. GPU-native hardware encoding
6. Synchronized game audio and local recording
7. Recording controls
8. Quest 2 performance and first-release hardening

The first working release is not complete until users can position the camera, preview it, record real gameplay locally, retrieve a valid recording, restart Beat Saber, and find their settings restored.

### Companion and remote viewing

9. Versioned remote-stream protocol
10. Wi-Fi streaming
11. Avalonia companion for Windows, macOS, and Linux, including viewing and recording
12. USB streaming
13. Direct TV/receiver casting feasibility
14. OBS output from the companion

The companion receives native Quest gameplay. It does not run Beat Saber, require PCVR, or depend on Meta's Windows software.

### Full broadcast production

15. Lightweight Quest-native broadcast compositor and scenes
16. Direct Quest livestreaming
17. In-game Twitch/YouTube chat panel
18. Discord feasibility study
19. Production Discord integration only for capabilities proven supported
20. Full-system Quest 2 performance and beta hardening

## Product principles

- Match Camera2's familiar user-visible behavior, concepts, and workflow where practical without copying its implementation.
- Quest 2 is the baseline.
- Use GPU-native hardware encoding.
- Never use unbounded queues.
- Gameplay wins over capture quality.
- Reuse one selected camera/compositor and encoded-media pipeline across compatible outputs.
- Local recording works without a PC.
- Direct livestreaming works without a PC.
- The optional desktop companion remains cross-platform and independent of Meta's Windows runtime.
- TV output uses the selected SaberStage view rather than the HMD mirror when technically possible.
- Chat belongs in an HMD-only panel by default.
- Discord integration uses supported/public mechanisms only; never patch or modify Discord.
- Camera, preview, output, and broadcast configuration should be set once and restore automatically.
- Design the menu and settings architecture for the complete product from the start without exposing fake or nonfunctional controls.
