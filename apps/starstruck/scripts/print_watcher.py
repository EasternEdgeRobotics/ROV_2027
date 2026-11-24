#!/usr/bin/env python3
"""Prints every photo StarStruck puts in the print folder on a Canon Ivy 2.

Printed photos move to printed/ in that folder, ones that fail to failed/.
Needs dtgreene/ivy2 on PYTHONPATH, which needs BlueZ, so Linux only.
"""

import argparse
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

POLL_SECONDS = 1.0

# A failure the printer reports (no paper, low battery, cover open) waits this
# long before the photo is tried again, so it is not hammered while someone
# fixes it.
RETRY_SECONDS = 10.0
ATTEMPTS = 3


def print_one(mac, photo):
    """Runs in its own process. ivy2's connection thread can only be started
    once per process, so a second print in the same one fails."""
    from PIL import Image
    from ivy2 import Ivy2Printer

    # The paper is portrait. Turning a landscape photo first keeps everyone in
    # it, where ivy2's own crop would cut off both sides.
    with Image.open(photo) as image, tempfile.TemporaryDirectory() as tmp:
        image = image.convert("RGB")

        if image.width > image.height:
            image = image.rotate(90, expand=True)

        jpeg = Path(tmp) / "print.jpg"
        image.save(jpeg, quality=95)

        printer = Ivy2Printer()
        printer.connect(mac)

        try:
            printer.print(str(jpeg))
        finally:
            printer.disconnect()


def move(photo, folder):
    folder.mkdir(exist_ok=True)
    shutil.move(str(photo), folder / photo.name)


def main():
    default_dir = Path.home() / "Pictures" / "starstruck" / "print"

    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("mac", help="the printer's Bluetooth address")
    parser.add_argument("--dir", type=Path, default=default_dir, help=f"print folder, default {default_dir}")
    parser.add_argument("--one", type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()

    if args.one:
        print_one(args.mac, args.one)
        return

    args.dir.mkdir(parents=True, exist_ok=True)
    print(f"Watching {args.dir}")
    failures = {}

    while True:
        for photo in sorted(args.dir.glob("*.tiff")):
            print(f"Printing {photo.name}")
            result = subprocess.run([sys.executable, __file__, args.mac, "--one", str(photo)])

            if result.returncode == 0:
                move(photo, args.dir / "printed")
                continue

            failures[photo] = failures.get(photo, 0) + 1

            if failures[photo] >= ATTEMPTS:
                print(f"Giving up on {photo.name}, moved to failed/")
                move(photo, args.dir / "failed")
            else:
                time.sleep(RETRY_SECONDS)

        time.sleep(POLL_SECONDS)


if __name__ == "__main__":
    main()
