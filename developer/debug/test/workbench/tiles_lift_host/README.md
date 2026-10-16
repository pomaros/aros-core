# Cached lift presenter host checks

Run `python3 run.py --output /tmp/tiles-lift-fresh` (fresh directory required).
The runner extracts the current production lift.c bodies and compiles them with
ASan/UBSan against an allocation, graphics and window accounting model. It checks
four allocation failure points, failed screen read, exact 30% RGB dimming with
alpha preserved, clipping at negative/right/bottom boundaries, local hit coordinates,
outside/Escape dismissal, cache-only refresh and complete cleanup. The model's
ModifyIDCMP deliberately deletes a non-null port to detect incorrect detach order.

This is not native focus/input, SMP, screenshot capture or latency evidence. It does
not prove immediate ordering above windows opened later: TileLiftFront requires
caller scheduling, currently a bounded polling fallback. The caller must hold the
screen lock and borrowed bitmaps until Close, and forward SELECTDOWN/Escape exactly
once. The presenter owns only its screenshot, metadata, window and message port.
