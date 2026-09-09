# Future Horizon OS virtual camera publisher integration

## Status

**Deferred concept only. No implementation is currently planned.**

This document records a possible future replacement for, or complement to,
the SaberStage Helper Discord screen-source path. It is based on Meta's
experimental Virtual Camera Publisher API as documented on September 9, 2026.
The API is currently version `0.0.1`, is supplied on an as-is basis, and is new
enough that its behavior, packaging, permissions, and compatibility may change
substantially before SaberStage should depend on it.

Before beginning any implementation, re-read the current Meta documentation,
inspect the latest package source and license, and repeat every requirement and
compatibility test in this document. Do not assume the API described here is
unchanged.

## Concept

SaberStage already owns a configurable third-person Unity camera. Instead of
encoding that camera, transporting it to a companion APK, decoding it, and
having Discord capture the companion's visible Android activity, SaberStage
could publish the camera to Horizon OS as an application-defined virtual
camera.

The intended pipeline would be:

```text
SaberStage third-person Unity camera
    -> Horizon OS virtual camera publisher
    -> Meta system camera selection
    -> recording, casting, livestreaming, or a compatible third-party app
```

This does **not** intercept or replace the normal headset compositor capture.
It registers an additional camera that the user may select through Horizon OS.
Meta's current sample exposes registered cameras under:

```text
Meta button -> Camera -> Camera Settings -> Camera View
```

## Confirmed Meta requirements as of September 9, 2026

Meta's current documentation states that:

- Virtual Camera Publisher requires Horizon OS v81 or newer.
- The publishing application must be a Unity application using Android API
  level 32 or newer.
- A publishing application may register up to three cameras simultaneously.
- The maximum published frame rate is 60 FPS.
- Meta recommends rates that divide evenly into the application's render rate;
  for a 90 Hz game, 30 or 45 FPS are the documented examples.
- Registration supplies a camera name, stable numeric ID, width, height, frame
  rate, Unity `Camera`, and optional thumbnail.
- The manager disables the registered Unity camera until a consumer requests
  it and throttles rendering to the registered frame rate.
- Capture-start and capture-stop observers are available, as is an
  `IsCapturing` query.
- Unregistering a camera gracefully ends an active capture. Registered cameras
  are removed automatically when the application exits.
- The feature does not work through Quest Link and must be tested in a complete
  APK running on a headset.

The publishing application's Android manifest currently requires:

```xml
<manifest
    xmlns:android="http://schemas.android.com/apk/res/android"
    xmlns:horizonos="http://schemas.horizonos/sdk">

    <uses-permission
        android:name="horizonos.permission.CREATE_VIRTUAL_CAMERA" />

    <horizonos:uses-horizonos-sdk
        horizonos:minSdkVersion="81"
        horizonos:targetSdkVersion="81" />
</manifest>
```

Meta describes `CREATE_VIRTUAL_CAMERA` as a normal permission that does not
require a user-consent dialog.

An optional thumbnail requires Meta's thumbnail content provider in the
application manifest. It should be omitted from the first proof of concept so
the initial test changes as little of Beat Saber's manifest and lifecycle as
possible.

## Discord compatibility is not yet established

Meta states that published cameras are available to system recording, casting,
and livestreaming. Meta also states that third-party applications can consume
them through Android Camera2, but the **consuming application** requires:

```text
horizonos.permission.APP_DEFINED_CAMERA
```

This creates two separate questions:

1. Can Meta's system recording and casting UI select and use a camera published
   from a modded Beat Saber process?
2. Does the Quest Discord build enumerate Horizon application-defined cameras,
   either through the system picker or Camera2, and does its manifest contain
   the required consumer permission?

SaberStage cannot add a permission to Discord. If Discord does not support this
API, a successfully published SaberStage camera still will not appear in
Discord's virtual-camera selector. This must be proven on the headset before
the existing helper path is removed or redesigned.

## Proposed SaberStage design

