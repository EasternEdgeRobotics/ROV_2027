#include <eer/video/backend_webrtcbin.hpp>

#include <gst/app/gstappsink.h>
#include <gst/sdp/sdp.h>
#include <gst/video/video.h>
#include <curl/curl.h>

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace {

// The elements needed to receive one video codec: RTP depayloader, optional
// parser, and a preference-ordered list of decoder factories.
struct VideoDecodeChain {
    const char* encoding_name;
    const char* depay;
    const char* parse;
    const char* const* decoders;
};

constexpr guint kVideoQueueBuffers = 1;
constexpr guint kAudioQueueBuffers = 1;
constexpr guint kAppSinkBuffers = 1;

// Queue leaky modes:
// 0 = no leak
// 1 = upstream/new buffers
// 2 = downstream/old buffers
constexpr guint kQueueLeakyMode = 2;

// Jitter buffering, in milliseconds. webrtcbin's own default is 200ms, which is
// sized for the public internet; the tether is a stable wired link, so this is
// cranked down. Override at runtime with EER_VIDEO_JITTER_LATENCY_MS to compare
// settings without rebuilding, 0 disables reordering entirely.
constexpr guint kDefaultJitterLatencyMs = 5;

// Every HTTP call is bounded. Without these a camera that answers ARP but not
// TCP parks the signalling thread on the kernel's connect timeout (~2 minutes),
// long past the point where the UI has given up and asked for a reconnect.
constexpr long kHttpConnectTimeoutMs = 3000;
constexpr long kOfferTimeoutMs = 8000;
constexpr long kTrickleTimeoutMs = 3000;
constexpr long kDeleteTimeoutMs = 1500;

constexpr int kMaxRedirects = 3;

// How long to let ICE gather before posting the offer anyway. With no STUN
// server configured only host candidates are gathered, so this completes in
// milliseconds; it only bites when EER_VIDEO_STUN_SERVER is set and the server
// is unreachable.
constexpr auto kIceGatheringTimeout = std::chrono::milliseconds(2500);

// How long after the answer the signalling thread stays alive to trickle late
// candidates. Bounded so stop() never waits long on a join.
constexpr auto kTricklePollInterval = std::chrono::milliseconds(250);
constexpr auto kTrickleWindow = std::chrono::seconds(10);

// Every codec the receive chain can decode, so the server can pick whatever it
// has. H264 stays first and keeps payload type 127, which is what our ROV
// cameras negotiate
constexpr const char* kDefaultVideoCaps =
    "application/x-rtp,media=video,encoding-name=H264,"
    "payload=127,clock-rate=90000;"
    "application/x-rtp,media=video,encoding-name=H265,"
    "payload=126,clock-rate=90000;"
    "application/x-rtp,media=video,encoding-name=VP8,"
    "payload=96,clock-rate=90000;"
    "application/x-rtp,media=video,encoding-name=VP9,"
    "payload=98,clock-rate=90000;"
    "application/x-rtp,media=video,encoding-name=AV1,"
    "payload=99,clock-rate=90000";

// Audio is received and dropped, so the only thing that matters here is that
// the server finds something it can match. Our ROV cameras send no audio
// at all and are happy with any of these; a server that does publish audio
// rejects the whole session, video included, with "codecs not supported by
// client" if the codec it has is not on the list, which is what offering PCMU
// alone used to cause.
constexpr const char* kDefaultAudioCaps =
    "application/x-rtp,media=audio,encoding-name=OPUS,"
    "payload=111,clock-rate=48000,encoding-params=(string)2;"
    "application/x-rtp,media=audio,encoding-name=PCMU,"
    "payload=0,clock-rate=8000;"
    "application/x-rtp,media=audio,encoding-name=PCMA,"
    "payload=8,clock-rate=8000";

// Audio caps of "none" drop the audio transceiver entirely, which is the
// sturdiest setting for a camera that has no audio: an m-line that is never
// offered cannot be mis-negotiated.
bool is_none_caps(const std::string& caps) {
    if (caps.size() != 4) {
        return false;
    }

    std::string lowered;
    lowered.reserve(caps.size());

    for (char c : caps) {
        lowered.push_back(
            static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    return lowered == "none";
}

// curl_easy_init() performs a lazy, non-thread-safe global init. Four cameras
// negotiate concurrently, each on its own signalling thread, so the global
// init has to happen exactly once up front.
void ensure_curl_global_init() {
    static std::once_flag once;
    std::call_once(once, [] {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
            std::cerr << "curl_global_init failed; WebRTC signalling may be "
                         "unreliable\n";
        }
    });
}

size_t curl_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* str = static_cast<std::string*>(userdata);
    str->append(ptr, size * nmemb);
    return size * nmemb;
}

size_t curl_header_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    const size_t total = size * nmemb;
    auto* location = static_cast<std::string*>(userdata);

    std::string header(ptr, total);
    const size_t colon = header.find(':');

    if (colon != std::string::npos) {
        std::string name = header.substr(0, colon);

        for (char& c : name) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        if (name == "location") {
            std::string value = header.substr(colon + 1);

            const size_t first = value.find_first_not_of(" \t");
            const size_t last = value.find_last_not_of(" \t\r\n");

            if (first != std::string::npos && last != std::string::npos) {
                *location = value.substr(first, last - first + 1);
            }
        }
    }

    return total;
}

// Aborts an in-flight transfer as soon as the stream is stopped, so a teardown
// never has to wait out the full request timeout.
int curl_xfer_cb(
    void* userdata,
    curl_off_t,
    curl_off_t,
    curl_off_t,
    curl_off_t) {
    auto* stopping = static_cast<std::atomic<bool>*>(userdata);
    return (stopping && stopping->load()) ? 1 : 0;
}

// Resolves a possibly-relative Location header against the request URL.
std::string resolve_url(const std::string& base, const std::string& ref) {
    if (ref.empty()) {
        return {};
    }

#if defined(LIBCURL_VERSION_NUM) && LIBCURL_VERSION_NUM >= 0x073E00
    CURLU* handle = curl_url();

    if (!handle) {
        return ref;
    }

    std::string result = ref;

    if (curl_url_set(handle, CURLUPART_URL, base.c_str(), 0) == CURLUE_OK &&
        curl_url_set(handle, CURLUPART_URL, ref.c_str(), 0) == CURLUE_OK) {
        char* full = nullptr;

        if (curl_url_get(handle, CURLUPART_URL, &full, 0) == CURLUE_OK &&
            full) {
            result = full;
            curl_free(full);
        }
    }

    curl_url_cleanup(handle);
    return result;
#else
    // Without the URL API only absolute Location headers can be honoured.
    if (ref.rfind("http://", 0) == 0 || ref.rfind("https://", 0) == 0) {
        return ref;
    }

    return base;
#endif
}

