# SaberStage private FFmpeg runtime notice

SaberStage's optional Direct FFmpeg hardware backend dynamically links a private Android ARM64 build of:

- FFmpeg 9.0.1, licensed under LGPL-3.0-or-later in this configuration: <https://ffmpeg.org/releases/ffmpeg-9.0.1.tar.xz>
- Mbed TLS 3.6.7, licensed under Apache-2.0 and statically incorporated into the private `libavformat` build: <https://github.com/Mbed-TLS/mbedtls/releases/tag/mbedtls-3.6.7>

The exact source archives are SHA-256 pinned in `scripts/build-ffmpeg-hardware.sh`. That script is the complete reproducible cross-build recipe and emits the exact FFmpeg configuration, upstream license texts, binary hashes, and `saberstage-ffmpeg-changes.diff`. The diff records SaberStage's private symbol namespace, Android system certificate-directory support, and forwarding for Android MediaCodec maximum bitrate and complexity controls.

No GPL, nonfree, or software H.264 encoder is enabled. The runtime's video encoder is Android `h264_mediacodec` only. This third-party notice does not choose or imply a license for SaberStage's independently authored source; the project-license decision remains separate.
