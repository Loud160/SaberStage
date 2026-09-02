# Lifetime and threading rules

The game thread owns `ApplicationRoot`, Unity/IL2CPP objects, menus, camera transforms, compositor state, and UI. The render bridge schedules through Unity's permitted render-thread mechanism. Audio callback owns only a preallocated SPSC write operation. Codec drain, mux/file, network, and chat each use owned joinable workers where needed.

Every cross-thread queue is bounded and has an explicit overflow policy. Media drops newest/oldest frames according to sink contract before blocking gameplay; control messages reserve capacity. Workers never call Unity APIs, invoke callbacks while holding subsystem locks, or retain raw Unity pointers. Packet payload lifetime is shared/pooled and immutable after publication.

Unexpected failures from workers may enqueue plain-text diagnostics and one
pending user message, but they never create or mutate Unity UI. The
process-lifetime `ErrorRuntimeDriver` drains that state on the Unity main
thread only after the active flow, top view controller, and shared prompt are
stable. Its retained flow/prompt references use Unity-safe handles and are
released when the dialog closes or a flow transition invalidates the host.

Lock order is root lifecycle -> session state -> sink state; packet queues use their own short-duration synchronization and are never held during I/O. No shutdown join occurs while holding a lock needed by that worker.

Shutdown order is: stop accepting UI actions; flush pending settings; stop/release chat and reconnection timers; stop sinks; stop frame scheduling; signal audio/video producers; drain/finalize capture within a bound; stop/join network/file/codec workers; release codec/surfaces/GPU bridge on proper threads; destroy preview/compositor/avatar/camera Unity objects; flush diagnostics; destroy services and root. Application-root teardown isolates each subsystem so one cleanup failure is recorded without skipping the remaining releases. Normal worker ownership remains joinable and is joined before its service is destroyed; an impossible-precondition platform join failure is recorded as a critical ownership failure rather than allowed to escape a `noexcept` boundary.
