"""Camera frames for Python vision scripts, as BGR numpy arrays at a fixed size.

    source = frame_capture.open_source("http://192.168.137.200:8889/cam/whep")
    ok, frame = source.read()
    source.release()

The source picks the backend: an http(s) URL is read over WHEP with GStreamer,
an rtsp URL with ffmpeg, and usbN or a video file with OpenCV.
"""

import os
import platform
import shutil
import subprocess
import threading
import time
import urllib.parse
import urllib.request
from pathlib import Path

import cv2
import numpy as np

NO_SIGNAL_IMAGE = Path(__file__).resolve().parents[3] / "assets" / "nosignal" / "nosignal.jpg"
RETRY_SECONDS = 3.0

# A stream that goes quiet without an error, such as the ROV losing power,
# counts as a disconnect after this long.
STALL_SECONDS = 5.0


def add_arguments(parser):
    parser.add_argument(
        "--source",
        default="http://192.168.137.200:8889/cam/whep",
        help="WHEP or RTSP URL, usbN, or a video file",
    )
    parser.add_argument(
        "--resolution",
        default="1280x720",
        help="WIDTHxHEIGHT that every frame is scaled to",
    )


def open_from_args(args):
    return open_source(args.source, *parse_resolution(args.resolution))


def parse_resolution(text):
    try:
        width, height = text.lower().split("x")
        return int(width), int(height)
    except ValueError:
        raise ValueError(f"Resolution {text!r} is not WIDTHxHEIGHT, such as 1280x720") from None


def open_source(source, width=1280, height=720):
    lowered = source.lower()

    if lowered.startswith(("http://", "https://")):
        return WhepSource(source, width, height)

    if lowered.startswith("rtsp://"):
        return FfmpegSource(source, width, height)

    return OpenCVSource(source, width, height)


class OpenCVSource:
    def __init__(self, source, width, height):
        self.width = width
        self.height = height

        if source.lower().startswith("usb"):
            self.cap = cv2.VideoCapture(int(source[3:]))
            self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, width)
            self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, height)
        else:
            self.cap = cv2.VideoCapture(source)

        if not self.cap.isOpened():
            raise RuntimeError(f"OpenCV could not open {source!r}")

    def read(self):
        # A USB camera may not support the size asked for.
        ok, frame = self.cap.read()

        if ok and frame.shape[:2] != (self.height, self.width):
            frame = cv2.resize(frame, (self.width, self.height))

        return ok, frame

    def release(self):
        self.cap.release()


class _StreamSource:
    # Reads the stream on a thread and keeps only the newest frame, so a slow
    # script skips frames instead of falling behind. Shows the no-signal image
    # while disconnected and reconnects on its own. Subclasses implement
    # _session(), which runs one connection and returns why it ended.

    def __init__(self, url, width, height):
        self.url = url
        self.width = width
        self.height = height

        image = cv2.imread(str(NO_SIGNAL_IMAGE))

        if image is None:
            self.no_signal = np.zeros((height, width, 3), dtype=np.uint8)
        else:
            self.no_signal = cv2.resize(image, (width, height))

        self.frame = self.no_signal
        self.lock = threading.Lock()
        self.running = True
        self.thread = threading.Thread(target=self._loop, daemon=True)
        self.thread.start()

    def read(self):
        with self.lock:
            return True, self.frame.copy()

    def release(self):
        self.running = False
        self.thread.join(timeout=2)

    def _set_frame(self, frame):
        with self.lock:
            self.frame = frame

    def _loop(self):
        while self.running:
            try:
                reason = self._session()
            except Exception as exc:
                reason = f"failed: {exc}"

            self._set_frame(self.no_signal)

            if self.running:
                print(f"{self.url} {reason}. Retrying in {RETRY_SECONDS:g}s")
                time.sleep(RETRY_SECONDS)


