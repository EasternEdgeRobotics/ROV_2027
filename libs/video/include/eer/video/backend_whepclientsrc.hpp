#pragma once

#include <eer/video/CameraStream.hpp>

#include <gst/gst.h>

#include <atomic>
#include <mutex>

class WhepClientStream : public CameraStream {
public:
    WhepClientStream();
    ~WhepClientStream() override;

    bool start(const StreamConfig& cfg) override;
    void stop() override;
    bool poll_frame(FrameData& out) override;
    bool failed() const override;
    bool connected() const override;
    std::string codec_summary() const override;
    void request_keyframe() override;

    void on_pad_added(GstPad* pad);

private:
    GstElement* pipeline_ = nullptr;
    GstElement* whepclientsrc_ = nullptr;
    GstElement* appsink_ = nullptr;

    std::atomic<bool> stopping_{false};
    std::atomic<bool> failed_{false};
    std::atomic<bool> connected_{false};

    bool video_linked_ = false;
    bool audio_linked_ = false;

    StreamConfig cfg_;

    std::mutex frame_mutex_;
    FrameData pending_frame_;

    static GstFlowReturn on_new_sample(GstElement* sink, gpointer data);

    static GstBusSyncReply on_bus_sync(
        GstBus* bus,
        GstMessage* msg,
        gpointer data);
};