The first integration should be isolated behind a compile-time experimental
feature and should coexist with the current helper path.

### Camera registration

- Register one camera named for its function, such as `Third Person`. Meta's
  picker automatically shows the application name, and its guidance says not
  to repeat the application name in the camera name.
- Use a stable camera ID stored as a SaberStage constant.
- Start with one conservative mode, preferably 1920x1080 at 30 FPS, before
  exposing multiple resolution or frame-rate choices.
- Mirror the transform, field of view, culling mask, post-processing choices,
  and motion-script result of SaberStage's Primary camera.
- Do not alter the headset cameras or the image seen by the player.
- Do not register preview cameras, UI cameras, or temporary calibration views.

### Rendering and lifetime

- Register only after SaberStage's camera is fully initialized.
- Call the publisher update from the existing Unity-frame lifecycle point, not
  from a socket, encoder, or worker thread.
- Use capture observer callbacks or `IsCapturing` to avoid any additional
  per-frame camera work while the virtual camera is not being consumed.
- Keep registration, capture callbacks, scene transitions, graphics-resource
  recreation, and unregistration on the Unity main thread.
- Gracefully unregister before SaberStage camera destruction and during mod
  shutdown.
- Treat scene changes and graphics-context loss as normal lifecycle events;
  never retain a destroyed Unity `Camera`, texture, or Java object reference.

The Meta manager currently requires a Unity `Camera`, not merely an existing
encoded stream. Its ownership behavior must be inspected before reusing
SaberStage's existing camera directly. If it changes `Camera.enabled` or render
targets in a way that conflicts with SaberStage, use a dedicated publisher
camera that follows the existing Primary camera rather than allowing two
systems to control the same component.

### Configuration and UI

The proof of concept should not add permanent user-facing configuration. Use a
single diagnostic switch or developer setting until system and Discord
compatibility are proven.

If the feature becomes stable, possible production controls are:

- **Publish Horizon virtual camera**
- published resolution
- published frame rate
- current state: unavailable, registered, idle, or being captured
- a shortcut explaining where to select the camera in Horizon OS

The UI must identify the minimum Horizon OS version and must not advertise
Discord support unless it has been verified against the installed Discord
release.

## Beat Saber mod integration risks

SaberStage is a native IL2CPP mod loaded into an existing Beat Saber APK, not a
Unity project that can import Meta's package normally. The sample's direct
Unity-package workflow therefore cannot be assumed to work unchanged.

Before implementation, determine:

- Beat Saber's Unity version and whether the current Meta package assemblies
  can load on it.
- Beat Saber's actual Android API level and manifest compatibility.
- Whether MBF/QMOD manifest requirements can safely add the Horizon namespace,
  publisher permission, and Horizon SDK declaration.
- Whether the package calls a stable Android service through Java/JNI or relies
  on Unity 6 APIs that are absent from Beat Saber.
- Whether the integration can use the published package under its Oculus SDK
  license or whether only a bridge to the platform API may be redistributed.
- Whether required managed types can be injected safely into Beat Saber's
  IL2CPP environment, or whether a small Java/JNI bridge is more reliable.
- Whether registration remains valid across Beat Saber scene transitions and
  Android pause/resume events.

Do not patch undocumented Horizon services, replace compositor textures, hook
system casting internals, or modify the player's eye buffers as a workaround.
If the supported publisher API cannot be integrated safely, retain the helper
architecture.

## Audio boundary

The published-camera documentation describes video camera publication. It does
not establish a way for SaberStage to attach its custom mixed PCM stream to the
virtual camera.

The proof of concept must separately verify:

- whether Meta recording/casting captures Beat Saber's game audio;
- whether Discord captures application audio with the selected virtual camera;
- whether SaberStage's Quest microphone and Chat TTS mix are included;
- whether enabling the current SaberStage audio pipeline causes duplicated or
  unsynchronized audio;
- whether pause/AFK behavior can provide silent audio while replacing video.

