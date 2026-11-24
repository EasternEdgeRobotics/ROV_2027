#!/usr/bin/env python3
"""Prints every photo StarStruck puts in the print folder through CUPS.

Printed photos move to printed/ in that folder, ones that fail to failed/.
Made for a Canon Selphy CP1500, but any CUPS printer works.
"""

import argparse
import shutil
import subprocess
import tempfile
import time
from pathlib import Path

from PIL import Image, ImageOps

POLL_SECONDS = 1.0

# A photo CUPS refuses waits this long before it is tried again.
RETRY_SECONDS = 10.0
ATTEMPTS = 3


FRAME = Path(__file__).with_name("frame.png")


def framed(photo, frame_path):
    """The photo behind the frame, filling the frame's transparent hole."""
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


def queued_jobs(printer):
    command = ["lpstat", "-o"] + ([printer] if printer else [])
    output = subprocess.run(command, capture_output=True, text=True).stdout
    return {line.split()[0] for line in output.splitlines() if line.strip()}


def print_one(printer, photo, frame_path):
    """Sends one photo and waits for CUPS to finish it. Returns False if CUPS
    would not take it."""
    with Image.open(photo) as image, tempfile.TemporaryDirectory() as tmp:
        image = image.convert("RGB")

        if frame_path:
            image = framed(image, frame_path)

        jpeg = Path(tmp) / "print.jpg"
        image.save(jpeg, quality=95)

        # fill crops the 3:2 photo to the paper rather than leaving white
        # strips, which matters on 100x148 mm Selphy postcards.
        command = ["lp", "-o", "print-scaling=fill"]
        command += ["-d", printer] if printer else []
        result = subprocess.run(command + [str(jpeg)], capture_output=True, text=True)

    if result.returncode != 0:
        print(result.stderr.strip())
        return False

    # lp prints "request id is Selphy-12 (1 file(s))".
    job = result.stdout.split()[3]

    # Waiting keeps one photo in CUPS at a time, so the count in the app is
    # what is really left. A printer out of paper or ink holds the job here
    # until someone fixes it.
    while job in queued_jobs(printer):
        time.sleep(POLL_SECONDS)

    return True


def move(photo, folder):
    folder.mkdir(exist_ok=True)
    shutil.move(str(photo), folder / photo.name)


def main():
    default_dir = Path.home() / "Pictures" / "starstruck" / "print"

    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("printer", nargs="?", help="CUPS printer name, default the system default printer")
    parser.add_argument("--dir", type=Path, default=default_dir, help=f"print folder, default {default_dir}")
    parser.add_argument("--frame", type=Path, default=FRAME, help="frame PNG, default frame.png beside this script")
    parser.add_argument("--no-frame", action="store_true", help="print photos without a frame")
    args = parser.parse_args()
    frame = None if args.no_frame else args.frame

    args.dir.mkdir(parents=True, exist_ok=True)
    print(f"Watching {args.dir}")
    failures = {}

    while True:
        for photo in sorted(args.dir.glob("*.tiff")):
            print(f"Printing {photo.name}")

            if print_one(args.printer, photo, frame):
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
