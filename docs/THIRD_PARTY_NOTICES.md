# Third-party notices

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
available in the upstream repository at the pinned revision. This notice does
not select a final license for SaberStage's remaining first-party source.

## Camera2

SaberStage's menu screen topology and left camera-list presentation are adapted from [Camera2](https://github.com/kinsi55/CS_BeatSaber_Camera2), inspected at commit `ee82cfdccf432a00692fad46d32489768942571a`.

MIT License

Copyright (c) 2021 Kinsi

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

## ozz-animation

`src/avatar/TwoBoneIK.cpp` is marked `MIT_OZZ_DERIVED` and adapts the target-softening and analytic two-bone construction from [ozz-animation](https://github.com/guillaumeblanc/ozz-animation), revision `744eb9d99f606eda849acb0b1204f7a3dc20bca1`. SaberStage replaces ozz's SIMD matrices and local correction-quaternion output with its fixed-size scalar math and world-space joint-position output. No other ozz framework source is imported.

ozz-animation is distributed under the MIT License (MIT).

Copyright (c) 2020 Guillaume Blanc

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
