# Development tooling provenance

The root Windows/Linux launchers and `scripts/quest_tool.py` are adapted from the user-owned Big Screen repository's source-deployment, receipt-ownership, log-collection, removal, and ADB-session workflow. They intentionally retain per-file `GPL-3.0-only` notices. This does not select a final license for the SaberStage mod source as a whole.

The SaberStage variant is smaller because Prompt 2 deploys one late-mod library and has no embedded runtime/media payload. It preserves the important safety behavior:

- exactly one authorized Quest unless `ANDROID_SERIAL` selects one;
- direct source deployment refuses an existing unreceipted library, which may belong to ModsBeforeFriday;
- redeploy/remove requires the installed SHA-256 to match SaberStage's receipt;
- removal targets only the exact receipt-owned library and never settings, logs, recordings, dependencies, or unrelated mods;
- launchers stop ADB on completion so ModsBeforeFriday can connect later;
- support logs are collected read-only into a ZIP containing device/package data, logcat, filtered SaberStage Paperlog lines, settings, source-receipt/hash state, and a crash-file listing.
