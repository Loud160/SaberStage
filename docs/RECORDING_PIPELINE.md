# Recording pipeline

## Current Stage 1 implementation

```text
Persistent SaberStage Primary camera -> Hollywood GPU/MediaCodec H.264 -> .partial.h264
persistent HMD-positioned AudioListener -> Hollywood PCM capture ----------> .partial.wav
                                                Hollywood FFmpeg mux -> .partial.mp4 -> final .mp4
```

The user explicitly approved Hollywood or a locally built FFmpeg path. The first usable implementation therefore depends on Hollywood 1.2.2, shares Hollywood's encoder texture with SaberStage's preview, and keeps camera ownership, recording state, filenames, output folder, and UI in SaberStage. By default, Start begins capture immediately and one persistent encoder/audio session continues through menus, loading transitions, `GameCore`, and results until Stop & Save. The spectator camera is retargeted when Beat Saber replaces its main camera, while a persistent audio listener follows the current HMD camera and temporarily owns audio-listener responsibility across scene changes. Optional Gameplay Only mode instead arms in menus and finalizes after gameplay ends. Failed finalization preserves the partial H.264/WAV files.

Output is `/sdcard/ModData/com.beatgames.beatsaber/Mods/SaberStage/Recordings`. `scripts/quest_tool.py pull-recordings` retrieves it into a new timestamped desktop folder without deleting headset files.

This path still requires Quest validation for video content, audio, duration, sync, sustained performance, and abnormal shutdown behavior.

## Future direct packet pipeline

```text
scheduled broadcast render
-> GPU bridge -> MediaCodec AVC input Surface
-> timestamped video packets -----+
Unity audio tap -> AAC packets ----+-> CaptureTimeline -> packet fan-out
                                         `-> MP4/network sinks
```

The later direct packet path remains GPU-native and hardware-only. It is needed when timestamped encoded packets must fan out simultaneously to local recording, the companion, and livestream sinks. Conservative Quest 2 defaults and higher modes remain gated by measured capability/performance tiers. Software H.264 fallback remains out of scope.

The frame scheduler uses monotonic time and a fixed rational cadence independent of HMD refresh. It presents at most one due spectator frame per game frame and drops overdue output frames rather than catching up. Encoder drain has a bounded queue and owns codec-output release.

The Unity mix tap copies interleaved PCM into a preallocated SPSC ring on the audio callback. It performs no allocation, logging, file I/O, or Unity object work there. An audio worker timestamps and encodes AAC-LC. Video and audio PTS are mapped through one capture clock. Recording pause closes a timeline segment so paused duration is omitted from both tracks; game pause alone does not.

The MP4 sink starts only after required output formats are known, writes samples in valid order, and finalizes on normal stop. It writes a unique sanitized `.partial.mp4` in the recording directory, then safely renames after successful stop. It never overwrites unrelated files. Low storage, write failure, or codec failure stops only capture, retains diagnostics/partial data when useful, and returns gameplay to `Idle/Failed` safely.

Acceptance requires desktop `ffprobe` validation, decoded frame inspection proving the independent camera, audible game audio, duration/PTS checks, and repeated long Quest 2 sessions. Android contracts: [MediaCodec](https://developer.android.com/reference/android/media/MediaCodec), [MediaMuxer](https://developer.android.com/reference/android/media/MediaMuxer), and [playback capture limitations](https://developer.android.com/media/platform/av-capture).
