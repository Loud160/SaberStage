#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
#
# Part of SaberStage.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

# File responsibility:
# - Builds the pinned Quest FFmpeg hardware-enabled runtime on Linux.
# - Only the codec, muxer, transport, and Android interfaces required by SaberStage are enabled.

# Build SaberStage's private, hardware-only Android FFmpeg runtime.
#
# This recipe intentionally enables no software video encoder. H.264 encoding
# is available only through Android MediaCodec; audio uses FFmpeg's native AAC
# encoder. The private SONAME suffix and symbol namespace keep this runtime
# from binding to another mod's independently packaged FFmpeg libraries.

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"

ffmpeg_version="9.0.1"
ffmpeg_sha256="cf38e0e28c7e5605942c4a77755349b0145804a397af37eb1fb4c77cb237f635"
ffmpeg_archive="ffmpeg-${ffmpeg_version}.tar.xz"
ffmpeg_url="https://ffmpeg.org/releases/${ffmpeg_archive}"

mbedtls_version="3.6.7"
mbedtls_sha256="a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6"
mbedtls_archive="mbedtls-${mbedtls_version}.tar.bz2"
mbedtls_url="https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-${mbedtls_version}/${mbedtls_archive}"

android_api="29"
build_suffix="-saberstage9"
symbol_namespace="SABERSTAGE9"
cache_root="${SABERSTAGE_FFMPEG_CACHE:-${HOME}/.cache/saberstage-ffmpeg}"
install_root="${repo_root}/.cache/dependencies/ffmpeg-hardware"
build_root="${cache_root}/build-android-arm64"
source_root="${cache_root}/ffmpeg-${ffmpeg_version}"
pristine_root="${cache_root}/ffmpeg-${ffmpeg_version}-pristine"
mbedtls_source="${cache_root}/mbedtls-${mbedtls_version}"
mbedtls_build="${cache_root}/mbedtls-build-android-arm64"
mbedtls_install="${cache_root}/mbedtls-install-android-arm64"
ndk_root="${ANDROID_NDK_ROOT:-${HOME}/.cache/bigscreen-toolchains/android-ndk-r27d}"
toolchain="${ndk_root}/toolchains/llvm/prebuilt/linux-x86_64"

download_verified() {
    local url="$1"
    local destination="$2"
    local expected="$3"
    if [[ -f "${destination}" ]] &&
       printf '%s  %s\n' "${expected}" "${destination}" | sha256sum --check --status; then
        return
    fi
    local temporary="${destination}.download.$$"
    rm -f "${temporary}"
    curl --fail --location --retry 3 --output "${temporary}" "${url}"
    printf '%s  %s\n' "${expected}" "${temporary}" | sha256sum --check --status
    mv -f "${temporary}" "${destination}"
}

mkdir -p "${cache_root}"
download_verified "${ffmpeg_url}" "${cache_root}/${ffmpeg_archive}" "${ffmpeg_sha256}"
download_verified "${mbedtls_url}" "${cache_root}/${mbedtls_archive}" "${mbedtls_sha256}"

if [[ ! -x "${toolchain}/bin/aarch64-linux-android${android_api}-clang" ]]; then
    printf 'Android NDK r27d Linux toolchain was not found at %s\n' "${ndk_root}" >&2
    exit 1
fi

rm -rf "${source_root}" "${pristine_root}" "${build_root}" "${install_root}"
tar -xf "${cache_root}/${ffmpeg_archive}" -C "${cache_root}"
cp -a "${source_root}" "${pristine_root}"

if [[ ! -f "${mbedtls_install}/lib/libmbedtls.a" ||
      ! -f "${mbedtls_install}/lib/libmbedx509.a" ||
      ! -f "${mbedtls_install}/lib/libmbedcrypto.a" ]]; then
    rm -rf "${mbedtls_source}" "${mbedtls_build}" "${mbedtls_install}"
    tar -xf "${cache_root}/${mbedtls_archive}" -C "${cache_root}"
    cmake -S "${mbedtls_source}" -B "${mbedtls_build}" \
        -DCMAKE_TOOLCHAIN_FILE="${ndk_root}/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI=arm64-v8a \
        -DANDROID_PLATFORM="android-${android_api}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="${mbedtls_install}" \
        -DENABLE_PROGRAMS=OFF \
        -DENABLE_TESTING=OFF \
        -DUSE_SHARED_MBEDTLS_LIBRARY=OFF \
        -DUSE_STATIC_MBEDTLS_LIBRARY=ON
    cmake --build "${mbedtls_build}" --parallel
    cmake --install "${mbedtls_build}"