// Decoder preferences, hardware first, software last. A factory that is not
// installed is skipped, so the same table works across macOS, Windows and
// Linux. vtdec_hw (VideoToolbox) covers H264, H265, VP9 and AV1 on Apple
// hardware.
const char* const kH264Decoders[] = {
    "vtdec_hw", "vtdec", "d3d11h264dec", "nvh264dec", "vah264dec",
    "avdec_h264", nullptr};

const char* const kH265Decoders[] = {
    "vtdec_hw", "vtdec", "d3d11h265dec", "nvh265dec", "vah265dec",
    "avdec_h265", nullptr};

const char* const kVp8Decoders[] = {
    "nvvp8dec", "d3d11vp8dec", "vavp8dec", "vp8dec", "avdec_vp8", nullptr};

const char* const kVp9Decoders[] = {
    "vtdec_hw", "nvvp9dec", "d3d11vp9dec", "vavp9dec", "vp9dec", "avdec_vp9",
    nullptr};

const char* const kAv1Decoders[] = {
    "vtdec_hw", "nvav1dec", "d3d11av1dec", "vaav1dec", "dav1ddec", "av1dec",
    nullptr};

// RTP encoding-name -> the elements needed to turn it into raw video.
const VideoDecodeChain kVideoChains[] = {
    {"H264", "rtph264depay", "h264parse", kH264Decoders},
    {"H265", "rtph265depay", "h265parse", kH265Decoders},
    {"VP8", "rtpvp8depay", nullptr, kVp8Decoders},
    {"VP9", "rtpvp9depay", "vp9parse", kVp9Decoders},
    {"AV1", "rtpav1depay", "av1parse", kAv1Decoders},
};

const VideoDecodeChain* find_video_chain(const std::string& encoding_name) {
    for (const VideoDecodeChain& chain : kVideoChains) {
        if (encoding_name == chain.encoding_name) {
            return &chain;
        }
    }

    return nullptr;
}

GstElement* make_element(
    const char* factory,
    const std::string& label,
    const char* purpose) {
    GstElement* element = gst_element_factory_make(factory, nullptr);

    if (!element) {
        std::cerr << "[" << label << "] Missing GStreamer element '" << factory
                  << "' needed for " << purpose
                  << " (is the plugin installed?)\n";
    }

    return element;
}

// Copies one NV12 plane, collapsing the row loop when the source is tightly
// packed (the common case for the software and VideoToolbox decoders).
void copy_plane(
    std::vector<uint8_t>& dst,
    const uint8_t* src,
    int width,
    int rows,
    int stride) {
    dst.resize(static_cast<size_t>(width) * rows);

    if (stride == width) {
        std::memcpy(dst.data(), src, static_cast<size_t>(width) * rows);
        return;
    }

    for (int row = 0; row < rows; ++row) {
        std::memcpy(
            dst.data() + static_cast<size_t>(row) * width,
            src + static_cast<size_t>(row) * stride,
            static_cast<size_t>(width));
    }
}

// Reads EER_VIDEO_JITTER_LATENCY_MS, falling back to the default above. Applied
// to webrtcbin's internal jitterbuffers and to the one in the video chain, so a
// single value covers the whole receive path.
guint jitter_latency_ms() {
    const char* env = std::getenv("EER_VIDEO_JITTER_LATENCY_MS");

    if (!env || env[0] == '\0') {
        return kDefaultJitterLatencyMs;
    }

    char* end = nullptr;
    const long value = std::strtol(env, &end, 10);

    if (end == env || *end != '\0' || value < 0 || value > 10000) {
        std::cerr << "Ignoring invalid EER_VIDEO_JITTER_LATENCY_MS: " << env
                  << "\n";
        return kDefaultJitterLatencyMs;
    }

    return static_cast<guint>(value);
}

bool latency_logging_enabled() {
    const char* env = std::getenv("EER_VIDEO_LOG_LATENCY");
    return env && env[0] != '\0' && std::strcmp(env, "0") != 0;
}

const char* configured_stun_server() {
    const char* stun = std::getenv("EER_VIDEO_STUN_SERVER");

    if (stun && stun[0] != '\0') {
        return stun;
    }

    // Default: none. The ROV and the surface laptop share a subnet, so host
    // candidates are enough and a STUN lookup only adds startup latency (and a
    // dependency on an internet connection that is not there in the water).
    return nullptr;
}

std::string sdp_attribute(const GstSDPMessage* sdp, const char* key) {
    const guint medias = gst_sdp_message_medias_len(sdp);

    for (guint i = 0; i < medias; ++i) {
        const GstSDPMedia* media = gst_sdp_message_get_media(sdp, i);
        const gchar* value = gst_sdp_media_get_attribute_val(media, key);

        if (value) {
            return value;
        }
    }

    const gchar* value = gst_sdp_message_get_attribute_val(sdp, key);
    return value ? value : std::string();
}

} // namespace

WebRtcBinStream::WebRtcBinStream() = default;

WebRtcBinStream::~WebRtcBinStream() {
    stop();
}