class FfmpegSource(_StreamSource):
    # OpenCV's own RTSP reader buffers for nearly 10 seconds on macOS and Windows.
    proc = None

    def __init__(self, url, width, height):
        if shutil.which("ffmpeg") is None:
            raise RuntimeError("ffmpeg is not on PATH")

        super().__init__(url, width, height)

    def _session(self):
        # At "error", H.265 prints a page of missing reference errors until the
        # first keyframe. Connection failures are still reported at "fatal".
        cmd = [
            "ffmpeg", "-hide_banner", "-loglevel", "fatal",
            "-rtsp_transport", "tcp", "-timeout", str(int(STALL_SECONDS * 1e6)),
            "-fflags", "nobuffer", "-flags", "low_delay",
        ]

        if platform.system() == "Darwin":
            cmd += ["-hwaccel", "videotoolbox"]

        cmd += [
            "-i", self.url, "-an",
            "-vf", f"scale={self.width}:{self.height}",
            "-pix_fmt", "bgr24", "-f", "rawvideo", "pipe:1",
        ]

        size = self.width * self.height * 3
        self.proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)

        try:
            while self.running:
                data = self.proc.stdout.read(size)

                if len(data) < size:
                    return "ended"

                self._set_frame(np.frombuffer(data, np.uint8).reshape(self.height, self.width, 3))
        finally:
            self.proc.kill()
            self.proc.wait()

        return "stopped"

    def release(self):
        self.running = False

        # The thread is blocked reading ffmpeg's output until ffmpeg exits.
        if self.proc is not None:
            self.proc.kill()

        self.thread.join(timeout=2)


# Everything the decoder can take, so the server picks what it has. The same
# list as the frontend's webrtcbin backend.
VIDEO_CAPS = (
    "application/x-rtp,media=video,encoding-name=H264,payload=127,clock-rate=90000;"
    "application/x-rtp,media=video,encoding-name=H265,payload=126,clock-rate=90000;"
    "application/x-rtp,media=video,encoding-name=VP8,payload=96,clock-rate=90000;"
    "application/x-rtp,media=video,encoding-name=VP9,payload=98,clock-rate=90000;"
    "application/x-rtp,media=video,encoding-name=AV1,payload=99,clock-rate=90000"
)


def _load_gstreamer(backend):
    try:
        import gi

        gi.require_version("Gst", "1.0")
        gi.require_version("GstApp", "1.0")
        gi.require_version("GstSdp", "1.0")
        gi.require_version("GstWebRTC", "1.0")

        # Importing GstApp is what gives appsink its try_pull_sample method.
        from gi.repository import Gst, GstApp, GstSdp, GstWebRTC  # noqa: F401
    except ImportError as exc:
        raise RuntimeError("WHEP sources need PyGObject, see libs/video/docs/Python.md") from exc
    except ValueError as exc:
        raise RuntimeError(f"PyGObject could not find GStreamer: {exc}") from exc
    except AssertionError as exc:
        # uv's own Python on macOS cannot find Homebrew's GLib. Setting this
        # from inside the process is too late, it must be set before Python starts.
        raise RuntimeError(
            "PyGObject could not load GLib. Use a venv made from Homebrew's python3,"
            " or set DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib"
        ) from exc

    Gst.init(None)

    if Gst.ElementFactory.find(backend) is None:
        raise RuntimeError(f"GStreamer has no {backend} element, see libs/video/docs/Python.md")

    return Gst, GstSdp, GstWebRTC


