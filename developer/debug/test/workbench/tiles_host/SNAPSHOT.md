# Desktop presenter snapshots

The dedicated presenter tags copy other owners' cached AppIcon tiles. Existing
owner-only query/publication APIs are unchanged. Caller supplies an array of up
to40 entries, and holds its Screen lock until Free. Screen pointers are compared
only as identities under the registry lock, never dereferenced there.

Placement has its own timestamp and screen identity, separate from visible
geometry. Wanderer reports full placement when an otherwise shown desktop tile
is covered. Hidden/list/nonfront presentation and entry removal invalidate it;
visible geometry continues to fail closed when covered. Placement expires at
1000ms if updates stop. Fresh unsuspended frames are copied dynamically. Stale, suspended or dead
owners use a static fallback pre-rendered from the private duplicated DiskObject
during WBTileCreate, before registry insertion/locking (AddAppIconA:343/351).
The fallback is clipped to the tile's bitmap by a private layer, with no owner
render hook. Each snapshot owns a separate fallback copy, so original removal
can still switch the lifted image to its static fallback. INCOMPLETE/Omitted
is reserved for missing caches; normal stale producers do not set it. Capacity
failure is atomic, without truncation.

Each opaque handle identifies a private registry node through a monotonically
increasing token, not an allocator address. Entries expose read-only independent
bitmap copies and a copied label. Four snapshots globally and one per creator
bound memory. Free/Refresh/Activate validate handle and creator before accessing
node memory. Removal of an original tile does not invalidate copied bitmap
storage. Creator exit marks its nodes retired; the next ordinary snapshot API
or tile cleanup frees graphics resources outside the task-exit callback.

Activation checks registration generation, frame generation, live owner/port,
same fresh placement/screen, fresh content, local bounds and named region before
using the existing64-message global bound. No owner bitmap or port is borrowed.
Refresh synchronously copies changed frames into the same snapshot bitmaps and
updates the internal frame generation. Caller must not concurrently draw during
Refresh. Removed/stale/moved entries switch to static fallback and lose activation
eligibility without disabling other tiles. Fresh recovery updates pixels and
restores activation. Reopen to change geometry/membership. FALSE means an
invalid or foreign handle.

`test_snapshot.c` exercises actual production functions with host OS/graphics
mocks: covered placement, owner/foreign handles, capacity and allocation rollback,
independent pixels, refresh address stability, named click routing, stale-frame
refusal, removal, allocator-address reuse with matching frame epochs, wrong
screen, hidden/stale placement, static fallback/suspend/resume/owner-exit, per-owner/global bounds and
deferred creator-exit reclamation. This is not guest rendering evidence.