The existing helper explicitly receives SaberStage's mixed game, microphone,
and TTS audio. The helper must remain available until the virtual-camera path
matches the required audio behavior or a separate supported audio route exists.

## Performance questions

Although this path should remove SaberStage's helper transport and decode
stages, it is not automatically free. Horizon OS may still request a separately
rendered camera and perform its own color conversion and encoding.

Measure all of the following on Quest 2 and Quest 3:

- headset frame rate and frame-time percentiles with publishing unavailable,
  registered but idle, previewed, recorded, cast, and streamed;
- CPU, GPU, and memory impact;
- additional Unity camera render cost;
- texture allocation and copy behavior;
- output frame pacing at 30, 45, and 60 FPS where supported;
- latency compared with the current helper path;
- behavior with bloom, post-processing, avatars on layer 3, UI exclusion, and
  SaberStage motion scripts;
- behavior while Twitch streaming or recording simultaneously.

Registration while idle must have negligible ongoing overhead. Capture must
not block the Unity thread or add another SaberStage encoder when Horizon OS is
already responsible for encoding the published camera.

## Future proof-of-concept sequence

1. **Refresh the research.** Record the headset OS version, Discord version,
   package version, official API requirements, source revision, and license.
2. **Validate Meta's sample unchanged.** Install the official sample APK and
   confirm that its cameras appear in the Horizon camera picker and work for
   system recording and casting.
3. **Test Discord against the official sample.** Check whether Discord lists a
   published sample camera and can keep consuming it after focus returns to the
   VR application. Stop here if Discord cannot consume it.
4. **Inspect the integration boundary.** Identify the package's Unity,
   Android, Java/JNI, graphics, and manifest dependencies without changing
   SaberStage.
5. **Create an isolated SaberStage prototype.** Publish one conservative
   third-person camera. Do not remove or alter the helper implementation.
6. **Verify lifecycle.** Exercise game startup, menu-to-map transitions,
   pause/resume, camera recreation, capture start/stop, Beat Saber exit, and
   unexpected consumer disconnects.
7. **Verify media behavior.** Confirm the selected third-person view, aspect
   ratio, post-processing, layer visibility, audio, AFK behavior, and latency.
8. **Benchmark both paths.** Compare the virtual camera with the existing
   Direct encoder plus helper decoder under the same scene and capture settings.
9. **Make a product decision.** Keep both paths, make virtual camera preferred
   with helper fallback, or defer it again based on measured compatibility and
   reliability.

## Minimum acceptance criteria

The feature is not ready to replace the helper unless all of these are true:

- The camera registers without destabilizing Beat Saber.
- It appears consistently after cold start and across scene transitions.
- Horizon recording and casting show the correct SaberStage third-person view.
- The tested Quest Discord build lists and captures it reliably regardless of
  whether Discord or Beat Saber was opened first.
- Returning focus to Beat Saber does not stop Discord consumption.
- Game audio, Quest microphone, and Chat TTS meet SaberStage's stream-audio
  requirements without duplication or unacceptable drift.
- Pausing shows the configured AFK image and applies the expected mute state.
- Stopping capture releases all graphics and platform resources.
- An idle registered camera has negligible performance cost.
- Active capture performs better than, or offers a clear reliability advantage
  over, the helper pipeline on Quest 2 and Quest 3.
- Failure leaves recording, Twitch streaming, Beat Saber, and the existing
  helper fallback usable.

## References

- Meta documentation: <https://developers.meta.com/horizon/documentation/unity/unity-sample-virtual-camera-publisher/>
- Meta sample repository: <https://github.com/oculus-samples/Unity-VirtualCameraPublisher>
- Package onboarding documentation: <https://github.com/oculus-samples/Unity-VirtualCameraPublisher/blob/dev/com.meta.xr.virtualcamerapublisher/README.md>
- Current SaberStage helper design: [`../DISCORD_SCREEN_SOURCE.md`](../DISCORD_SCREEN_SOURCE.md)