class WhepSource(_StreamSource):
    def __init__(self, url, width, height):
        self.backend = os.environ.get("EER_VIDEO_BACKEND", "webrtcbin")

        if self.backend not in ("webrtcbin", "whepclientsrc"):
            raise ValueError(f"EER_VIDEO_BACKEND is {self.backend!r}, not webrtcbin or whepclientsrc")

        # Loaded here so OpenCV and RTSP sources work without GStreamer, and so
        # a missing install raises now instead of retrying forever.
        self.Gst, self.GstSdp, self.GstWebRTC = _load_gstreamer(self.backend)
        super().__init__(url, width, height)

    def _session(self):
        Gst = self.Gst
        pipeline = Gst.Pipeline.new(None)

        output = Gst.parse_bin_from_description(
            "videoconvert ! videoscale"
            f" ! video/x-raw,format=BGR,width={self.width},height={self.height}"
            " ! appsink name=sink max-buffers=1 drop=true sync=false",
            True,
        )
        pipeline.add(output)

        def on_pad_added(_element, pad):
            if not output.get_static_pad("sink").is_linked():
                pad.link(output.get_static_pad("sink"))

        session_url = None

        try:
            if self.backend == "whepclientsrc":
                self._start_whepclientsrc(pipeline, on_pad_added)
            else:
                session_url = self._start_webrtcbin(pipeline, on_pad_added)

            return self._pull_frames(pipeline, output.get_by_name("sink"))
        finally:
            pipeline.set_state(Gst.State.NULL)

            # Ends the session on the server now, instead of when it notices.
            if session_url is not None:
                try:
                    request = urllib.request.Request(session_url, method="DELETE")
                    urllib.request.urlopen(request, timeout=1.5).close()
                except OSError:
                    pass

    def _start_whepclientsrc(self, pipeline, on_pad_added):
        Gst = self.Gst
        src = Gst.ElementFactory.make("whepclientsrc", None)
        Gst.ChildProxy.set_property(src, "signaller::whep-endpoint", self.url)
        src.set_property("stun-server", None)

        # The default asks for Opus, and a camera with no audio then rejects
        # the whole session. Asking for none works with or without audio.
        Gst.util_set_object_arg(src, "audio-codecs", "<>")

        src.connect("pad-added", on_pad_added)
        pipeline.add(src)
        pipeline.set_state(Gst.State.PLAYING)

    def _start_webrtcbin(self, pipeline, on_pad_added):
        # Returns the session URL the server gave, for ending the session.
        Gst, GstWebRTC = self.Gst, self.GstWebRTC
        webrtc = Gst.ElementFactory.make("webrtcbin", None)
        webrtc.set_property("bundle-policy", GstWebRTC.WebRTCBundlePolicy.MAX_BUNDLE)

        # Milliseconds of jitter buffering. The default 200 is meant for the
        # internet, not a tether.
        webrtc.set_property("latency", 5)

        # webrtcbin's pads carry RTP, which whepclientsrc would have decoded.
        def on_rtp_pad_added(_element, pad):
            decoder = Gst.ElementFactory.make("decodebin", None)
            decoder.connect("pad-added", on_pad_added)
            pipeline.add(decoder)
            decoder.sync_state_with_parent()
            pad.link(decoder.get_static_pad("sink"))

        webrtc.connect("pad-added", on_rtp_pad_added)
        pipeline.add(webrtc)
        webrtc.emit(
            "add-transceiver",
            GstWebRTC.WebRTCRTPTransceiverDirection.RECVONLY,
            Gst.Caps.from_string(VIDEO_CAPS),
        )
        pipeline.set_state(Gst.State.PLAYING)

        promise = Gst.Promise.new()
        webrtc.emit("create-offer", None, promise)
        promise.wait()
        # The offer points into the reply, so the reply has to stay in a
        # variable. Inlined, Python frees it and webrtcbin gets a dead offer.
        reply = promise.get_reply()
        webrtc.emit("set-local-description", reply.get_value("offer"), None)

        # Send every candidate in the offer, so one request does the whole
        # exchange. With no STUN server they are all local and gathering takes
        # milliseconds.
        deadline = time.monotonic() + 2.5

        while (
            webrtc.get_property("ice-gathering-state") != GstWebRTC.WebRTCICEGatheringState.COMPLETE
            and time.monotonic() < deadline
        ):
            time.sleep(0.01)

        request = urllib.request.Request(
            self.url,
            data=webrtc.get_property("local-description").sdp.as_text().encode(),
            headers={"Content-Type": "application/sdp"},
            method="POST",
        )

        with urllib.request.urlopen(request, timeout=8) as response:
            answer = response.read().decode()
            location = response.headers.get("Location")

        _result, sdp = self.GstSdp.SDPMessage.new_from_text(answer)
        webrtc.emit(
            "set-remote-description",
            GstWebRTC.WebRTCSessionDescription.new(GstWebRTC.WebRTCSDPType.ANSWER, sdp),
            None,
        )

        return urllib.parse.urljoin(self.url, location) if location else None

    def _pull_frames(self, pipeline, sink):
        Gst = self.Gst
        bus = pipeline.get_bus()
        last_frame = time.monotonic()

        while self.running:
            message = bus.pop_filtered(Gst.MessageType.ERROR | Gst.MessageType.EOS)

            if message is not None and message.type == Gst.MessageType.EOS:
                return "ended"

            if message is not None:
                return f"failed: {message.parse_error()[0].message.rstrip('.')}"

            sample = sink.try_pull_sample(100 * Gst.MSECOND)

            if sample is not None:
                self._set_frame(self._to_array(sample))
                last_frame = time.monotonic()
            elif time.monotonic() - last_frame > STALL_SECONDS:
                return f"sent nothing for {STALL_SECONDS:g}s"

        return "stopped"

    def _to_array(self, sample):
        buffer = sample.get_buffer()
        data = buffer.extract_dup(0, buffer.get_size())

        # Rows can be padded to an alignment boundary, so the stride comes
        # from the buffer and not from width * 3.
        stride = len(data) // self.height
        rows = np.frombuffer(data, np.uint8)[: stride * self.height].reshape(self.height, stride)
        return rows[:, : self.width * 3].reshape(self.height, self.width, 3)
