# AppIcon support for indicator panels

These calls belong to `workbench.library`. They work on a program-drawn tile
registered by the calling task; the indicator owns its panel and source protocol.
They do not implement the declarative `PANEL=` resolver.

## Panel anchor

```c
struct AppIconGeometry geometry = { .aig_Icon = icon };
struct TagItem query[] = {
    { WBCTRLA_QueryAppIconGeometry, (IPTR)&geometry },
    { TAG_DONE, 0 }
};
if (WorkbenchControlA(NULL, query) &&
    (geometry.aig_Flags & APPICONGF_VALID)) {
    /* Position the panel using aig_Left/Top and aig_Width/Height. */
}
```

`WBCTRLA_QueryAppIconGeometry` is `WBA_BASE + 0x113`. `AppIconGeometry` contains
the input `aig_Icon`, output `ULONG aig_Flags`, signed `LONG aig_Left/aig_Top`,
and `ULONG aig_Width/aig_Height`. `APPICONGF_VALID` has value `1UL` (bit 0).

Coordinates are relative to the screen displaying the Wanderer tile. Open the
panel on that same screen; these are not coordinates across several screens.
The returned rectangle is the complete tile footprint, excluding its label,
even when only a fragment is visible or the tile is showing its static fallback.
It is not the bounds of the first exposed fragment or the nontransparent pixels.
Left/top can be negative. The indicator should clamp its panel to its own screen.

Wanderer samples geometry with visibility and on pointer events. VALID requires
at least one presented pixel and geometry/visibility reports less than 1 second
old. Hiding, full coverage, leaving the screen or list mode invalidates the
observation. A successful query can therefore return zero flags and zero bounds.
An unknown, removed or foreign-owned AppIcon returns FALSE with zero outputs.
The query copies scalar values; it transfers no Screen, Window or other pointer.
Treat missing geometry as unavailable and avoid anchoring from stale coordinates.

## Reversible fallback

```c
struct TagItem suspend[] = {
    { WBCTRLA_SuspendAppIcon, (IPTR)icon },
    { TAG_DONE, 0 }
};
BOOL accepted = WorkbenchControlA(NULL, suspend);
```

`WBCTRLA_SuspendAppIcon` is `WBA_BASE + 0x114`. It preserves the AppIcon handle,
owner, port, layout reservation and cached static `.info` image. It requests a
coalesced asynchronous refresh, revokes hover and makes the presenter select the
static fallback. TRUE means the request was accepted, not that the display has
already completed its blit. It does not wait for the desktop or the source.

Repeated suspension is safe. NULL-surface heartbeats cannot restore old dynamic
pixels. To resume, publish a new valid surface through `WBCTRLA_PublishAppIcon`
using the same AppIcon. Normal per-icon cadence still applies; a premature,
invalid or allocation-failed publication returns FALSE and leaves it suspended.
Retry through the indicator's existing bounded event/timer loop. Do not remove
and re-add the icon to resume.

This differs from irreversible `WBCTRLA_DetachAppIcon`. The owner must continue
draining/replying to borrowed messages and follow ordinary removal/port lifetime
rules. A suspended registration can still have one earlier pointer message owned
by its recipient; it is never mutated or freed until returned.

## Sources and panels

An indicator using `System/Tiles` definitions must implement the existing
asynchronous ARexx GET/SET protocol and send `TILE CHANGED <id>` to `TILES` when
its value changes. A custom command/event port needs an adapter if it is not an
ARexx port. An indicator publishing its own AppIcon surface owns that connection
itself. The B6 skeleton's Zune panel stays in the indicator process; unavailable
sources must not make the desktop wait.

These owner-only calls do not let another process query or suspend an icon owned
by `System/Tiles`. Such a presenter would need a separate service command for
panel placement. The current B6 integration uses the indicator's own AppIcon;
the generic declarative panel resolver remains separate work.
