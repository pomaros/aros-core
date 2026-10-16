#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# SPDX-License-Identifier: APL-1.1
"""Replay retained indicator logs, real-input records and artifact hashes."""
import argparse
import hashlib
import json
from pathlib import Path
import re
p=argparse.ArgumentParser();p.add_argument('run',type=Path);a=p.parse_args()
r=a.run;result=json.loads((r/'result.json').read_text())
assert result['result']=='PASS' and not result['errors'] and result['qemu_exit']==0
for name,digest in result['sha256'].items():assert hashlib.sha256((r/name).read_bytes()).hexdigest()==digest,name
assert (r/'guest-errors.log').read_bytes()==b''
text=(r/'serial.log').read_text()
assert not re.search(r'\b(?:ERROR|FAIL|PANIC|CRASH|FATAL|GURU)\b|Synchronous Exception',text,re.I)
assert text.count('[INDPROBE] PASS')==1
for phase,expected in [('escape',0),('hotkey',1),('outside',0),('glyph',1),('glyph-second',0)]:
    part=text.split('[INDPROBE] '+phase+'-ready',1)[1]
    found=re.search(r'\[INDPROBE\] command=STATS success=1 answer=healthy=\d panel=(\d)',part)
    assert found and int(found[1])==expected,phase
programmatic='[INDPROBE] programmatic-glyph opened=1' in text
exchange='[INDPROBE] exchange' in text
if exchange:
    assert '[INDPROBE] exchange disabled-closes=1 disabled-click-ignored=1 enabled-click-opens=1 same-registration=1' in text
    assert 'disabled=1' in text and 'command=PANEL success=0' in text
    assert any(event['event']=='disabled-glyph-injected' for event in result['events'])
    assert any(event['event']=='enabled-glyph-injected' for event in result['events'])
if programmatic:
    part=text.split('[INDPROBE] programmatic-ready',1)[1]
    assert re.search(r'command=STATS success=1 answer=healthy=\d panel=1',part)
    assert any(x['execute']=='input-send-event' for x in [json.loads(line)['send'] for line in (r/'qmp.jsonl').read_text().splitlines() if 'send' in json.loads(line)])
held=text.split('[INDPROBE] settings-held' ,1)[1].split('[INDPROBE] settings-returned',1)[0]
assert 'command=PANEL success=1' in held and 'panel=1' in held and 'command=HIDE success=1' in held
assert 'command=UPDATE 101 1 invalid success=0' in text
assert 'command=GET success=0' in text and 'healthy=0' in text
assert 'command=GET success=1 answer=Battery' in text
assert 'command=QUIT success=1' in text
frames=[int(x) for x in re.findall(r'\[TILE2\] accepted id=1 frame=(\d+)',text)]
assert len(frames)>=4 and all(b-a>=500 for a,b in zip(frames,frames[1:])),frames
requests=[json.loads(line)['send'] for line in (r/'qmp.jsonl').read_text().splitlines() if 'send' in json.loads(line)]
assert sum(x['execute']=='send-key' for x in requests)==2
assert sum(x['execute']=='input-send-event' for x in requests)>=9
assert any(x['execute']=='query-status' for x in requests) and requests[-1]['execute']=='quit'
anchors=re.findall(r'healthy=\d panel=1 draws=\d+ suspends=\d+ anchor=(-?\d+),(-?\d+) glyph=(-?\d+),(-?\d+)',text)
assert anchors, "missing measured anchor"
if anchors:
    assert all((int(ax),int(ay))==(int(gx),int(gy)) for ax,ay,gx,gy in anchors),anchors
    assert max(map(int,re.findall(r'suspends=(\d+)',text)))>=1
copies=re.findall(r'\[TILE5\] copy tile=(0x[0-9a-f]+) initial=(\d)',text)
typed='[INDPROBE] typed-protocol' in text
if typed:
    if '[INDPROBE] mismatched-confirmation' in text:
        assert '[INDPROBE] mismatched-confirmation rejected=1 preserved=42' in text
        assert '[INDPROBE] typed-confirmation calls=5 timeout=1 late-ignored=1 newer-preserved=1' in text
    else:
        assert '[INDPROBE] typed-confirmation calls=4 timeout=1 late-ignored=1 newer-preserved=1' in text
    assert '[INDPROBE] typed-radio unavailable-and-off-distinct=1' in text
    assert 'command=GET PERCENT success=1 answer=75' in text
    assert 'command=SET LEVEL 80 success=0' in text and 'command=SET LEVEL 90 success=1 answer=Confirmed' in text
    assert 'command=GET LEVEL success=1 answer=37' in text
    assert 'command=GET RADIO success=1 answer=off' in text and 'command=GET RADIO success=1 answer=on' in text
    assert sum(initial=='1' for handle,initial in copies)==3
    if '[INDPROBE] typed-radio-start' in text:
        sections=[text.split('[INDPROBE] typed-protocol')[0],
                  text.split('[INDPROBE] typed-protocol')[1].split('[INDPROBE] typed-radio-start')[0],
                  text.split('[INDPROBE] typed-radio-start')[1]]
        for section in sections:
            handles=re.findall(r'\[TILE5\] copy tile=(0x[0-9a-f]+)',section)
            assert len(set(handles))==1
        assert ('command=UPDATE STATUS Joined-but-radio-unknown success=1' in text or
                'command=UPDATE LINK on Joined-but-radio-unknown success=1' in text)