bool WebRtcBinStream::start(const StreamConfig& cfg) {
    // A restart on a live object has to go through the full teardown first,
    // otherwise the old pipeline and signalling thread leak.
    stop();

    ensure_curl_global_init();

    stopping_ = false;
    failed_ = false;
    connected_ = false;
    latency_logged_ = false;
    cfg_ = cfg;

    video_linked_ = false;
    audio_linked_ = false;

    {
        std::lock_guard<std::mutex> lock(codec_mutex_);
        video_codec_.clear();
        audio_codec_.clear();
    }

    {
        std::lock_guard<std::mutex> lock(ice_mutex_);
        ice_candidates_.clear();
        ice_candidates_sent_ = 0;
        ice_gathering_done_ = false;
    }

    {
        std::lock_guard<std::mutex> lock(resource_mutex_);
        resource_url_.clear();
    }

    if (cfg_.url.empty()) {
        std::cerr << "[" << cfg_.label << "] Refusing to start with an empty "
                                          "WHEP URL\n";
        failed_ = true;
        return false;
    }

    token_ = std::make_shared<CallbackToken>();
    token_->self = this;

    pipeline_ = gst_pipeline_new(nullptr);
    webrtcbin_ = make_element("webrtcbin", cfg_.label, "WebRTC signalling");

    if (!pipeline_ || !webrtcbin_) {
        std::cerr << "[" << cfg_.label
                  << "] Failed to create pipeline or webrtcbin\n";
        failed_ = true;
        stop();
        return false;
    }

    // "latency" has to be set before the session is negotiated: it is what the
    // internal jitterbuffers are created with.
    g_object_set(
        webrtcbin_,
        "bundle-policy",
        GST_WEBRTC_BUNDLE_POLICY_MAX_BUNDLE,
        "stun-server",
        configured_stun_server(),
        "latency",
        jitter_latency_ms(),
        nullptr);

    gst_bin_add(GST_BIN(pipeline_), webrtcbin_);

    g_signal_connect(
        webrtcbin_,
        "on-ice-candidate",
        G_CALLBACK(+[](GstElement*, guint idx, gchararray cand, gpointer d) {
            static_cast<WebRtcBinStream*>(d)->on_ice_candidate(idx, cand);
        }),
        this);

    g_signal_connect(
        webrtcbin_,
        "pad-added",
        G_CALLBACK(+[](GstElement*, GstPad* pad, gpointer d) {
            static_cast<WebRtcBinStream*>(d)->on_pad_added(pad);
        }),
        this);

    // Watching the transport directly turns "no frames for three seconds" into
    // "ICE failed", which the UI can act on immediately instead of waiting out
    // a frame timeout.
    g_signal_connect(
        webrtcbin_,
        "notify::ice-gathering-state",
        G_CALLBACK(+[](GObject*, GParamSpec*, gpointer d) {
            static_cast<WebRtcBinStream*>(d)->on_ice_gathering_state_changed();
        }),
        this);

    g_signal_connect(
        webrtcbin_,
        "notify::ice-connection-state",
        G_CALLBACK(+[](GObject*, GParamSpec*, gpointer d) {
            static_cast<WebRtcBinStream*>(d)->on_ice_connection_state_changed();
        }),
        this);

    g_signal_connect(
        webrtcbin_,
        "notify::connection-state",
        G_CALLBACK(+[](GObject*, GParamSpec*, gpointer d) {
            static_cast<WebRtcBinStream*>(d)->on_connection_state_changed();
        }),
        this);

    // Video transceiver.
    {
        const std::string& caps_string =
            cfg_.video_caps.empty() ? kDefaultVideoCaps : cfg_.video_caps;

        GstCaps* caps = gst_caps_from_string(caps_string.c_str());
        if (!caps) {
            std::cerr << "[" << cfg_.label
                      << "] Invalid video caps: " << caps_string << "\n";
            failed_ = true;
            stop();
            return false;
        }

        GstWebRTCRTPTransceiver* transceiver = nullptr;
        g_signal_emit_by_name(
            webrtcbin_,
            "add-transceiver",
            GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_RECVONLY,
            caps,
            &transceiver);

        gst_caps_unref(caps);

        if (!transceiver) {
            std::cerr << "[" << cfg_.label
                      << "] Failed to add video transceiver\n";
            failed_ = true;
            stop();
            return false;
        }

        gst_object_unref(transceiver);
    }

    // Audio transceiver, unless it has been turned off outright.
    if (!is_none_caps(cfg_.audio_caps)) {
        const std::string& caps_string =
            cfg_.audio_caps.empty() ? kDefaultAudioCaps : cfg_.audio_caps;

        GstCaps* caps = gst_caps_from_string(caps_string.c_str());
        if (!caps) {
            std::cerr << "[" << cfg_.label
                      << "] Invalid audio caps: " << caps_string << "\n";
            failed_ = true;
            stop();
            return false;
        }

        GstWebRTCRTPTransceiver* transceiver = nullptr;
        g_signal_emit_by_name(
            webrtcbin_,
            "add-transceiver",
            GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_RECVONLY,
            caps,
            &transceiver);

        gst_caps_unref(caps);

        if (!transceiver) {
            std::cerr << "[" << cfg_.label
                      << "] Failed to add audio transceiver\n";
            failed_ = true;
            stop();
            return false;
        }

        gst_object_unref(transceiver);
    }

    GstBus* bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline_));
    gst_bus_set_sync_handler(bus, on_bus_sync, this, nullptr);
    gst_object_unref(bus);

    GstStateChangeReturn state_ret =
        gst_element_set_state(pipeline_, GST_STATE_PLAYING);

    if (state_ret == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "[" << cfg_.label
                  << "] Failed to set webrtcbin pipeline to PLAYING\n";
        failed_ = true;
        stop();
        return false;
    }

    // The promise carries a token rather than `this`: if the offer completes
    // after stop() has run, the callback sees a cleared token instead of a
    // dangling stream.
    GstPromise* promise = gst_promise_new_with_change_func(
        +[](GstPromise* p, gpointer d) {
            auto* token = static_cast<std::shared_ptr<CallbackToken>*>(d);
            std::lock_guard<std::mutex> lock((*token)->mutex);

            if ((*token)->self) {
                (*token)->self->on_offer_created(p);
            } else {
                gst_promise_unref(p);
            }
        },
        new std::shared_ptr<CallbackToken>(token_),
        +[](gpointer d) {
            delete static_cast<std::shared_ptr<CallbackToken>*>(d);
        });

    g_signal_emit_by_name(webrtcbin_, "create-offer", nullptr, promise);

    return true;
}

void WebRtcBinStream::stop() {
    const bool was_running = pipeline_ != nullptr || signalling_thread_.joinable();

    stopping_ = true;
    connected_ = false;

    // Wake the signalling thread out of any wait it is sitting in. In-flight
    // HTTP calls abort through curl_xfer_cb.
    ice_cv_.notify_all();

    // Stop new asynchronous callbacks from entering this object.
    if (token_) {
        std::lock_guard<std::mutex> lock(token_->mutex);
        token_->self = nullptr;
    }

    {
        std::lock_guard<std::mutex> lock(signalling_mutex_);

        if (signalling_thread_.joinable()) {
            signalling_thread_.join();
        }
    }

    if (pipeline_) {
        GstBus* bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline_));
        gst_bus_set_sync_handler(bus, nullptr, nullptr, nullptr);
        gst_object_unref(bus);

        if (webrtcbin_) {
            g_signal_handlers_disconnect_by_data(webrtcbin_, this);
        }

        gst_element_set_state(pipeline_, GST_STATE_NULL);
        gst_object_unref(pipeline_);

        pipeline_ = nullptr;
        webrtcbin_ = nullptr;
    }

    // Tell the server the session is over. Without this every reconnect leaves
    // a stale WHEP session (and its encoder resources) behind on the server.
    if (was_running) {
        delete_whep_resource();
    }

    token_.reset();

    video_linked_ = false;
    audio_linked_ = false;
}

