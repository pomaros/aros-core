#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL), version 1.1.
"""Synthetic pixel-oracle controls, not an AROS display qualification."""
from pathlib import Path
import tempfile
import unittest
from tiles_pixels import locate_tile, locate_fallback


class Pixels(unittest.TestCase):
    def test_pixels(self):
        w,h=240,190
        def pixel_image(kind):
            data=bytearray(bytes((3,4,5))*(w*h))
            if kind=='live':
                for y in range(40,168):
                    for x in range(20,148):
                        data[(y*w+x)*3:(y*w+x+1)*3]=bytes((10,30,50) if x<84 else (70,90,110))
                # Text can reach x+63: the locator must use the untouched
                # left edge, not assume all label pixels lie before x+60.
                for y in range(58,66):
                    for x in range(28,84):
                        data[(y*w+x)*3:(y*w+x+1)*3]=bytes((3,4,5))
            if kind=='fallback':
                for y in range(20,52):
                    for x in range(30,62):
                        data[(y*w+x)*3:(y*w+x+1)*3]=bytes((10,30,50) if (((x-30)//8+(y-20)//8)&1)==0 else (70,90,110))
            return f'P6\n{w} {h}\n255\n'.encode()+data
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'frame.ppm'
            p.write_bytes(pixel_image('live'));self.assertEqual(locate_tile(p),((20,40),(w,h)))
            with self.assertRaises(RuntimeError):locate_fallback(p)
            p.write_bytes(pixel_image('fallback'));self.assertEqual(locate_fallback(p),(30,20))
            with self.assertRaises(RuntimeError):locate_tile(p)
            p.write_bytes(pixel_image('blank'))
            with self.assertRaises(RuntimeError):locate_tile(p)
            with self.assertRaises(RuntimeError):locate_fallback(p)

    def test_bad_encoding_and_truncated_pixels(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'frame.ppm'
            for blob in (b'P3\n2 2\n255\n', b'P6\n2 2\n255\n'+bytes(11)):
                path.write_bytes(blob)
                for locate in (locate_tile, locate_fallback):
                    with self.assertRaises(RuntimeError):
                        locate(path)


if __name__=='__main__':
    unittest.main()
