# Development tooling provenance

The root Windows/Linux launchers and `scripts/quest_tool.py` are adapted from the user-owned Big Screen repository's source-deployment, receipt-ownership, log-collection, removal, and ADB-session workflow. They intentionally retain per-file `GPL-3.0-only` notices. This does not select a final license for the SaberStage mod source as a whole.

The SaberStage variant is smaller because Prompt 2 deploys one late-mod library and has no embedded runtime/media payload. It preserves the important safety behavior:

- exactly one authorized Quest unless `ANDROID_SERIAL` selects one;
- direct source deployment refuses an existing unreceipted library, which may belong to ModsBeforeFriday;
- redeploy/remove requires the installed SHA-256 to match SaberStage's receipt;
- removal targets only the exact receipt-owned library and never settings, logs, recordings, dependencies, or unrelated mods;
- launchers stop ADB on completion so ModsBeforeFriday can connect later;
- support logs are collected read-only into a ZIP containing SaberStage's
  current/previous private logs, device/package data, logcat, redacted settings,
  source-receipt/hash state, and a crash-file listing;
- a filtered SaberStage Paper2 excerpt remains in the archive only because
  third-party dependencies may still use Paper2 and their loader output may
  explain a startup failure.