bool WebRtcBinStream::poll_frame(FrameData& out) {
    std::lock_guard<std::mutex> lock(frame_mutex_);

    if (!pending_frame_.dirty) {
        return false;
    }

    // Swap rather than copy: the caller's buffers go back to the capture side
    // for reuse, so a steady stream settles into two allocations total.
    out.y_plane.swap(pending_frame_.y_plane);
    out.uv_plane.swap(pending_frame_.uv_plane);
    out.width = pending_frame_.width;
    out.height = pending_frame_.height;
    out.dirty = true;

    pending_frame_.dirty = false;
    return true;
}

bool WebRtcBinStream::failed() const {
    return failed_.load();
}

bool WebRtcBinStream::connected() const {
    return connected_.load();
}

std::string WebRtcBinStream::codec_summary() const {
    std::lock_guard<std::mutex> lock(codec_mutex_);

    if (video_codec_.empty()) {
        return audio_codec_;
    }

    if (audio_codec_.empty()) {
        return video_codec_;
    }

    return video_codec_ + " + " + audio_codec_;
}

// Reports what the receive path is actually buffering, once per session, when
// EER_VIDEO_LOG_LATENCY is set. This is the pipeline's own accounting, it does
// not include the camera's encoder, the server, or the display, but it does
// say whether a jitter setting is doing anything at all.
void WebRtcBinStream::log_pipeline_latency() {
    if (latency_logged_.exchange(true) || !latency_logging_enabled()) {
        return;
    }

    if (!pipeline_) {
        return;
    }

    GstQuery* query = gst_query_new_latency();

    if (!query) {
        return;
    }

    if (gst_element_query(pipeline_, query)) {
        gboolean live = FALSE;
        GstClockTime min = GST_CLOCK_TIME_NONE;
        GstClockTime max = GST_CLOCK_TIME_NONE;

        gst_query_parse_latency(query, &live, &min, &max);

        std::cout << "[" << cfg_.label << "] pipeline latency: min="
                  << (GST_CLOCK_TIME_IS_VALID(min) ? min / GST_MSECOND : 0)
                  << "ms max=";

        if (GST_CLOCK_TIME_IS_VALID(max)) {
            std::cout << max / GST_MSECOND << "ms";
        } else {
            std::cout << "unlimited";
        }

        std::cout << " live=" << (live ? "yes" : "no")
                  << " jitter-setting=" << jitter_latency_ms() << "ms\n";
    }

    gst_query_unref(query);
}

void WebRtcBinStream::request_keyframe() {
    if (stopping_ || !pipeline_) {
        return;
    }

    // Travels upstream to webrtcbin, which turns it into an RTCP PLI.
    gst_element_send_event(
        pipeline_,
        gst_video_event_new_upstream_force_key_unit(GST_CLOCK_TIME_NONE, TRUE, 0));
}

GstBusSyncReply WebRtcBinStream::on_bus_sync(
    GstBus*,
    GstMessage* msg,
    gpointer data) {
    auto* self = static_cast<WebRtcBinStream*>(data);

    if (!self) {
        return GST_BUS_PASS;
    }

    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR: {
            GError* err = nullptr;
            gchar* debug_info = nullptr;

            gst_message_parse_error(msg, &err, &debug_info);

            std::cerr << "[" << self->cfg_.label << "] GStreamer error: "
                      << (err ? err->message : "unknown") << "\n";

            if (debug_info) {
                std::cerr << "  Debug: " << debug_info << "\n";
            }

            self->failed_ = true;

            g_clear_error(&err);
            g_free(debug_info);
            break;
        }

        case GST_MESSAGE_EOS:
            std::cerr << "[" << self->cfg_.label << "] End of stream\n";
            self->failed_ = true;
            break;

        default:
            break;
    }

    return GST_BUS_PASS;
}

// ---------------------------------------------------------------------------
// Signalling
// ---------------------------------------------------------------------------

void WebRtcBinStream::on_offer_created(GstPromise* promise) {
    if (stopping_ || !webrtcbin_) {
        gst_promise_unref(promise);
        return;
    }

    const GstStructure* reply = gst_promise_get_reply(promise);

    if (!reply) {
        std::cerr << "[" << cfg_.label
                  << "] Failed to create WebRTC offer: empty promise reply\n";
        failed_ = true;
        gst_promise_unref(promise);
        return;
    }

    GstWebRTCSessionDescription* offer = nullptr;
    gst_structure_get(
        reply,
        "offer",
        GST_TYPE_WEBRTC_SESSION_DESCRIPTION,
        &offer,
        nullptr);

    gst_promise_unref(promise);

    if (!offer) {
        std::cerr << "[" << cfg_.label << "] Failed to create WebRTC offer\n";
        failed_ = true;
        return;
    }

    // Setting the local description starts ICE gathering; the candidates it
    // produces are collected by on_ice_candidate while the signalling thread
    // waits below.
    GstPromise* local_promise = gst_promise_new();
    g_signal_emit_by_name(
        webrtcbin_,
        "set-local-description",
        offer,
        local_promise);
    gst_promise_interrupt(local_promise);
    gst_promise_unref(local_promise);

    gchar* offer_str = gst_sdp_message_as_text(offer->sdp);
    gst_webrtc_session_description_free(offer);

    if (!offer_str) {
        std::cerr << "[" << cfg_.label
                  << "] Failed to serialize WebRTC offer SDP\n";
        failed_ = true;
        return;
    }

    std::string offer_sdp(offer_str);
    g_free(offer_str);

    // The HTTP exchange runs off the webrtcbin streaming thread. Blocking here
    // would stall the element itself, including the teardown that a failed
    // exchange is supposed to trigger.
    std::lock_guard<std::mutex> lock(signalling_mutex_);

    if (stopping_ || signalling_thread_.joinable()) {
        return;
    }

    signalling_thread_ =
        std::thread(&WebRtcBinStream::run_signalling, this, std::move(offer_sdp));
}

void WebRtcBinStream::run_signalling(std::string offer_sdp) {
    wait_for_ice_gathering();

    if (stopping_) {
        return;
    }

    // Post the offer with the candidates already in it. Servers that do not
    // implement trickle ICE (or that only answer with their own candidates)
    // then have everything they need in a single round trip.
    const std::string full_offer = offer_with_candidates(offer_sdp);

    std::string answer_sdp;

    if (!whep_exchange(full_offer, answer_sdp)) {
        if (!stopping_) {
            std::cerr << "[" << cfg_.label << "] WHEP exchange failed\n";
            failed_ = true;
        }
        return;
    }

    if (stopping_) {
        return;
    }

    if (!apply_answer(answer_sdp)) {
        failed_ = true;
        return;
    }

    trickle_candidates(offer_sdp);
}

