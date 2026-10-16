# Wanderer tile visibility geometry

The production visibility query uses the same image offsets, icon position and
scroll origin as tile drawing, intersects the iconlist viewport and screen, then
checks the display window layer's unobscured clip rectangles under `LockLayer`.
It never samples the backing bitmap, redraws to measure visibility, or invokes a
presenter while holding the layer lock. Screen order is sampled under a separate
short `LockIBase`; later moves are observed on the existing 250ms timer. Hidden,
list-mode, out-of-viewport and fully occluded entries publish false. Hide,
destruction and entry removal publish false before releasing their storage.

The Workbench registry receives a timestamped observation even when its bounded
notification pool is full. Only actual state changes emit a visibility message;
the service's coalesced query is the retry path. A separate accepted-frame relay
requests an immediate dirty redraw without advancing the regular refresh tick.

Run the production intersection helper's host checks:

```sh
clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I workbench/system/Wanderer/Classes \
  developer/debug/test/workbench/tiles_visibility/geometry.c -o /tmp/tile-geometry
/tmp/tile-geometry
```

These checks cover inclusive pixel boundaries, full occlusion, a one-pixel
uncovered strip, viewport clipping, negative scrolled coordinates and an empty
visible clip list. They do not replace native compilation and actual desktop
occlusion/visibility evidence.
