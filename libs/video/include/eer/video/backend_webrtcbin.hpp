#pragma once

#include <eer/video/CameraStream.hpp>

#include <gst/gst.h>
#include <gst/webrtc/webrtc.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// WHEP (WebRTC HTTP Egress Protocol) receiver built directly on webrtcbin.
//
// Everything that can block, DNS, the HTTP offer/answer exchange, trickled
// ICE candidates, the session teardown, runs on this object's own signalling
// thread with hard timeouts, so a camera that is unplugged, unrouteable or
// simply slow can never stall a GStreamer streaming thread or the render loop.
class WebRtcBinStream : public CameraStream {
public:
    WebRtcBinStream();
    ~WebRtcBinStream() override;

    bool start(const StreamConfig& cfg) override;
    void stop() override;
    bool poll_frame(FrameData& out) override;
    bool failed() const override;
    bool connected() const override;
    std::string codec_summary() const override;
    void request_keyframe() override;

private:
    struct IceCandidate {
        guint mline_index = 0;
        std::string candidate;
    };

    struct HttpResponse {
        long status = 0;
        std::string body;
        std::string location;
    };

    // Keeps asynchronous GStreamer callbacks from touching a stream that has
    // already been stopped. stop() clears `self` under the mutex, so a callback
    // either runs to completion first or observes null and does nothing.
    struct CallbackToken {
        std::mutex mutex;
        WebRtcBinStream* self = nullptr;
    };

    // --- pipeline ----------------------------------------------------------
    GstElement* pipeline_ = nullptr;
    GstElement* webrtcbin_ = nullptr;

    std::atomic<bool> stopping_{false};
    std::atomic<bool> failed_{false};
    std::atomic<bool> connected_{false};
    std::atomic<bool> latency_logged_{false};

    // Only ever touched from webrtcbin's streaming thread.
    bool video_linked_ = false;
    bool audio_linked_ = false;

    StreamConfig cfg_;

    std::mutex frame_mutex_;
    FrameData pending_frame_;

    // Set from webrtcbin's streaming thread as pads appear, read by the UI.
    mutable std::mutex codec_mutex_;
    std::string video_codec_;
    std::string audio_codec_;

    // --- signalling --------------------------------------------------------
    std::shared_ptr<CallbackToken> token_;

    std::mutex signalling_mutex_;
    std::thread signalling_thread_;

    std::mutex ice_mutex_;
    std::condition_variable ice_cv_;
    std::vector<IceCandidate> ice_candidates_;
    size_t ice_candidates_sent_ = 0;
    bool ice_gathering_done_ = false;

    std::mutex resource_mutex_;
    std::string resource_url_;

    // --- GStreamer callbacks ----------------------------------------------
    void on_offer_created(GstPromise* promise);
    void on_ice_candidate(guint mline_index, const gchar* candidate);
    void on_pad_added(GstPad* pad);
    void on_ice_gathering_state_changed();
    void on_ice_connection_state_changed();
    void on_connection_state_changed();

    void link_video(GstPad* pad, const std::string& encoding_name);
    void link_audio(GstPad* pad);
    void log_pipeline_latency();

    // --- signalling thread -------------------------------------------------
    void run_signalling(std::string offer_sdp);
    void wait_for_ice_gathering();
    std::string offer_with_candidates(const std::string& offer_sdp);
    bool apply_answer(const std::string& answer_sdp);
    void trickle_candidates(const std::string& offer_sdp);
    void delete_whep_resource();

    bool whep_exchange(
        const std::string& offer_sdp,
        std::string& out_answer_sdp);

    bool http_request(
        const std::string& url,
        const char* method,
        const char* content_type,
        const std::string& body,
        long timeout_ms,
        bool abortable,
        HttpResponse& out);

    static GstFlowReturn on_new_sample(GstElement* sink, gpointer data);

    static GstBusSyncReply on_bus_sync(
        GstBus* bus,
        GstMessage* msg,
        gpointer data);
};