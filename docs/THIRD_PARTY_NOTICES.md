# Third-party notices

## Discord interface symbol

The Discord glyph shown beside the **Discord Live Steam** action is provided by
[Icons8](https://icons8.com/icon/25627/discord) and is used only to identify the
Discord destination. Discord and the Discord logo are trademarks of Discord
Inc.; SaberStage is not endorsed by or affiliated with Discord. The glyph is
kept white, undistorted, and adjacent to the visible Discord name in accordance
with [Discord's brand guidance](https://discord.com/branding).

## KittenTTS Nano and sherpa-onnx

SaberStage uses [KittenTTS Nano English v0.2](https://github.com/KittenML/KittenTTS)
through [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx) 1.13.7. The exact
model archive, C API header, Android runtime archive, prepared English-only
model ZIP, and private-name runtime-library hashes are recorded in
`dependencies/kitten-tts.json`. The model ZIP is embedded in
`libsaberstage.so`; the ARM64 sherpa-onnx and ONNX Runtime libraries are shipped
as private runtime payloads and loaded only while Chat TTS is enabled.

KittenTTS model files and sherpa-onnx are distributed under Apache License 2.0.
The ONNX Runtime binary included in sherpa-onnx's pinned Android distribution is
distributed under the MIT License. Their upstream licenses and notices are
preserved in the pinned source/model archives. SaberStage does not install an
Android speech service, invoke cloud speech, or share these private libraries
with other mods.

## Native Logger Quest

SaberStage statically builds [Native Logger Quest](https://github.com/Loud160/NativeLoggerQuest)
version `1.0.0` from immutable revision
`b83b21bbafa1b33c3f36640ded3c2933b5e88085`. The expected official archive
SHA-256 is recorded in `dependencies/native-logger.json`. It provides the
private asynchronous file/logcat backend and the target-local beatsaber-hook
abort bridge; it does not install a shared logger or replace Paper2 used by
other mods.

Native Logger Quest is distributed under GPL-3.0-only with its preserved
GPLv3 section 7 terms. Its complete license, additional terms, and notice are
available in the upstream repository at the pinned revision. SaberStage's own
first-party source is separately distributed under the project license linked
from the repository root.

## BeatSaberPlus / ChatPlex UI reference

The standalone rich-chat controls and request manager use the supported UI
workflow/layout of [BeatSaberPlus](https://github.com/hardcpp/BeatSaberPlus)
revision `632addd001d29a7beee14a52eadf87ccb2792a3a` as their reference. Its linked
ChatPlexSDK revision is `9c209664b5a0c64f9c9c1e7e152b0ca6d1dab7ce`.
SaberStage implements the Quest panels, transport, bounded renderer and durable
request queue in its native code; it does not bundle the PC plugin or require
the PC application. The exact reference paths and standalone differences are
recorded in the [chat plan](planning/TWITCH_RICH_CHAT_AND_SONG_REQUESTS.md).

MIT License

Copyright (c) 2021 HardCPP (BeatSaberPlus)

Copyright (c) 2023 HardCPP (ChatPlexSDK)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## Camera2

SaberStage's menu screen topology and left camera-list presentation are adapted from [Camera2](https://github.com/kinsi55/CS_BeatSaber_Camera2), inspected at commit `ee82cfdccf432a00692fad46d32489768942571a`.

MIT License

Copyright (c) 2021 Kinsi

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
