#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL), version 1.1.
"""Read-only QMP P6 observations for the appicon_tile live/fallback patterns.

These probe-specific pixel checks start no guest and make no timing claim.
"""
import re

def locate_tile(path):
    # QMP screendump uses binary PPM. Find two exact 64x128 flat-color halves;
    # probe text starts at x+8, so the leftmost columns remain unambiguous.
    data=path.read_bytes();m=re.match(rb'P6\s+(\d+)\s+(\d+)\s+255\s',data)
    if not m:raise RuntimeError('unsupported screenshot encoding')
    w,h=map(int,m.groups());pixels=data[m.end():]
    if len(pixels)!=w*h*3:raise RuntimeError('truncated screenshot')
    def px(x,y):return pixels[(y*w+x)*3:(y*w+x+1)*3]
    found=set()
    for y in range(h):
        x=0
        while x<w:
            start=x;color=px(x,y)
            while x<w and px(x,y)==color:x+=1
            if x-start!=64 or x+64>w:continue
            other=px(x,y)
            if color==other or any(px(xx,y)!=other for xx in range(x,x+64)):continue
            if x+64<w and px(x+64,y)==other:continue
            top=y
            while top and px(start+1,top-1)==color and px(start+96,top-1)==other:top-=1
            bottom=y
            while bottom+1<h and px(start+1,bottom+1)==color and px(start+96,bottom+1)==other:bottom+=1
            if bottom-top+1==128:found.add((start,top))
    if len(found)!=1:raise RuntimeError(f'expected one 128x128 split tile, found {sorted(found)}')
    return next(iter(found)),(w,h)

def locate_fallback(path):
    data=path.read_bytes();m=re.match(rb'P6\s+(\d+)\s+(\d+)\s+255\s',data)
    if not m:raise RuntimeError('unsupported fallback screenshot encoding')
    w,h=map(int,m.groups());pixels=data[m.end():]
    if len(pixels)!=w*h*3:raise RuntimeError('truncated fallback screenshot')
    def px(x,y):return pixels[(y*w+x)*3:(y*w+x+1)*3]
    found=[]
    for y in range(h-31):
        for x in range(w-31):
            a,b=px(x,y),px(x+8,y)
            if a==b or px(x+16,y)!=a or px(x+24,y)!=b:continue
            if px(x,y+8)!=b or px(x,y+16)!=a or px(x,y+24)!=b:continue
            if all(px(x+dx,y+dy)==(a if ((dx//8+dy//8)&1)==0 else b)
                   for dy in range(32) for dx in range(32)):
                found.append((x,y))
    if len(found)!=1:raise RuntimeError(f'expected one 32x32 cached fallback, found {found}')
    return found[0]
