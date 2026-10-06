#!/usr/bin/env python3
"""Safe Linux serial diagnostics: never changes BOOT/RESET on open/close.

Usage: python3 scripts/device_console.py artifacts/session
Commands: status, ui, page N, tap X Y, swipe X Y X2 Y2, text VALUE,
          :shot NAME, :boot-down, :boot-up, :reset, :quit
Only one reader may use the serial port at a time. Requires Pillow for PNGs.
"""
import array
import fcntl
import os
from pathlib import Path
import re
import select
import sys
import termios
import time
from PIL import Image

DEFAULT_PORT = '/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0'

class Session:
    def __init__(self, output, port=DEFAULT_PORT):
        self.output = Path(output)
        self.output.parent.mkdir(parents=True, exist_ok=True)
        self.fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        fcntl.flock(self.fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        self.log = self.output.with_suffix('.log').open('w', buffering=1)
        attrs = termios.tcgetattr(self.fd)
        attrs[0] = attrs[1] = attrs[3] = 0
        attrs[2] = (attrs[2] & ~(termios.HUPCL | termios.CSIZE | termios.PARENB | termios.CSTOPB | termios.CRTSCTS)) | termios.CLOCAL | termios.CREAD | termios.CS8
        attrs[4] = attrs[5] = termios.B115200
        attrs[6][termios.VMIN] = 0
        attrs[6][termios.VTIME] = 0
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
        self.pending = bytearray()
        self.shot = None
        self.shot_path = None
        self.saved = False
        self.lines_changed = False
        self.lines = []

    def report(self, line):
        self.log.write(line+'\n')
        self.lines.append(line)
        print(line, flush=True)

    def control(self, mode):
        # Hardware-verified CH340 mapping: DTR=BOOT, RTS=RESET.
        bits = array.array('i', [0])
        fcntl.ioctl(self.fd, termios.TIOCMGET, bits, True)
        bits[0] &= ~(termios.TIOCM_DTR | termios.TIOCM_RTS)
        if mode == 'boot': bits[0] |= termios.TIOCM_DTR
        if mode == 'reset': bits[0] |= termios.TIOCM_RTS
        fcntl.ioctl(self.fd, termios.TIOCMSET, bits)
        self.lines_changed = True
        self.report('CONTROL '+mode)

    def pump(self, seconds=.2):
        until = time.monotonic()+seconds
        while time.monotonic() < until:
            if not select.select([self.fd], [], [], min(.2, max(0, until-time.monotonic())))[0]: continue
            try: data = os.read(self.fd, 65536)
            except BlockingIOError: continue
            self.pending.extend(data)
            while b'\n' in self.pending:
                line, _, rest = self.pending.partition(b'\n')
                self.pending = bytearray(rest)
                line = re.sub(rb'\x1b\[[0-9;]*m', b'', line).rstrip(b'\r')
                if line.startswith(b'SHOT '): self.shot = bytearray(line+b'\n')
                elif self.shot is not None:
                    self.shot.extend(line+b'\n')
                    if line == b'SHOT_END':
                        self.save_shot(bytes(self.shot))
                        self.shot = None
                else: self.report(line.decode(errors='replace'))

    def save_shot(self, raw):
        m = re.search(rb'SHOT (\d+) (\d+)\n(.*?)\nSHOT_END', raw, re.S)
        if not m: raise RuntimeError('Screenshot header invalid')
        w, h = map(int, m.group(1,2))
        if w*h > 320*240: raise RuntimeError('Screenshot dimensions invalid')
        encoded = re.sub(rb'[IWEVD] \(\d+\) [^\r\n]*\r?\n', b'', m.group(3))
        encoded = re.sub(rb'\s', b'', encoded)
        if not re.fullmatch(rb'[0-9a-f]+', encoded) or len(encoded)%8: raise RuntimeError('Screenshot payload invalid')
        pixels = []
        for i in range(0,len(encoded),8):
            count,v = int(encoded[i:i+4],16),int(encoded[i+4:i+8],16)
            if len(pixels)+count > w*h: raise RuntimeError('Screenshot pixel overflow')
            v = (v&255)<<8 | v>>8
            pixels.extend([((v>>11)*255//31,((v>>5)&63)*255//63,(v&31)*255//31)]*count)
        if len(pixels) != w*h: raise RuntimeError('Screenshot pixel count invalid')
        img=Image.new('RGB',(w,h));img.putdata(pixels)
        dest = self.shot_path or self.output.with_suffix('.png')
        dest.parent.mkdir(parents=True,exist_ok=True)
        img.save(dest)
        self.saved=True
        self.report('SAVED '+str(dest))

    def command(self, command, wait=.8):
        self.report('COMMAND '+('wifi [redacted]' if command.startswith('wifi ') else command))
        os.write(self.fd,(command+'\n').encode())
        self.pump(wait)

    def screenshot(self, path, timeout=45):
        self.shot_path=Path(path);self.saved=False
        self.command('shot', .1)
        until=time.monotonic()+timeout
        while not self.saved and time.monotonic()<until:self.pump(.2)
        if not self.saved:raise TimeoutError('Screenshot timed out')
        return self.shot_path

    def close(self):
        if self.lines_changed:self.control('release')
        os.close(self.fd);self.log.close()

    def __enter__(self):return self
    def __exit__(self,*args):self.close()

def main():
    output=sys.argv[1] if len(sys.argv)>1 else 'artifacts/device-session'
    with Session(output, os.environ.get('TERMINAL_PORT',DEFAULT_PORT)) as session:
        print('Serial ready. :shot NAME saves a PNG; :quit closes without reset.',flush=True)
        while True:
            session.pump(.1)
            if not select.select([sys.stdin],[],[],.1)[0]:continue
            line=sys.stdin.readline()
            if not line or line.strip()==':quit':break
            command=line.strip()
            if command.startswith(':shot '):session.screenshot(session.output.parent/(command[6:]+'.png'))
            elif command==':boot-down':session.control('boot')
            elif command==':boot-up':session.control('release')
            elif command==':reset':session.control('reset');time.sleep(.1);session.control('release')
            elif command:session.command(command,.1)

if __name__=='__main__':main()