void WebRtcBinStream::wait_for_ice_gathering() {
    std::unique_lock<std::mutex> lock(ice_mutex_);

    ice_cv_.wait_for(lock, kIceGatheringTimeout, [this] {
        return ice_gathering_done_ || stopping_.load();
    });

    if (!ice_gathering_done_ && !stopping_) {
        std::cerr << "[" << cfg_.label
                  << "] ICE gathering did not complete in time; posting offer "
                     "with "
                  << ice_candidates_.size() << " candidate(s)\n";
    }
}

std::string WebRtcBinStream::offer_with_candidates(
    const std::string& offer_sdp) {
    GstSDPMessage* sdp = nullptr;

    if (gst_sdp_message_new(&sdp) != GST_SDP_OK || !sdp) {
        return offer_sdp;
    }

    if (gst_sdp_message_parse_buffer(
            reinterpret_cast<const guint8*>(offer_sdp.data()),
            static_cast<guint>(offer_sdp.size()),
            sdp) != GST_SDP_OK) {
        gst_sdp_message_free(sdp);
        return offer_sdp;
    }

    const guint media_count = gst_sdp_message_medias_len(sdp);

    {
        std::lock_guard<std::mutex> lock(ice_mutex_);

        for (const IceCandidate& candidate : ice_candidates_) {
            if (candidate.mline_index >= media_count) {
                continue;
            }

            // Mutating a media section of an SDP we own; the getter is const
            // only because it is also used for inspection.
            GstSDPMedia* media = const_cast<GstSDPMedia*>(
                gst_sdp_message_get_media(sdp, candidate.mline_index));

            if (!media) {
                continue;
            }

            // SDP attributes carry the value without the "candidate:" key.
            std::string value = candidate.candidate;
            const std::string prefix = "candidate:";

            if (value.rfind(prefix, 0) == 0) {
                value = value.substr(prefix.size());
            }

            if (value.empty()) {
                continue;
            }

            gst_sdp_media_add_attribute(media, "candidate", value.c_str());
        }

        ice_candidates_sent_ = ice_candidates_.size();

        if (ice_gathering_done_) {
            for (guint i = 0; i < media_count; ++i) {
                GstSDPMedia* media =
                    const_cast<GstSDPMedia*>(gst_sdp_message_get_media(sdp, i));

                if (media) {
                    gst_sdp_media_add_attribute(
                        media, "end-of-candidates", nullptr);
                }
            }
        }
    }

    gchar* text = gst_sdp_message_as_text(sdp);
    gst_sdp_message_free(sdp);

    if (!text) {
        return offer_sdp;
    }

    std::string result(text);
    g_free(text);
    return result;
}

bool WebRtcBinStream::apply_answer(const std::string& answer_sdp) {
    GstSDPMessage* sdp_msg = nullptr;

    if (gst_sdp_message_new(&sdp_msg) != GST_SDP_OK || !sdp_msg) {
        std::cerr << "[" << cfg_.label << "] Failed to allocate SDP message\n";
        return false;
    }

    const GstSDPResult parse_ret = gst_sdp_message_parse_buffer(
        reinterpret_cast<const guint8*>(answer_sdp.data()),
        static_cast<guint>(answer_sdp.size()),
        sdp_msg);

    if (parse_ret != GST_SDP_OK) {
        std::cerr << "[" << cfg_.label
                  << "] Failed to parse WHEP answer SDP\n";
        gst_sdp_message_free(sdp_msg);
        return false;
    }

    if (gst_sdp_message_medias_len(sdp_msg) == 0) {
        std::cerr << "[" << cfg_.label
                  << "] WHEP answer contained no media sections\n";
        gst_sdp_message_free(sdp_msg);
        return false;
    }

    GstWebRTCSessionDescription* answer =
        gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_ANSWER, sdp_msg);

    if (!answer) {
        std::cerr << "[" << cfg_.label
                  << "] Failed to create WebRTC answer description\n";
        gst_sdp_message_free(sdp_msg);
        return false;
    }

    // set-remote-description is thread safe: webrtcbin marshals it onto its own
    // thread, so this does not block the signalling thread on the pipeline.
    GstPromise* remote_promise = gst_promise_new();
    g_signal_emit_by_name(
        webrtcbin_,
        "set-remote-description",
        answer,
        remote_promise);
    gst_promise_interrupt(remote_promise);
    gst_promise_unref(remote_promise);

    gst_webrtc_session_description_free(answer);
    return true;
}

// Trickles any candidate gathered after the offer went out. Servers that do not
// implement PATCH answer 405/501; that is not an error, it just ends trickling.
void WebRtcBinStream::trickle_candidates(const std::string& offer_sdp) {
    std::string resource;

    {
        std::lock_guard<std::mutex> lock(resource_mutex_);
        resource = resource_url_;
    }

    if (resource.empty()) {
        return;
    }

    GstSDPMessage* sdp = nullptr;

    if (gst_sdp_message_new(&sdp) != GST_SDP_OK || !sdp) {
        return;
    }

    if (gst_sdp_message_parse_buffer(
            reinterpret_cast<const guint8*>(offer_sdp.data()),
            static_cast<guint>(offer_sdp.size()),
            sdp) != GST_SDP_OK) {
        gst_sdp_message_free(sdp);
        return;
    }

    const std::string ufrag = sdp_attribute(sdp, "ice-ufrag");
    const std::string pwd = sdp_attribute(sdp, "ice-pwd");
    const guint media_count = gst_sdp_message_medias_len(sdp);

    const auto deadline = std::chrono::steady_clock::now() + kTrickleWindow;

    while (!stopping_ && std::chrono::steady_clock::now() < deadline) {
        std::vector<IceCandidate> pending;
        bool done = false;

        {
            std::unique_lock<std::mutex> lock(ice_mutex_);

            ice_cv_.wait_for(lock, kTricklePollInterval, [this] {
                return stopping_.load() ||
                       ice_candidates_sent_ < ice_candidates_.size();
            });

            pending.assign(
                ice_candidates_.begin() +
                    static_cast<std::ptrdiff_t>(ice_candidates_sent_),
                ice_candidates_.end());

            ice_candidates_sent_ = ice_candidates_.size();
            done = ice_gathering_done_;
        }

        if (stopping_ || pending.empty()) {
            if (done) {
                break;
            }
            continue;
        }

        std::string fragment;

        if (!ufrag.empty()) {
            fragment += "a=ice-ufrag:" + ufrag + "\r\n";
        }

        if (!pwd.empty()) {
            fragment += "a=ice-pwd:" + pwd + "\r\n";
        }

        for (const IceCandidate& candidate : pending) {
            if (candidate.mline_index >= media_count) {
                continue;
            }

            const GstSDPMedia* media =
                gst_sdp_message_get_media(sdp, candidate.mline_index);

            if (!media) {
                continue;
            }

            const gchar* mid = gst_sdp_media_get_attribute_val(media, "mid");

            fragment += "m=";
            fragment += gst_sdp_media_get_media(media);
            fragment += " 9 RTP/AVP 0\r\n";

            if (mid) {
                fragment += "a=mid:";
                fragment += mid;
                fragment += "\r\n";
            }

            fragment += "a=" + candidate.candidate + "\r\n";
        }

        if (fragment.empty()) {
            continue;
        }

        HttpResponse response;

        const bool ok = http_request(
            resource,
            "PATCH",
            "application/trickle-ice-sdpfrag",
            fragment,
            kTrickleTimeoutMs,
            true,
            response);

        if (!ok || response.status < 200 || response.status >= 300) {
            std::cerr << "[" << cfg_.label
                      << "] Server did not accept trickled ICE candidates "
                         "(HTTP "
                      << response.status << "); continuing without trickle\n";
            break;
        }

        if (done) {
            break;
        }
    }

    gst_sdp_message_free(sdp);
}