fi
if [[ ! -d "${mbedtls_source}" ]]; then
    tar -xf "${cache_root}/${mbedtls_archive}" -C "${cache_root}"
fi

# Android disables ELF symbol versioning in upstream configure. SaberStage
# enables it and assigns a private namespace so another unversioned FFmpeg
# cannot satisfy these imports when both backends are installed.
sed -i '/^[[:space:]]*android)$/,/^[[:space:]]*;;/ s/^[[:space:]]*disable symver$/        enable symver/' \
    "${source_root}/configure"
for component in avcodec avformat avutil; do
    upper="$(printf '%s' "${component}" | tr '[:lower:]' '[:upper:]')"
    sed -i "s/^LIB${upper}_/${symbol_namespace}_LIB${upper}_/" \
        "${source_root}/lib${component}/lib${component}.v"
done

# FFmpeg's mbedTLS transport accepts a CA file but Android exposes its trusted
# roots as a directory. Treat a directory-valued ca_file as a certificate path
# so RTMPS can verify Twitch/Kick/YouTube against the Quest system trust store.
python3 - "${source_root}/libavformat/tls_mbedtls.c" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text()
needle = "mbedtls_x509_crt_parse_file(&tls_ctx->ca_cert, shr->ca_file)"
replacement = "(shr->ca_file && shr->ca_file[strlen(shr->ca_file) - 1] == '/') ? " \
              "mbedtls_x509_crt_parse_path(&tls_ctx->ca_cert, shr->ca_file) : " \
              "mbedtls_x509_crt_parse_file(&tls_ctx->ca_cert, shr->ca_file)"
if needle not in text:
    raise SystemExit("FFmpeg mbedTLS CA-loading site changed; refusing an unreviewed build")
path.write_text(text.replace(needle, replacement, 1))
PY

# Upstream MediaCodec exposes average bitrate but does not forward Android's
# optional maximum-bitrate or complexity keys. Add private AVOptions for those
# documented MediaFormat controls. Unsupported vendor codecs may ignore them;
# SaberStage reports the actual codec format after open and keeps the average
# bitrate as the only guaranteed control.
python3 - "${source_root}/libavcodec/mediacodecenc.c" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text()
edits = [
    ("    int bitrate_mode;\n", "    int bitrate_mode;\n    int64_t max_bitrate;\n    int complexity;\n"),
    ("    if (s->bitrate_mode >= 0) {\n",
     "    if (s->max_bitrate > 0)\n        ff_AMediaFormat_setInt64(format, \"max-bitrate\", s->max_bitrate);\n"
     "    if (s->complexity >= 0)\n        ff_AMediaFormat_setInt32(format, \"complexity\", s->complexity);\n"
     "    if (s->bitrate_mode >= 0) {\n"),
    ("    { \"bitrate_mode\", \"Bitrate control method\",",
     "    { \"max_bitrate\", \"Optional Android hardware encoder peak bitrate\",                         \\\n"
     "                    OFFSET(max_bitrate), AV_OPT_TYPE_INT64, {.i64 = 0}, 0, INT64_MAX, VE },          \\\n"
     "    { \"complexity\", \"Optional Android hardware encoder complexity (0-10)\",                    \\\n"
     "                    OFFSET(complexity), AV_OPT_TYPE_INT, {.i64 = -1}, -1, 10, VE },                  \\\n"
     "    { \"bitrate_mode\", \"Bitrate control method\","),
]
for needle, replacement in edits:
    if needle not in text:
        raise SystemExit(f"FFmpeg MediaCodec option site changed: {needle!r}")
    text = text.replace(needle, replacement, 1)
path.write_text(text)
PY

mkdir -p "${build_root}"
cd "${build_root}"
export PKG_CONFIG_PATH="${mbedtls_install}/lib/pkgconfig"

