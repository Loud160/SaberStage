# Recording pipeline

## Selectable hardware recording implementation

```text
Persistent SaberStage Primary camera -> Hollywood GPU/MediaCodec H.264 -> .partial.h264
persistent HMD-positioned AudioListener -> lock-free PCM ring -> audio worker -> .partial.wav
                                                Hollywood FFmpeg mux -> .partial.mp4 -> final .mp4
```

The user explicitly approved Hollywood or a locally built FFmpeg path. The first usable implementation therefore depends on Hollywood 1.2.2, shares Hollywood's encoder texture with SaberStage's preview, and keeps camera ownership, recording state, filenames, output folder, and UI in SaberStage. By default, Start begins capture immediately and one persistent encoder/audio session continues through menus, loading transitions, `GameCore`, and results until Stop & Save. The spectator camera is retargeted when Beat Saber replaces its main camera, while a persistent audio listener follows the current HMD camera and temporarily owns audio-listener responsibility across scene changes. Optional Gameplay Only mode instead arms in menus and finalizes after gameplay ends. Failed finalization preserves the partial H.264/WAV files.

Output is `/sdcard/Oculus/VideoShots`, the same folder used by the Quest's built-in video recorder. SaberStage keeps its `SaberStage_` filename prefix so its captures remain identifiable. `scripts/quest_tool.py pull-recordings` retrieves only completed SaberStage MP4 files into a new timestamped desktop folder without deleting headset files or copying unrelated system recordings.

Hollywood's encoded callback now copies into a bounded queue and a worker owns file writes. The Unity audio callback only copies into a preallocated four-second single-producer/single-consumer ring; PCM conversion, WAV writes, and livestream fan-out run on its worker. This removes per-sample disk writes from Unity's real-time audio thread and prevents storage stalls from running inside Hollywood's encoder-drain callback.

The alternate `Direct FFmpeg (Hardware)` backend uses SaberStage's private FFmpeg 9 runtime and Android MediaCodec surface input:

```text
Primary camera RenderTexture -> Unity render-thread EGL bridge -> MediaCodec input Surface
    -> FFmpeg h264_mediacodec -> bounded encoded-packet fan-out -> .partial.h264
game audio -> same bounded PCM worker ---------------------------------------> .partial.wav
private FFmpeg H.264 demux + AAC encode + MP4 mux -> .partial.mp4 -> final .mp4
```

Direct mode exposes 720p/1080p/1440p, 30/60 FPS, target and peak bitrate, CBR/VBR, hardware complexity preference, H.264 profile/level, keyframe interval, and AAC bitrate. The build enables no software video encoder; unsupported hardware settings fail the start instead of stealing gameplay CPU. The two FFmpeg installations use different SONAMEs and private symbol namespaces so Hollywood remains independently selectable.

EGL setup is failure-contained: shader or surface initialization restores Unity's prior draw/read surfaces before reporting failure. The bridge validates the current EGL context/config, including `EGL_RECORDABLE_ANDROID`, before using Unity's native texture as a GL texture, and records stage-specific EGL/GL and encoder counters. The present implementation is OpenGL ES/EGL-specific; a Vulkan-native bridge remains future work. If the bridge fails before presenting or submitting any input, a local-only session falls back to Hollywood while the raw stream is still guaranteed empty; an active live session fails explicitly because changing encoder parameter sets midstream would be unsafe. Stop order first drains the encoder while its input surface is valid, then destroys the EGL surface on the render thread, preventing codec/surface use-after-free during Stop & Save.

Both paths still require fresh Quest validation for video content, color/orientation, audio, duration, sync, sustained gameplay performance, 1440p codec acceptance, and abnormal shutdown behavior.

## Third-person camera multisampling

The Primary camera profile exposes `Off`, `2x`, and `4x` MSAA. This setting belongs only to SaberStage's third-person camera: it affects the floor/movable preview and the image sent to either recording backend, but never changes Beat Saber's headset render target or overlaps a graphics-settings mod. `Off` is the Quest 2-safe default.