void WebRtcBinStream::delete_whep_resource() {
    std::string resource;

    {
        std::lock_guard<std::mutex> lock(resource_mutex_);
        resource = std::move(resource_url_);
        resource_url_.clear();
    }

    if (resource.empty()) {
        return;
    }

    HttpResponse response;

    // Deliberately not abortable: stopping_ is already set by the time we get
    // here, and this is the request that releases the session server-side.
    http_request(
        resource,
        "DELETE",
        nullptr,
        std::string(),
        kDeleteTimeoutMs,
        false,
        response);
}

bool WebRtcBinStream::whep_exchange(
    const std::string& offer_sdp,
    std::string& out_answer_sdp) {
    std::string url = cfg_.url;

    for (int redirect = 0; redirect <= kMaxRedirects; ++redirect) {
        HttpResponse response;

        if (!http_request(
                url,
                "POST",
                "application/sdp",
                offer_sdp,
                kOfferTimeoutMs,
                true,
                response)) {
            return false;
        }

        // WHEP endpoints are commonly published behind a redirect (a missing
        // trailing slash, or an edge that forwards to the media server).
        if (response.status >= 300 && response.status < 400) {
            if (response.location.empty()) {
                std::cerr << "[" << cfg_.label << "] WHEP HTTP "
                          << response.status
                          << " redirect without a Location header\n";
                return false;
            }

            const std::string next = resolve_url(url, response.location);

            if (next.empty() || next == url) {
                std::cerr << "[" << cfg_.label
                          << "] WHEP redirect loop at " << url << "\n";
                return false;
            }

            url = next;
            continue;
        }

        if (response.status != 200 && response.status != 201) {
            std::cerr << "[" << cfg_.label << "] WHEP HTTP " << response.status
                      << " from " << url << "\n"
                      << response.body << "\n";
            return false;
        }

        // Record the session resource before anything else can fail: the
        // server has already allocated it, so it has to be deleted on the way
        // out even if the answer itself turns out to be unusable.
        if (!response.location.empty()) {
            std::lock_guard<std::mutex> lock(resource_mutex_);
            resource_url_ = resolve_url(url, response.location);
        }

        if (response.body.empty()) {
            std::cerr << "[" << cfg_.label
                      << "] WHEP response body was empty\n";
            return false;
        }

        if (response.body.rfind("v=", 0) != 0) {
            std::cerr << "[" << cfg_.label
                      << "] WHEP response was not an SDP answer\n";
            return false;
        }

        out_answer_sdp = std::move(response.body);
        return true;
    }

    std::cerr << "[" << cfg_.label << "] Too many WHEP redirects\n";
    return false;
}

bool WebRtcBinStream::http_request(
    const std::string& url,
    const char* method,
    const char* content_type,
    const std::string& body,
    long timeout_ms,
    bool abortable,
    HttpResponse& out) {
    CURL* curl = curl_easy_init();

    if (!curl) {
        std::cerr << "[" << cfg_.label << "] curl_easy_init failed\n";
        return false;
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/sdp");

    if (content_type) {
        const std::string header = std::string("Content-Type: ") + content_type;
        headers = curl_slist_append(headers, header.c_str());
    }

    // Some proxies stall on an Expect: 100-continue that never gets answered.
    headers = curl_slist_append(headers, "Expect:");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    if (std::strcmp(method, "POST") == 0) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
    } else {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
    }

    if (!body.empty()) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(
            curl,
            CURLOPT_POSTFIELDSIZE,
            static_cast<long>(body.size()));
    }

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out.body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curl_header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &out.location);

    // Redirects are followed by hand so the Location of a 201 (the session
    // resource) is never confused with the Location of a 3xx.
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);

    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, kHttpConnectTimeoutMs);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);

    // Required for timeouts to be safe on a non-main thread: without it libcurl
    // may use SIGALRM for the DNS timeout.
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1L);

    if (abortable) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curl_xfer_cb);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &stopping_);
    }

    const CURLcode res = curl_easy_perform(curl);

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &out.status);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        if (res == CURLE_ABORTED_BY_CALLBACK) {
            // Expected during teardown; not worth a log line.
            return false;
        }

        std::cerr << "[" << cfg_.label << "] " << method << " " << url
                  << " failed: " << curl_easy_strerror(res) << "\n";
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Transport state
// ---------------------------------------------------------------------------

void WebRtcBinStream::on_ice_candidate(
    guint mline_index,
    const gchar* candidate) {
    if (stopping_ || !candidate) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(ice_mutex_);
        ice_candidates_.push_back({mline_index, candidate});
    }

    ice_cv_.notify_all();
}

void WebRtcBinStream::on_ice_gathering_state_changed() {
    if (!webrtcbin_) {
        return;
    }

    GstWebRTCICEGatheringState state = GST_WEBRTC_ICE_GATHERING_STATE_NEW;
    g_object_get(webrtcbin_, "ice-gathering-state", &state, nullptr);

    if (state != GST_WEBRTC_ICE_GATHERING_STATE_COMPLETE) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(ice_mutex_);
        ice_gathering_done_ = true;
    }

    ice_cv_.notify_all();
}