else:
    assert len({handle for handle,initial in copies})==1
    assert sum(initial=='1' for handle,initial in copies)==1
cpu_aux='[INDPROBE] cpu-aux' in text
if cpu_aux:
    assert '[INDPROBE] cpu-aux independent-lease=1 invalid-rejected=1 expired=1' in text
    sampled=re.search(r'\[INDPROBE\] cpu-resource scanned=(\d+) bounded=1 no-task-pointers=1',text)
    assert sampled and 0<int(sampled.group(1))<=128
    assert 'command=GET TOP success=1 answer=CPU data unavailable' in text
    assert 'command=GET TOP success=1 answer=CPU: independent 25.00%' in text
battery_service='[INDPROBE] battery-service' in text
if battery_service:
    assert '[INDPROBE] battery-service absent=1 subscriptions-bound=8 events-switch=1 ui-subscribe-unsubscribe=1 shutdown-complete=1' in text
    assert 'command=SUBSCRIBE INDPROBE.BAT8 success=0' in text
    assert 'command=SUBSCRIBE INDPROBE.BAT8 success=1' in text
sound='[INDPROBE] sound-mute-ready' in text
if sound:
    assert '[INDPROBE] sound-mute confirmed=0 calls=6' in text
    if '[INDPROBE] sound-real-source' in text:
        assert '[INDPROBE] sound-real-source unavailable=1 shutdown-complete=1' in text
    assert 'command=GET LEVEL success=1 answer=0' in text
    assert any(event['event']=='sound-mute-injected' for event in result['events'])
wifi='[INDPROBE] wifi-real-worker' in text
control_icons='[INDPROBE] control-icons' in text
if control_icons:
    assert '[INDPROBE] control-icons installer=1 bindings=3 battery-fallback=1' in text
wifi_palette='[INDPROBE] wifi-palette' in text
wifi_negative='[INDPROBE] wifi-negative' in text
if wifi_negative:
    assert '[INDPROBE] wifi-negative settings-only=1 lease-expired=1 recovered=1 same-registration=1' in text
    assert 'network-actions=0' in text and 'network-actions=1' in text
    assert (r/'wifi-unavailable.ppm').is_file()
    assert any(event['event']=='wifi-unavailable' for event in result['events'])
if wifi_palette:
    assert '[INDPROBE] wifi-palette other-opens=1 connect-unavailable=1 commands-complete=1' in text
    assert 'command=WIFI OTHER success=1' in text and 'command=WIFI CONNECT success=0' in text
if wifi:
    assert '[INDPROBE] wifi-real-worker unavailable=1 ui-responsive=1' in text
    if 'reserved=' in text: assert '[INDPROBE] wifi-wake allocated=1 reserved=0' in text
    assert '[INDPROBE] wifi-real-worker shutdown-complete=1' in text
    assert re.search(r'command=WIFI STATUS success=1 answer=ready=1 busy=0 wireless=0 dead=0',text)
    assert 'command=WIFI SCAN success=0' in text and 'command=WIFI DISCONNECT success=0' in text
verdict={'result':'PASS'  ,'scope':'Independent replay of UP indicator smoke artifacts; not full WM15 or hardware',
         'control_icons':control_icons,'wifi_palette':wifi_palette,'wifi_negative':wifi_negative,'exchange':exchange,
         'checker_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
         'typed_contract':typed,'cpu_auxiliary':cpu_aux,'wifi_real_worker':wifi,'sound_mute_input':sound,'battery_service':battery_service,'measured_anchors':anchors,'single_registration':True,'frames':len(frames),'real_input_phases':(6 if programmatic else 5)+(2 if exchange else 0),'held_settings_responsiveness':True}
print(json.dumps(verdict))
