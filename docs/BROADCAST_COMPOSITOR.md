# Broadcast compositor

The compositor owns the single final image before encoding. Stage 1 contains only the selected game camera. Later `BroadcastScene` documents can reference typed, bounded sources such as game camera, integrated avatar, song metadata, score/combo/energy, static text/image, status, and optional viewer-facing chat.

Scenes (`Gameplay`, `Starting Soon`, `BRB`, `Results`, `Minimal`, `Cinematic`) switch source visibility/layout without restarting the encoder. Each source has stable identity, transform/layout, visibility policy, update rate, and resource budget. HMD-only preview/chat panels are excluded by culling contract. No browser-source engine, arbitrary plugin ABI, or OBS port is planned.

Rendering occurs only on `FrameScheduler` demand. Expensive sources can have independent update rates, but composition is game-thread/render-thread coordinated. Scene validation rejects missing/cyclic/oversized inputs and falls back to `Gameplay`. A slow overlay cannot block the camera or encoder.
