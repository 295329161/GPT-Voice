#!/usr/bin/env python3
"""Send demo commands or capture its LVGL framebuffer; never flashes the board."""
import argparse
from pathlib import Path
import re
import time
import serial

p = argparse.ArgumentParser()
p.add_argument('commands', nargs='*', default=['status'])
p.add_argument('--port', default='/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0')
p.add_argument('--wait', type=float, default=3)
p.add_argument('--output', default='artifacts/demo-serial.log')
a = p.parse_args()
Path(a.output).parent.mkdir(parents=True, exist_ok=True)
s = serial.Serial(port=None, baudrate=115200, timeout=.1)
s.dtr = False
s.rts = False
s.port = a.port
s.open()
log = bytearray()
def receive(seconds):
    end=time.monotonic()+seconds
    data=bytearray()
    while time.monotonic()<end:
        data.extend(s.read(s.in_waiting or 1))
    log.extend(data)
    return bytes(data)
receive(3)
shot_index=0
for cmd in a.commands:
    s.write((cmd+'\n').encode())
    data=receive(max(a.wait, 10) if cmd=='shot' else a.wait)
    if cmd=='shot':
        m=re.search(rb'SHOT (\d+) (\d+)\r?\n([0-9a-f\r\n]+)\r?\nSHOT_END',data)
        if not m:
            Path(a.output).write_bytes(log)
            raise RuntimeError('Incomplete screenshot: '+data[:200].decode(errors='replace'))
        w,h=map(int,m.group(1,2)); pixels=[]
        encoded=re.sub(rb'\s',b'',m.group(3)).decode()
        for i in range(0,len(encoded),8):
            count=int(encoded[i:i+4],16);v=int(encoded[i+4:i+8],16)
            # LVGL RGB565 uses swapped bytes for this SPI panel.
            v=((v&255)<<8)|(v>>8)
            rgb=((v>>11)*255//31,((v>>5)&63)*255//63,(v&31)*255//31)
            pixels.extend([rgb]*count)
        if len(pixels)!=w*h: raise RuntimeError(f'Bad pixel count {len(pixels)} expected {w*h}')
        from PIL import Image
        image=Image.new('RGB',(w,h));image.putdata(pixels)
        path=Path(a.output).with_suffix('.png')
        if shot_index: path=path.with_name(path.stem+f'-{shot_index}'+path.suffix)
        shot_index+=1
        image.save(path);print(path)
    else:
        print(data.decode(errors='replace'))
Path(a.output).write_bytes(log)
s.close()
