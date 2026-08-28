# Lifetime and threading rules

The game thread owns `ApplicationRoot`, Unity/IL2CPP objects, menus, camera transforms, compositor state, and UI. The render bridge schedules through Unity's permitted render-thread mechanism. Audio callback owns only a preallocated SPSC write operation. Codec drain, mux/file, network, and chat each use owned joinable workers where needed.

Every cross-thread queue is bounded and has an explicit overflow policy. Media drops newest/oldest frames according to sink contract before blocking gameplay; control messages reserve capacity. Workers never call Unity APIs, invoke callbacks while holding subsystem locks, or retain raw Unity pointers. Packet payload lifetime is shared/pooled and immutable after publication.

Lock order is root lifecycle -> session state -> sink state; packet queues use their own short-duration synchronization and are never held during I/O. No shutdown join occurs while holding a lock needed by that worker.

Shutdown order is: stop accepting UI actions; stop/release chat and reconnection timers; stop sinks; stop frame scheduling; signal audio/video producers; drain/finalize capture within a bound; stop/join network/file/codec workers; release codec/surfaces/GPU bridge on proper threads; destroy preview/compositor/avatar/camera Unity objects; flush settings/diagnostics; destroy services and root. Timeout produces a contained failed artifact/log, never a detached worker.
