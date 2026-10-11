#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct FrameData {
    std::vector<uint8_t> y_plane;
    std::vector<uint8_t> uv_plane;
    int width = 0;
    int height = 0;
    bool dirty = false;
};

struct StreamConfig {
    std::string url;

    // Optional backend-specific caps. Backends may ignore these.
    std::string video_caps;
    std::string audio_caps;

    std::string label;
};

class CameraStream {
public:
    virtual ~CameraStream() = default;

    virtual bool start(const StreamConfig& cfg) = 0;
    virtual void stop() = 0;
    virtual bool poll_frame(FrameData& out) = 0;
    virtual bool failed() const = 0;

    // True once the transport is up far enough that media should be flowing.
    // Backends that cannot tell may keep the default.
    virtual bool connected() const { return false; }

    // The codecs actually negotiated, e.g. "H265" or "H264 + OPUS". Empty until
    // the session is up, or if the backend cannot tell.
    virtual std::string codec_summary() const { return {}; }

    // Ask the sender for a fresh IDR. Called when decoding stalls, so that a
    // short burst of packet loss costs a keyframe rather than a full
    // renegotiation. A no-op for backends that cannot signal upstream.
    virtual void request_keyframe() {}
};