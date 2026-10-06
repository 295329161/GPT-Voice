#!/usr/bin/env python3
"""Capture every directly accessible device page; no credentials or SD writes.

Enter file details/image preview through the file browser separately. Voice is
not started by this helper because live service verification incurs API usage.
"""
from pathlib import Path
import argparse
from device_console import Session

PAGES = [(0,'00-home'),(1,'01-menu-1'),(2,'02-settings'),(3,'03-music-idle'),
         (4,'04-games'),(5,'05-pictures'),(7,'07-files'),(8,'08-home-assistant'),
         (9,'09-brightness'),(10,'10-volume'),(11,'11-wifi'),(12,'12-bluetooth'),
         (13,'13-weather-city'),(14,'14-web-disabled'),(15,'15-about'),
         (16,'16-shooter'),(17,'17-tiles'),(18,'18-flood'),(21,'21-weather-preview'),
         (22,'22-usb-idle')]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',default='artifacts/screenshots')
    args=parser.parse_args();out=Path(args.output);out.mkdir(parents=True,exist_ok=True)
    with Session(out/'capture') as session:
        session.command('status')
        for page,name in PAGES:
            session.command(f'page {page}',1.2)
            if page==16:session.command('tap 200 18',.7)
            session.screenshot(out/(name+'.png'))
        session.command('page 1',.8)
        session.command('swipe 260 100 40 100',.8)
        session.screenshot(out/'01-menu-2.png')
        session.command('page 2',.8)
        session.command('swipe 280 210 280 70',.8)
        session.screenshot(out/'02-settings-bottom.png')
        session.command('page 0')
        session.command('status')

if __name__=='__main__':main()
