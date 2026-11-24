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


FRAME = Path(__file__).with_name("frame.png")


def framed(photo, frame_path):
    """The photo behind the frame, filling the frame's transparent hole."""
    from PIL import Image, ImageOps

    with Image.open(frame_path) as frame:
        frame = frame.convert("RGBA")

    # The hole is wherever the frame is fully transparent, so a new design
    # needs no coordinates. Its corners can be any shape, the frame covers
    # what is outside them.
    hole = frame.getchannel("A").point(lambda a: 255 if a == 0 else 0).getbbox()

    if hole is None:
        print(f"{frame_path.name} has no transparent area, printing without it")
        return photo

    left, top, right, bottom = hole
    canvas = Image.new("RGB", frame.size, "white")
    canvas.paste(ImageOps.fit(photo, (right - left, bottom - top)), (left, top))
    canvas.paste(frame, (0, 0), frame)
    return canvas


def print_one(mac, photo, frame_path):
    """Runs in its own process. ivy2's connection thread can only be started
    once per process, so a second print in the same one fails."""
    from PIL import Image
    from ivy2 import Ivy2Printer

    with Image.open(photo) as image, tempfile.TemporaryDirectory() as tmp:
        image = image.convert("RGB")

        if frame_path:
            image = framed(image, frame_path)

        # The paper is portrait. Turning a landscape picture first keeps all
        # of it, where ivy2's own crop would cut off both sides.
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
    parser.add_argument("--frame", type=Path, default=FRAME, help="frame PNG, default frame.png beside this script")
    parser.add_argument("--no-frame", action="store_true", help="print photos without a frame")
    parser.add_argument("--one", type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()
    frame = None if args.no_frame else args.frame

    if args.one:
        print_one(args.mac, args.one, frame)
        return

    args.dir.mkdir(parents=True, exist_ok=True)
    print(f"Watching {args.dir}")
    failures = {}

    while True:
        for photo in sorted(args.dir.glob("*.tiff")):
            print(f"Printing {photo.name}")
            command = [sys.executable, __file__, args.mac, "--one", str(photo)]
            command += ["--no-frame"] if frame is None else ["--frame", str(frame)]
            result = subprocess.run(command)

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
