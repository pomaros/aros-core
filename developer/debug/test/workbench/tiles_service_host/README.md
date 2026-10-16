# Declarative tile parser and renderer host verification

Run with a new output directory:

```sh
python3 developer/debug/test/workbench/tiles_service_host/run.py --output /tmp/tiles-parser-render-1
```

The runner compiles the unmodified `workbench/system/Tiles/tiles_config.c` and
`tiles_render.c` with ASan/UBSan and warnings as errors. Only the graphics headers
and graphics calls are replaced by a recording fixture. Compile and execution
have deadlines; nonzero exits, unexpected stderr and missing case counts reject
the run. `parser-render.json` records verdicts and production hashes; compiler
command, generated fixture headers and logs remain in the output directory.

The parser accepts toggle, gauge, slider, value and glyph ToolTypes, with one
GET or ENV source. Keys/template names are case insensitive; duplicate supported
keys, malformed ports/commands, unknown templates, controls, overflow and
oversized fields fail with one bounded diagnostic. There is no logging inside
the parser; the service decides when to log that diagnostic once. Unrelated and
parenthesized disabled ToolTypes are ignored. A bad SIZE becomes 1x1 with a
warning, and EVERY is floored at 60 seconds. SET contains exactly one `%v` and no
other percent substitution. ENV is a relative variable name (including normal
subdirectories), without device prefixes or `.`/`..` components.

No field is silently truncated. Tests cover exact bounds and one byte beyond for
labels, glyph names, panel names, ENV variables, GET ports/commands and source
values. Typed results enforce toggle on/off/1/0 and gauge/slider 0..100; value and
glyph accept nonempty bounded printable text. The service remains responsible
for deadline arithmetic, source transport, replacing `%v` within its own bound,
and for supplying valid readable strings.

The renderer draws native rectangles, line glyphs and text into a supplied
RastPort; it allocates nothing and has no Zune dependency. The caller supplies
the font and pens 0 background, 1 text, 2 accent and 3 quiet. Wi-Fi and sound have
small vector glyphs; other identifiers use a neutral geometric symbol. This is
not a themed icon resolver or a STATES mapping. Text uses TextExtent and shortens
its prefix to fit, including negative bearings and overhang. Current pen, drawing
mode and cursor change; callers must own the RastPort while drawing.

The implemented layouts are 1x1, 2x1 and 2x2, with larger/irregular cell shapes
centering the largest of those layouts that fits. Small surfaces use compact
geometry for future titlebar reuse; no titlebar service is implemented here.
Templates use an On/Off state, percentage gauge/slider, text value or glyph state.
A renderer does not imply slider input handling, panel integration or other
service behavior.

The recording fixture checks every emitted line/rectangle/text extent against
the supplied surface bounds. It covers 480 template/cell/font/value combinations,
3,920 compact surfaces, 37 parser negatives, 10,000 deterministic malformed
inputs, 3x3 centering and changed toggle drawing. Each geometry case checks the
actual drawing operations, including text with negative bearings, rather than a
copy of the renderer's layout calculations. These results establish parser and
geometry behavior under host mocks; native compilation and actual guest pixels,
message delivery, redraw latency and wake-up budgets need service-level evidence.

Task12 adds strict eyes/internal-source and weather protocol checks, HOVER opt-in
semantics, 16,200 weather surface/condition/temperature combinations, and 5,400
eyes surface/extreme-coordinate combinations. Recorded geometry rejects any drawing
outside the supplied RastPort surface. Digest checks require moving pupils to change
the drawing and leaving to return to a coordinate-independent neutral drawing.
These host checks do not prove native pixels or pointer event delivery.
