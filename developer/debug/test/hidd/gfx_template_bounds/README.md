The host regression compiles the actual 8-, 16-, 24- and 32-bit template
blitters from `rom/hidds/gfx/gfx_bitmapmemblit.c`. Run from the core root:

```sh
python3 developer/debug/test/hidd/gfx_template_bounds/test.py
```

It compares output against a pixel oracle for six drawing modes, varied source
bit offsets, short and full-word widths, multiple rows and padded destinations.
Each template ends at an inaccessible page. ASan and UBSan check the little
endian host execution; this does not qualify a display driver or interrupt path.
`--source FILE` selects a retained source revision for a negative control.
