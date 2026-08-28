# Clean-room provenance log

SaberStage is independently implemented except where the user explicitly requested Camera2's menu structure as the direct layout reference. Camera2's MIT notice is preserved in `docs/THIRD_PARTY_NOTICES.md`. Other references below were consulted for public APIs, tool compatibility, file formats, and observable user behavior.

| Reference | What was learned | Permitted use here |
|---|---|---|
| [Camera2](https://github.com/kinsi55/CS_BeatSaber_Camera2), inspected at public commit `ee82cfdccf432a00692fad46d32489768942571a` | Menu screen topology and left camera-list BSML structure; placement preview behavior, FOV/render-scale concepts, smoothing terminology, and public `syncToSong`/`loop`/keyframe movement-script contract | Menu layout adapted under Camera2's MIT license; script compatibility independently implemented from the public data contract; no camera/runtime algorithm copying |
| [Qounters-](https://github.com/Metalit/bsq-qounters-minus), inspected at public commit `46fee406e5034c911b50f728e133ee1d5a045bbc` | Observable Quest-native use of full settings view controllers and explicit panel sizing | Quest panel-sizing reference only; no code/algorithm copying |
| [LIV documentation](https://help.liv.tv/hc/en-us) | Observable notion of a camera that reacts smoothly to player/head direction | Behavioral inspiration for optional anchored float only |
| [ReeCamera](https://github.com/Reezonate/ReeCamera) | Alternative PC recording/streaming camera feature set | Product comparison only |
| [CameraUtils-Quest](https://github.com/Reezonate/CameraUtils-Quest) | Quest camera registration and HMD/desktop visibility-layer concepts | Evaluate as an optional inter-mod camera/culling dependency later |
| [MRCPlus](https://github.com/Raemien/MRCPlus) | Quest spectator camera via Meta MRC requires a PC/receiver and setup | Rejected as SaberStage's core architecture |
| [HollywoodQuest](https://github.com/Fernthedev/HollywoodQuest), local reference at `f72de25de2931b979864322f88eb259c3f47783d` | Existing Unity camera/audio recording and post-mux feasibility on Quest; standard Unity `RenderTexture` and mono spectator-camera API availability | Feasibility/API confirmation only; no code copied and not a required dependency |
| [Replay](https://github.com/Metalit/Replay), local reference at `c3c2eac70e9fa45a25c0b765964a62dc46a3a4ac` | Offline replay/video rendering, user-facing FOV/resolution/framerate controls, and evidence that generated target bindings expose the required camera components | Product/API/test reference only; no code copied |
| [VRM Qavatars](https://github.com/BSQ-VRM/VRM-Qavatars) | Quest VRM avatar feasibility, tracking/calibration/performance concerns | Avatar requirements and risk reference only |
| [Scotland2](https://github.com/sc2ad/scotland2) | Mod phases and deterministic dependency loading | Public loader contract |
| [QuestPackageManager/QPM.CLI](https://github.com/QuestPackageManager/QPM.CLI) | Current QPM CLI; legacy qpm-rust is deprecated | Build/package tool |
| [QuestPatcher/QMOD](https://github.com/Lauriethefish/QuestPatcher) | QMOD package contract and schema source | Package metadata contract |
| [BSMG Quest modding guide](https://bsmg.wiki/quest-modding.html) | Community-supported modding workflow/version sensitivity | Tooling context, not a guarantee of target compatibility |
| [Android MediaCodec](https://developer.android.com/reference/android/media/MediaCodec) | AVC hardware codec discovery, input Surface, output access-unit draining | Platform API design |
| [Android MediaMuxer](https://developer.android.com/reference/android/media/MediaMuxer) | MP4 track/mux lifecycle and supported formats | Platform API design |
| [Android playback capture](https://developer.android.com/media/platform/av-capture) | MediaProjection rules for cross-app audio capture | Shows why an in-process Unity audio tap is preferred |
| [Unity `OnAudioFilterRead`](https://docs.unity3d.com/ScriptReference/MonoBehaviour.OnAudioFilterRead.html) | Audio callback is on the audio thread and must not call normal Unity APIs | Threading constraint |
| [Twitch video broadcast](https://dev.twitch.tv/docs/video-broadcast/) and [chat](https://dev.twitch.tv/docs/chat/) | RTMP ingest and supported EventSub/API chat direction | Provider contracts |
| [YouTube RTMPS ingestion](https://developers.google.com/youtube/v3/live/guides/rtmps-ingestion) and [live chat](https://developers.google.com/youtube/v3/live/docs/liveChatMessages) | RTMPS ingest and polling/streaming chat APIs | Provider contracts |
| [Discord Social SDK](https://discord.com/developers/docs/social-sdk/index.html), [mobile account linking](https://docs.discord.com/developers/discord-social-sdk/development-guides/account-linking-on-mobile), and [OAuth2](https://discord.com/developers/docs/topics/oauth2) | Supported Android account link, presence, message/voice capabilities and restrictions; no public external video-source API found | Presence and approved communication exploration only |
| [Android Keystore](https://developer.android.com/privacy-and-security/keystore) | Nonexportable app-bound cryptographic keys | Secret-storage design |
| [Unity XR input/tracking origin](https://docs.unity3d.com/Manual/xr_input.html) | Origin modes, recenter and tracking-origin events | Recenter model |
| [Google Cast overview](https://developers.google.com/cast/docs/overview) | Sender/receiver model and custom Web/Android TV receivers | Later receiver feasibility |

The local BigScreen repository is actively being changed in another task. SaberStage does not copy it and performs no edits, builds, restores, or Git operations there. Previously observed local environment facts (Beat Saber package target and installed QPM/NDK family) are treated as machine-specific evidence and revalidated inside SaberStage.

## Dependency review

| Dependency | Status/reason | License/maintenance/Quest/cross-platform/packet access |
|---|---|---|
| Scotland2 | Required loader | MIT; active Quest-specific loader; no companion impact; not a media layer |
| beatsaber-hook | Required native/IL2CPP support | MIT; active for the target family; Quest-specific; no media packet role |
| bs-cordl | Required generated Beat Saber/Unity bindings | Community maintained and target-version-specific; Quest-only build concern; no packet role |
| BSML | Required only for the Beat Saber menu | MIT/community maintained; Quest UI dependency; no companion impact; no packet role |
| custom-types | Required for managed Unity component types used by BSML/UI | MIT/community maintained; Quest-specific; no packet role |
| RapidJSON | Required for small configuration files | MIT; mature header-only C++; portable; no media role |
| Android MediaCodec/MediaMuxer | Required platform APIs for Stage 1 media | Android platform; Quest-native; companion unaffected; MediaCodec exposes encoded output before sinks, satisfying fan-out |
| HollywoodQuest/FFmpeg | Optional/rejected as mandatory core | Public project uses FFmpeg-family components with multiple licenses/build obligations; Quest proven but heavier and file-oriented; its current public surface does not establish the required packet fan-out contract |
| CameraUtils-Quest | Optional future interoperability | Public repository has no clearly declared license in the inspected landing page, so it cannot be adopted until licensing is confirmed; Quest-only; no encoded packets |
| mbedTLS/BoringSSL wrapper or platform TLS | Stage 2/3 selection deferred | Must be maintained, ARM64-capable, license-compatible, and expose bounded nonblocking I/O; networking only |
| Avalonia/.NET | Required companion stack in Stage 2 | MIT and cross-platform; desktop-only; consumes protocol packets and must not import Meta runtime |

No final SaberStage license has been selected.
