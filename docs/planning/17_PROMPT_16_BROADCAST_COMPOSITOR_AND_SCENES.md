# Prompt 16 — Implement the lightweight Quest-native broadcast compositor and scenes

Implement a Beat-Saber-specific broadcast-scene layer using the menu and settings architecture planned from the beginning of SaberStage.

Do not attempt to recreate full OBS.

## Principle

The compositor sits before the encoder:

```text
Game Camera
+ broadcast-only sources
→ composed GPU frame
→ hardware encoder
```

Keep this GPU-native.

Avoid CPU readback.

## Initial source model

Implement architecture for:

- Game Camera — required;
- Text;
- Image/Logo;
- Song Metadata;
- Score/Combo/Energy where stable public game state is available;
- Stream Status.

Future/optional sources:

- viewer chat overlay;
- alerts.

Each source should support only appropriate controls such as:

- enabled;
- anchor/position;
- size;
- opacity;
- Z-order;
- simple visibility rules.

## Broadcast scenes

Implement saved scenes such as:

- Gameplay;
- Starting Soon;
- BRB;
- Results;
- Minimal.

Scene changes must not recreate the encoder.

The UI should now expose the production controls anticipated by the earlier architecture. It must feel like a coherent expansion of the camera/recording product, not a separate OBS clone bolted onto the mod.

The normal HMD view must not automatically show broadcast overlays.

## Persistence

Scenes restore automatically on game launch.

Add:

- Reset Current Broadcast Scene
- Reset All Broadcast Scenes

## Performance

Do not continuously render multiple full gameplay cameras just because several camera presets/scenes exist.

Measure compositor cost on Quest 2.
