# Python frame capture
`python/frame_capture.py` gives vision scripts camera frames as BGR numpy arrays, every one scaled to the same size.

## Sources
| Source | Read with | Needs |
|---|---|---|
| `http://...` or `https://...` | GStreamer over WHEP, the same stream the frontend shows | GStreamer and PyGObject |
| `rtsp://...` | An `ffmpeg` process | `ffmpeg` on `PATH` |
| `usb0`, `usb1`, ... or a video file | OpenCV | Nothing more |

WHEP and RTSP sources read on a thread and keep only the newest frame, so `read()` never waits. While disconnected they return the no-signal image and reconnect every 3 seconds.

## Setup
Make a venv and install the requirements:
```sh
python3 -m venv .venv
.venv/bin/pip install -r libs/video/python/requirements.txt
```
On macOS, make the venv from Homebrew's `python3`. With uv's own Python, PyGObject cannot find Homebrew's GLib unless `DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib` is set before Python starts.

On Ubuntu, install GStreamer's introspection data and what pip needs to build PyGObject first:
```sh
sudo apt install gstreamer1.0-plugins-bad gstreamer1.0-nice gir1.2-gst-plugins-base-1.0 gir1.2-gst-plugins-bad-1.0 \
    libgirepository-2.0-dev libcairo2-dev pkg-config python3-dev
```

## WHEP backends
WHEP sources use `webrtcbin` unless `EER_VIDEO_BACKEND` says otherwise, the same choice as the frontend's build option.

| `EER_VIDEO_BACKEND` | Element | Comes from |
|---|---|---|
| `webrtcbin` | `webrtcbin`, with this file doing the WHEP request | gst-plugins-bad and libnice |
| `whepclientsrc` | `whepclientsrc` | gst-plugins-rs, which Homebrew includes and Ubuntu may not. Check with `gst-inspect-1.0 whepclientsrc` |

## Use
```python
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[N] / "libs" / "video" / "python"))
import frame_capture

parser = argparse.ArgumentParser()
frame_capture.add_arguments(parser)
source = frame_capture.open_from_args(parser.parse_args())

while True:
    ok, frame = source.read()
    ...

source.release()
```
`add_arguments` adds `--source` and `--resolution`. Without arguments, `open_source(url, width, height)` does the same.