"${source_root}/configure" \
    --prefix=.saberstage-install \
    --target-os=android \
    --arch=aarch64 \
    --cpu=armv8-a \
    --build-suffix="${build_suffix}" \
    --enable-cross-compile \
    --sysroot="${toolchain}/sysroot" \
    --cc="${toolchain}/bin/aarch64-linux-android${android_api}-clang" \
    --cxx="${toolchain}/bin/aarch64-linux-android${android_api}-clang++" \
    --ar="${toolchain}/bin/llvm-ar" \
    --nm="${toolchain}/bin/llvm-nm" \
    --ranlib="${toolchain}/bin/llvm-ranlib" \
    --strip="${toolchain}/bin/llvm-strip" \
    --enable-pic \
    --enable-shared \
    --disable-static \
    --disable-programs \
    --disable-doc \
    --disable-debug \
    --disable-autodetect \
    --disable-avdevice \
    --disable-avfilter \
    --disable-swscale \
    --disable-swresample \
    --disable-everything \
    --enable-avcodec \
    --enable-avformat \
    --enable-avutil \
    --enable-network \
    --enable-version3 \
    --enable-jni \
    --enable-mediacodec \
    --enable-mbedtls \
    --enable-encoder=h264_mediacodec \
    --enable-encoder=aac \
    --enable-decoder=pcm_s16le \
    --enable-decoder=gif \
    --enable-muxer=mp4 \
    --enable-muxer=flv \
    --enable-demuxer=h264 \
    --enable-demuxer=wav \
    --enable-demuxer=gif \
    --enable-parser=h264 \
    --enable-parser=aac \
    --enable-bsf=aac_adtstoasc \
    --enable-protocol=file \
    --enable-protocol=http \
    --enable-protocol=https \
    --enable-protocol=tcp \
    --enable-protocol=tls \
    --enable-protocol=rtmp \
    --enable-protocol=rtmps \
    --extra-cflags="-O3 -fPIC -w -I${mbedtls_install}/include" \
    --extra-ldflags="-Wl,-Bsymbolic -L${mbedtls_install}/lib" \
    --extra-libs="-lmbedtls -lmbedx509 -lmbedcrypto"

for required in \
    CONFIG_H264_MEDIACODEC_ENCODER CONFIG_AAC_ENCODER CONFIG_PCM_S16LE_DECODER \
    CONFIG_MP4_MUXER CONFIG_FLV_MUXER CONFIG_RTMP_PROTOCOL \
    CONFIG_RTMPS_PROTOCOL CONFIG_MBEDTLS CONFIG_H264_DEMUXER CONFIG_WAV_DEMUXER; do
    grep -q "^${required}=yes$" ffbuild/config.mak || {
        printf 'Required FFmpeg feature is missing: %s\n' "${required}" >&2
        exit 1
    }
done
for forbidden in CONFIG_GPL CONFIG_NONFREE CONFIG_LIBX264_ENCODER; do
    if grep -q "^${forbidden}=yes$" ffbuild/config.mak; then
        printf 'Forbidden FFmpeg feature was enabled: %s\n' "${forbidden}" >&2
        exit 1
    fi
done

make -j"$(nproc)"
make install
mkdir -p "${install_root}"
cp -a "${build_root}/.saberstage-install/." "${install_root}/"
cp "${source_root}/COPYING.LGPLv3" "${install_root}/COPYING.LGPLv3"
cp "${mbedtls_source}/LICENSE" "${install_root}/COPYING.MBEDTLS-APACHE-2.0"
cp config.h "${install_root}/saberstage-ffmpeg-config.h"
cp ffbuild/config.mak "${install_root}/saberstage-ffmpeg-config.mak"

{
    for relative in configure libavcodec/libavcodec.v libavformat/libavformat.v \
                    libavutil/libavutil.v libavformat/tls_mbedtls.c \
                    libavcodec/mediacodecenc.c; do
        diff -u --label "ffmpeg-${ffmpeg_version}/original/${relative}" \
                --label "ffmpeg-${ffmpeg_version}/saberstage/${relative}" \
                "${pristine_root}/${relative}" "${source_root}/${relative}" || [[ $? -eq 1 ]]
    done
} > "${install_root}/saberstage-ffmpeg-changes.diff"

cat > "${install_root}/BUILD-INFO.txt" <<EOF
FFmpeg: ${ffmpeg_version}
FFmpeg source: ${ffmpeg_url}
FFmpeg SHA-256: ${ffmpeg_sha256}
Mbed TLS: ${mbedtls_version}
Mbed TLS source: ${mbedtls_url}
Mbed TLS SHA-256: ${mbedtls_sha256}
Android ABI/API: arm64-v8a / ${android_api}
Video encoders: h264_mediacodec only (no software video encoder)
Audio encoders: AAC
Network outputs: RTMP and certificate-verified RTMPS
License configuration: LGPL-3.0-or-later FFmpeg and Apache-2.0 Mbed TLS; GPL and nonfree disabled
EOF

(cd "${install_root}/lib" && sha256sum *"${build_suffix}".so) > "${install_root}/SHA256SUMS"
printf 'ready\n' > "${install_root}/saberstage-ffmpeg-${ffmpeg_version}.ready"
printf 'SaberStage hardware-only FFmpeg runtime staged at %s\n' "${install_root}"
