# TV and receiver output feasibility

This output must carry the SaberStage camera/compositor, not the Quest HMD mirror. The recommended Stage 2 experiment is a small authenticated receiver that consumes the existing encoded stream, first in the desktop companion and a browser/Android TV test receiver. This maximizes reuse and makes latency/compatibility measurable.

Google Cast is possible only as a real sender/receiver integration: Google documents Android/Web senders plus Web or Android TV receivers. It is not a generic "send these encoder packets to every TV" API, and integrating its Android sender libraries into a patched native Unity app needs a device proof. A custom receiver may require hosted receiver registration and suitable stream packaging. See the [Google Cast overview](https://developers.google.com/cast/docs/overview).

DLNA/UPnP and generic HTTP/HLS have broad playback potential but commonly add startup latency and inconsistent live controls. WebRTC can be low latency but adds ICE/DTLS/SRTP, signaling, and receiver-browser variability. AirPlay sender support is not assumed from public Android APIs. No approach is promised as universal.

All receiver sinks are bounded and independently droppable, reuse the selected encode when codec-compatible, and cannot trigger a second render. Milestone acceptance requires representative Chromecast/Google TV, Android TV, smart-TV browser, and ordinary browser tests before product claims.