void WebRtcBinStream::on_ice_connection_state_changed() {
    if (!webrtcbin_ || stopping_) {
        return;
    }

    GstWebRTCICEConnectionState state = GST_WEBRTC_ICE_CONNECTION_STATE_NEW;
    g_object_get(webrtcbin_, "ice-connection-state", &state, nullptr);

    switch (state) {
        case GST_WEBRTC_ICE_CONNECTION_STATE_FAILED:
            std::cerr << "[" << cfg_.label << "] ICE connection failed\n";
            connected_ = false;
            failed_ = true;
            break;

        case GST_WEBRTC_ICE_CONNECTION_STATE_CLOSED:
            connected_ = false;
            failed_ = true;
            break;

        case GST_WEBRTC_ICE_CONNECTION_STATE_DISCONNECTED:
            // Often transient. Let the frame timeout decide.
            connected_ = false;
            break;

        case GST_WEBRTC_ICE_CONNECTION_STATE_CONNECTED:
        case GST_WEBRTC_ICE_CONNECTION_STATE_COMPLETED:
            connected_ = true;
            break;

        default:
            break;
    }
}

void WebRtcBinStream::on_connection_state_changed() {
    if (!webrtcbin_ || stopping_) {
        return;
    }

    GstWebRTCPeerConnectionState state = GST_WEBRTC_PEER_CONNECTION_STATE_NEW;
    g_object_get(webrtcbin_, "connection-state", &state, nullptr);

    switch (state) {
        case GST_WEBRTC_PEER_CONNECTION_STATE_FAILED:
            std::cerr << "[" << cfg_.label << "] Peer connection failed\n";
            connected_ = false;
            failed_ = true;
            break;

        case GST_WEBRTC_PEER_CONNECTION_STATE_CLOSED:
            connected_ = false;
            failed_ = true;
            break;

        case GST_WEBRTC_PEER_CONNECTION_STATE_CONNECTED:
            connected_ = true;
            break;

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Media
// ---------------------------------------------------------------------------

void WebRtcBinStream::on_pad_added(GstPad* pad) {
    if (stopping_ || !pipeline_) {
        return;
    }

    GstCaps* caps = gst_pad_get_current_caps(pad);

    if (!caps) {
        caps = gst_pad_query_caps(pad, nullptr);
    }

    if (!caps || gst_caps_is_empty(caps)) {
        if (caps) {
            gst_caps_unref(caps);
        }

        std::cerr << "[" << cfg_.label
                  << "] webrtcbin pad added without usable caps\n";
        return;
    }

    GstStructure* structure = gst_caps_get_structure(caps, 0);
    const gchar* media = gst_structure_get_string(structure, "media");

    if (!media) {
        gst_caps_unref(caps);
        return;
    }

    const bool is_video = std::strcmp(media, "video") == 0;
    const bool is_audio = std::strcmp(media, "audio") == 0;

    // Which codec the server actually chose out of everything we offered.
    const gchar* encoding = gst_structure_get_string(structure, "encoding-name");
    std::string encoding_name = encoding ? encoding : "";

    for (char& c : encoding_name) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }

    gst_caps_unref(caps);

    if (is_video && !video_linked_) {
        video_linked_ = true;

        {
            std::lock_guard<std::mutex> lock(codec_mutex_);
            video_codec_ = encoding_name;
        }

        link_video(pad, encoding_name);
    } else if (is_audio && !audio_linked_) {
        audio_linked_ = true;

        {
            std::lock_guard<std::mutex> lock(codec_mutex_);
            audio_codec_ = encoding_name;
        }

        link_audio(pad);
    }
}

// Builds the receive chain for whichever codec the server picked out of the
// offer. Explicit rather than decodebin-driven on purpose: vtdec_hw advertises
// video/x-raw(memory:GLMemory) ahead of plain video/x-raw, so decodebin
// autoplugs a GL path, which on macOS wants an NSApplication on the main
// thread and pushed first-frame latency from under a second to seven. Linking
// the decoder straight to videoconvert negotiates system memory instead.
void WebRtcBinStream::link_video(GstPad* pad, const std::string& encoding_name) {
    const VideoDecodeChain* recipe = find_video_chain(encoding_name);

    if (!recipe) {
        std::cerr << "[" << cfg_.label << "] Negotiated video codec '"
                  << encoding_name << "' has no receive chain\n";
        failed_ = true;
        return;
    }

    GstElement* jitterbuffer =
        make_element("rtpjitterbuffer", cfg_.label, "video receive");
    GstElement* depay = make_element(recipe->depay, cfg_.label, "video receive");

    // Not every codec needs a parser (VP8 has none).
    GstElement* parse = nullptr;
    bool parse_ok = true;

    if (recipe->parse) {
        parse = make_element(recipe->parse, cfg_.label, "video receive");
        parse_ok = parse != nullptr;
    }

    GstElement* decode = nullptr;
    for (const char* const* factory = recipe->decoders; *factory; ++factory) {
        decode = gst_element_factory_make(*factory, nullptr);

        if (decode) {
            break;
        }
    }

    if (!decode) {
        std::cerr << "[" << cfg_.label << "] No decoder available for "
                  << encoding_name << "\n";
    }

    GstElement* convert =
        make_element("videoconvert", cfg_.label, "video receive");
    GstElement* capsfilter =
        make_element("capsfilter", cfg_.label, "video receive");
    GstElement* queue = make_element("queue", cfg_.label, "video receive");
    GstElement* appsink = make_element("appsink", cfg_.label, "video receive");

    GstElement* const chain[] = {
        jitterbuffer, depay, parse, decode, convert, capsfilter, queue, appsink};

    if (!jitterbuffer || !depay || !parse_ok || !decode || !convert ||
        !capsfilter || !queue || !appsink) {
        // Nothing has been added to the bin yet, so the partially built chain
        // has to be released here or it leaks for the life of the process.
        for (GstElement* created : chain) {
            if (created) {
                gst_object_unref(created);
            }
        }

        std::cerr << "[" << cfg_.label
                  << "] Failed to create video receive chain for "
                  << encoding_name << "\n";
        failed_ = true;
        return;
    }

    // This sits after webrtcbin, whose src pads are already jitterbuffered and
    // reordered by its internal rtpbin, so it is largely redundant, kept
    // because it costs no measurable latency and do-lost gives the decoder an
    // explicit signal that packets went missing, instead of silently feeding it
    // a corrupt bitstream.
    g_object_set(
        jitterbuffer,
        "latency",
        jitter_latency_ms(),
        "do-lost",
        TRUE,
        nullptr);

    g_object_set(
        queue,
        "max-size-buffers",
        kVideoQueueBuffers,
        "max-size-bytes",
        (guint)0,
        "max-size-time",
        (guint64)0,
        "leaky",
        kQueueLeakyMode,
        nullptr);

    GstCaps* nv12 = gst_caps_from_string("video/x-raw,format=NV12");
    g_object_set(capsfilter, "caps", nv12, nullptr);
    gst_caps_unref(nv12);

    g_object_set(
        appsink,
        "emit-signals",
        TRUE,
        "sync",
        FALSE,
        "max-buffers",
        kAppSinkBuffers,
        "drop",
        TRUE,
        nullptr);

    g_signal_connect(appsink, "new-sample", G_CALLBACK(on_new_sample), this);

    // parse is optional, so the bin add and the link are built as a list.
    std::vector<GstElement*> linked;
    linked.reserve(8);

    linked.push_back(jitterbuffer);
    linked.push_back(depay);

    if (parse) {
        linked.push_back(parse);
    }

    linked.push_back(decode);
    linked.push_back(convert);
    linked.push_back(capsfilter);
    linked.push_back(queue);
    linked.push_back(appsink);

    for (GstElement* element : linked) {
        gst_bin_add(GST_BIN(pipeline_), element);
    }

    for (size_t i = 0; i + 1 < linked.size(); ++i) {
        if (gst_element_link(linked[i], linked[i + 1])) {
            continue;
        }

        std::cerr << "[" << cfg_.label
                  << "] Failed to link video receive chain at "
                  << GST_ELEMENT_NAME(linked[i]) << " -> "
                  << GST_ELEMENT_NAME(linked[i + 1]) << "\n";
        failed_ = true;
        return;
    }

    for (GstElement* element : linked) {
        gst_element_sync_state_with_parent(element);
    }

    GstPad* sink_pad = gst_element_get_static_pad(jitterbuffer, "sink");
    const GstPadLinkReturn ret = gst_pad_link(pad, sink_pad);
    gst_object_unref(sink_pad);

    if (ret != GST_PAD_LINK_OK) {
        std::cerr << "[" << cfg_.label
                  << "] Failed to link WebRTC video pad: " << ret << "\n";
        failed_ = true;
        return;
    }

    std::cout << "[" << cfg_.label << "] Receiving " << encoding_name
              << ", decoding with " << GST_ELEMENT_NAME(decode) << "\n";

    // No keyframe request from here: this runs on webrtcbin's streaming thread
    // while the pad is still being added, and an upstream event sent back into
    // that same element is a good way to deadlock it. Camera asks for the IDR
    // from the render thread once the session is up.
}

// Audio is negotiated only so that servers which insist on an audio track will
// talk to us; nothing plays it. Dropping the RTP straight into a fakesink keeps
// this branch codec-agnostic, the old depay/decode chain assumed Opus, so a
// PCMU or PCMA track would have failed to link, and saves decoding audio for
// the sole purpose of throwing it away.
void WebRtcBinStream::link_audio(GstPad* pad) {
    GstElement* queue = make_element("queue", cfg_.label, "audio receive");
    GstElement* sink = make_element("fakesink", cfg_.label, "audio receive");

    GstElement* const chain[] = {queue, sink};

    for (GstElement* element : chain) {
        if (element) {
            continue;
        }

        for (GstElement* created : chain) {
            if (created) {
                gst_object_unref(created);
            }
        }

        // Audio is decoded into a fakesink today, so a missing element costs
        // nothing visible, do not fail the whole stream over it.
        std::cerr << "[" << cfg_.label
                  << "] Failed to create audio receive chain; ignoring audio\n";
        return;
    }

    g_object_set(
        queue,
        "max-size-buffers",
        kAudioQueueBuffers,
        "max-size-bytes",
        (guint)0,
        "max-size-time",
        (guint64)0,
        "leaky",
        kQueueLeakyMode,
        nullptr);

    g_object_set(sink, "sync", FALSE, "async", FALSE, nullptr);

    gst_bin_add_many(GST_BIN(pipeline_), queue, sink, nullptr);

    if (!gst_element_link(queue, sink)) {
        std::cerr << "[" << cfg_.label
                  << "] Failed to link audio receive chain\n";
        return;
    }

    for (GstElement* element : chain) {
        gst_element_sync_state_with_parent(element);
    }

    GstPad* sink_pad = gst_element_get_static_pad(queue, "sink");
    const GstPadLinkReturn ret = gst_pad_link(pad, sink_pad);
    gst_object_unref(sink_pad);

    if (ret != GST_PAD_LINK_OK) {
        std::cerr << "[" << cfg_.label
                  << "] Failed to link WebRTC audio pad: " << ret << "\n";
    }
}

GstFlowReturn WebRtcBinStream::on_new_sample(GstElement* sink, gpointer data) {
    auto* self = static_cast<WebRtcBinStream*>(data);

    if (!self || self->stopping_) {
        return GST_FLOW_EOS;
    }

    GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));

    if (!sample) {
        return GST_FLOW_ERROR;
    }

    GstCaps* caps = gst_sample_get_caps(sample);
    GstBuffer* buf = gst_sample_get_buffer(sample);

    if (!caps || !buf) {
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    GstVideoInfo vinfo;
    if (!gst_video_info_from_caps(&vinfo, caps)) {
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    GstVideoFrame vframe;
    if (gst_video_frame_map(&vframe, &vinfo, buf, GST_MAP_READ)) {
        if (GST_VIDEO_FRAME_N_PLANES(&vframe) < 2) {
            gst_video_frame_unmap(&vframe);
            gst_sample_unref(sample);
            return GST_FLOW_OK;
        }

        const int w = GST_VIDEO_FRAME_WIDTH(&vframe);
        const int h = GST_VIDEO_FRAME_HEIGHT(&vframe);

        if (w <= 0 || h <= 0 || (w % 2) != 0 || (h % 2) != 0) {
            gst_video_frame_unmap(&vframe);
            gst_sample_unref(sample);
            return GST_FLOW_OK;
        }

        const int y_stride = GST_VIDEO_FRAME_PLANE_STRIDE(&vframe, 0);
        const int uv_stride = GST_VIDEO_FRAME_PLANE_STRIDE(&vframe, 1);

        const auto* y_src = static_cast<const uint8_t*>(
            GST_VIDEO_FRAME_PLANE_DATA(&vframe, 0));
        const auto* uv_src = static_cast<const uint8_t*>(
            GST_VIDEO_FRAME_PLANE_DATA(&vframe, 1));

        if (!y_src || !uv_src) {
            gst_video_frame_unmap(&vframe);
            gst_sample_unref(sample);
            return GST_FLOW_OK;
        }

        {
            std::lock_guard<std::mutex> lock(self->frame_mutex_);

            auto& f = self->pending_frame_;
            f.width = w;
            f.height = h;

            copy_plane(f.y_plane, y_src, w, h, y_stride);
            copy_plane(f.uv_plane, uv_src, w, h / 2, uv_stride);

            f.dirty = true;
        }

        self->connected_ = true;
        self->log_pipeline_latency();

        gst_video_frame_unmap(&vframe);
    }

    gst_sample_unref(sample);
    return GST_FLOW_OK;
}