MediaCodec and the Direct FFmpeg bridge require a normal single-sample encoder texture. When camera MSAA is enabled, the spectator camera therefore renders into a private multisampled target and resolves it into the backend-owned encoder texture after `OnPostRender`. Preview consumers continue to read the resolved encoder texture. Changing the setting while an output exists recreates only this owned intermediate target; allocation failure is logged and falls back safely to one sample.

## Direct packet fan-out

```text
scheduled broadcast render
-> GPU bridge -> MediaCodec AVC input Surface
-> timestamped video packets -----+
Unity audio tap -> AAC packets ----+-> CaptureTimeline -> packet fan-out
                                         `-> MP4/network sinks
```

The direct packet path is GPU-native and hardware-only. One encode currently feeds local recording and direct livestreaming; the companion remains a later sink. Conservative Quest 2 defaults and higher modes remain gated by measured capability/performance tiers. Software H.264 fallback remains out of scope.

The frame scheduler uses monotonic time and a fixed cadence independent of HMD refresh. It presents at most one due spectator frame per game frame. If Unity misses a capture deadline, the next submitted image retains its real presentation slot and the MP4 holds the prior picture over the gap; missed time is never compressed into consecutive frame numbers because doing so makes video progressively drift from continuously sampled game audio. Diagnostics report these skipped timeline deadlines separately from MediaCodec queue drops. Encoder drain tracks a bounded number of submitted surface frames and owns codec-output release.

Hollywood's callback does not expose MediaCodec packet timestamps. SaberStage now records the monotonic presentation deadline of each completed Hollywood spectator render and gives that one-to-one timing table to the private finalizer. A missed Unity/Hollywood render therefore remains a duration gap instead of renumbering the surviving frames into a shorter video track. This brings Hollywood's long-session A/V behavior in line with the Direct backend's timestamp-preserving rule while retaining separate diagnostics for skipped presentation deadlines.

The first packet actually emitted by the hardware encoder is normalized to the start of the saved video. Frames accepted but never emitted during encoder startup do not become a permanent picture delay; later deadline gaps keep their original duration, so startup normalization cannot reintroduce cumulative audio drift.

The GLES bridge samples Unity's RenderTexture in its native OpenGL orientation. It does not apply a second Y inversion; that redundant flip was the cause of upside-down Direct FFmpeg recordings.

The Unity mix tap copies interleaved PCM into a preallocated SPSC ring on the audio callback. It performs no allocation, logging, file I/O, or Unity object work there. The audio worker converts and writes PCM to the temporary WAV and fans it to the live sink, where AAC-LC encoding runs on the network worker. The hardware surface receives explicit monotonic presentation timestamps. Recording pause closes a video segment and disables the audio tap so paused duration is omitted from both temporary tracks; game pause alone does not.

The same audio worker can mix the persistent Quest microphone and local Chat
TTS into a requested local recording without creating a second clock or writer.
Microphone DSP and TTS resampling preserve the worker's frame count; underflow is
silence. Game-sound mute changes only the game source. Capture formats, bounded
ownership, routing, and Android permission behavior are documented in
[Chat TTS and Quest microphone audio](CHAT_TTS_AND_MICROPHONE_AUDIO.md).

The MP4 sink starts only after required output formats are known, writes samples in valid order, and finalizes on normal stop. It writes a unique sanitized `.partial.mp4` in the recording directory, then safely renames after successful stop. It never overwrites unrelated files. Low storage, write failure, or codec failure stops only capture, retains diagnostics/partial data when useful, and returns gameplay to `Idle/Failed` safely.

Acceptance requires desktop `ffprobe` validation, decoded frame inspection proving the independent camera, audible game audio, duration/PTS checks, and repeated long Quest 2 sessions. Android contracts: [MediaCodec](https://developer.android.com/reference/android/media/MediaCodec), [MediaMuxer](https://developer.android.com/reference/android/media/MediaMuxer), and [playback capture limitations](https://developer.android.com/media/platform/av-capture).
