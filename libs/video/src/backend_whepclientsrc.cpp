#include <eer/video/backend_whepclientsrc.hpp>

#include <gst/app/gstappsink.h>
#include <gst/video/video.h>

#include <cctype>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr guint kVideoQueueBuffers = 1;
constexpr guint kAudioQueueBuffers = 1;
constexpr guint kAppSinkBuffers = 1;

// Queue leaky modes:
// 0 = no leak
// 1 = upstream/new buffers
// 2 = downstream/old buffers
constexpr guint kQueueLeakyMode = 2;

// whepclientsrc's own defaults. video-codecs already covers everything WebRTC
// can carry; audio-codecs defaults to <OPUS>, which is the reason this backend
// could not read a stream that has no audio: the server is asked for an audio
// track it does not have and rejects the whole session with
// "codecs not supported by client". Offering no audio works against streams
// with and without it, so that is the default here.
const char* const kDefaultVideoCodecs[] = {
    "H264", "H265", "VP8", "VP9", "AV1", nullptr};

// Pulls codec names out of a GStreamer caps string, so the same config field
// feeds both backends: the webrtcbin backend wants the caps themselves, while
// this element wants bare codec names.
// "application/x-rtp,media=video,encoding-name=H264,..." -> {"H264"}
std::vector<std::string> codec_names_from_caps(const std::string& caps) {
    static const std::string kKey = "encoding-name=";

    std::vector<std::string> names;
    size_t pos = 0;

    while ((pos = caps.find(kKey, pos)) != std::string::npos) {
        pos += kKey.size();

        // Values may be bare (H264), quoted ("H264") or typed ((string)H264).
        if (pos < caps.size() && caps[pos] == '(') {
            const size_t close = caps.find(')', pos);

            if (close == std::string::npos) {
                break;
            }

            pos = close + 1;
        }

        bool quoted = false;

        if (pos < caps.size() && caps[pos] == '"') {
            quoted = true;
            ++pos;
        }

        const size_t end =
            quoted ? caps.find('"', pos) : caps.find_first_of(",; \t", pos);

        std::string name = caps.substr(
            pos, end == std::string::npos ? std::string::npos : end - pos);

        for (char& c : name) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }

        if (!name.empty()) {
            names.push_back(name);
        }

        if (end == std::string::npos) {
            break;
        }

        pos = end;
    }

    return names;
}

// Sets one of whepclientsrc's GstValueArray codec-list properties. An empty
// list means "do not negotiate this media type at all".
void set_codec_list(
    GstElement* element,
    const char* property,
    const std::vector<std::string>& codecs) {
    GValue array = G_VALUE_INIT;
    g_value_init(&array, GST_TYPE_ARRAY);

    for (const std::string& codec : codecs) {
        GValue entry = G_VALUE_INIT;
        g_value_init(&entry, G_TYPE_STRING);
        g_value_set_string(&entry, codec.c_str());
        gst_value_array_append_and_take_value(&array, &entry);
    }

    g_object_set_property(G_OBJECT(element), property, &array);
    g_value_unset(&array);
}

// RTP depayloader factory name -> the codec it depayloads.
struct DepayCodec {
    const char* depay;
    const char* codec;
    bool is_video;
};

const DepayCodec kDepayCodecs[] = {
    {"rtph264depay", "H264", true},
    {"rtph265depay", "H265", true},
    {"rtpvp8depay", "VP8", true},
    {"rtpvp9depay", "VP9", true},
    {"rtpav1depay", "AV1", true},
    {"rtpopusdepay", "OPUS", false},
    {"rtppcmudepay", "PCMU", false},
    {"rtppcmadepay", "PCMA", false},
};

// Names the missing element and points at the cause, because a plugin that is
// not installed is by far the likeliest reason for this to fail and it is not
// something the message should make anyone guess at. whepclientsrc is the one
// element here that does not come from gst-plugins-bad, it is in `rswebrtc`,
// from the Rust plugin set, so it is the one most often simply absent.
//
// backend_webrtcbin.cpp carries the same helper. They stay separate because the
// two backends are alternatives: only ever one of them is compiled into a
// build, so there is no shared translation unit for this to live in.
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

} // namespace

WhepClientStream::WhepClientStream()  = default;
WhepClientStream::~WhepClientStream() { stop(); }

// ---------------------------------------------------------------------------
// start, create pipeline + whepclientsrc; all WHEP signalling, SDP
// negotiation, ICE, RTP depayloading, and decoding happen inside the element.
// ---------------------------------------------------------------------------
bool WhepClientStream::start(const StreamConfig& cfg) {
    stopping_ = false;
    failed_ = false;
    connected_ = false;
    cfg_ = cfg;

    video_linked_ = false;
    audio_linked_ = false;

    if (cfg_.url.empty()) {
        std::cerr << "[" << cfg_.label
                  << "] Refusing to start with an empty WHEP URL\n";
        failed_ = true;
        return false;
    }

    pipeline_ = gst_pipeline_new(nullptr);

    if (!pipeline_) {
        std::cerr << "[" << cfg_.label << "] Failed to create the pipeline\n";
        return false;
    }

    // Reported separately from the pipeline above: one means GStreamer is out
    // of memory, the other means a plugin is missing, and telling them apart
    // is the whole difference between a machine problem and an install one.
    whepclientsrc_ =
        make_element("whepclientsrc", cfg_.label, "WHEP signalling");

    if (!whepclientsrc_) {
        return false;
    }

    gst_child_proxy_set(
        GST_CHILD_PROXY(whepclientsrc_),
        "signaller::whep-endpoint",
        cfg_.url.c_str(),
        nullptr);

    g_object_set(whepclientsrc_, "stun-server", NULL, nullptr);

    // Video: honour an explicit caps override, otherwise offer everything we
    // can decode so the server can pick.
    {
        std::vector<std::string> video_codecs =
            codec_names_from_caps(cfg_.video_caps);

        if (video_codecs.empty()) {
            for (const char* const* c = kDefaultVideoCodecs; *c; ++c) {
                video_codecs.emplace_back(*c);
            }
        }

        set_codec_list(whepclientsrc_, "video-codecs", video_codecs);
    }

    // Audio: none unless explicitly configured. Audio is discarded anyway, and
    // asking for a track the server does not have breaks the whole session.
    {
        std::vector<std::string> audio_codecs;

        if (!is_none_caps(cfg_.audio_caps)) {
            audio_codecs = codec_names_from_caps(cfg_.audio_caps);
        }

        set_codec_list(whepclientsrc_, "audio-codecs", audio_codecs);
    }

    gst_bin_add(GST_BIN(pipeline_), whepclientsrc_);

    g_signal_connect(
        whepclientsrc_,
        "pad-added",
        G_CALLBACK(+[](GstElement*, GstPad* pad, gpointer d) {
            static_cast<WhepClientStream*>(d)->on_pad_added(pad);
        }),
        this);

    GstBus* bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline_));
    gst_bus_set_sync_handler(bus, on_bus_sync, this, nullptr);
    gst_object_unref(bus);

    gst_element_set_state(pipeline_, GST_STATE_PLAYING);

    return true;
}

GstBusSyncReply WhepClientStream::on_bus_sync(
    GstBus*,
    GstMessage* msg,
    gpointer data) {
    auto* self = static_cast<WhepClientStream*>(data);
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
            
            self->failed_ = true;
            if (debug_info) {
                std::cerr << "  Debug: " << debug_info << "\n";
            }

            g_clear_error(&err);
            g_free(debug_info);
            break;
        }

        case GST_MESSAGE_EOS:
            self->failed_ = true;
            std::cerr << "[" << self->cfg_.label << "] End of stream\n";
            break;

        default:
            break;
    }

    return GST_BUS_PASS;
}

void WhepClientStream::stop() {
    stopping_ = true;

    if (pipeline_) {
        GstBus* bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline_));
        gst_bus_set_sync_handler(bus, nullptr, nullptr, nullptr);
        gst_object_unref(bus);

        gst_element_set_state(pipeline_, GST_STATE_NULL);
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
    }

    appsink_ = nullptr;
    video_linked_ = false;
    audio_linked_ = false;
}

bool WhepClientStream::failed() const {
    return failed_.load();
}

bool WhepClientStream::connected() const {
    return connected_.load();
}

// whepclientsrc hands out already-decoded pads, so the codec is not visible in
// their caps. It is still recoverable from the RTP depayloader its internal
// decodebin autoplugged, which is named after the codec it handles.
std::string WhepClientStream::codec_summary() const {
    if (!whepclientsrc_) {
        return {};
    }

    std::string video;
    std::string audio;

    GstIterator* it = gst_bin_iterate_recurse(GST_BIN(whepclientsrc_));

    if (!it) {
        return {};
    }

    GValue item = G_VALUE_INIT;
    bool done = false;

    while (!done) {
        switch (gst_iterator_next(it, &item)) {
            case GST_ITERATOR_OK: {
                auto* element = static_cast<GstElement*>(g_value_get_object(&item));
                GstElementFactory* factory =
                    element ? gst_element_get_factory(element) : nullptr;

                const char* name =
                    factory ? GST_OBJECT_NAME(factory) : nullptr;

                if (name) {
                    for (const DepayCodec& entry : kDepayCodecs) {
                        if (std::strcmp(name, entry.depay) != 0) {
                            continue;
                        }

                        (entry.is_video ? video : audio) = entry.codec;
                        break;
                    }
                }

                g_value_reset(&item);
                break;
            }

            case GST_ITERATOR_RESYNC:
                gst_iterator_resync(it);
                video.clear();
                audio.clear();
                break;

            default:
                done = true;
                break;
        }
    }

    g_value_unset(&item);
    gst_iterator_free(it);

    if (video.empty()) {
        return audio;
    }

    if (audio.empty()) {
        return video;
    }

    return video + " + " + audio;
}

bool WhepClientStream::poll_frame(FrameData& out) {
    std::lock_guard<std::mutex> lock(frame_mutex_);
    if (!pending_frame_.dirty) return false;

    // Swap rather than copy, so the caller's buffers go back to the capture
    // side for reuse instead of being reallocated every frame.
    out.y_plane.swap(pending_frame_.y_plane);
    out.uv_plane.swap(pending_frame_.uv_plane);
    out.width = pending_frame_.width;
    out.height = pending_frame_.height;
    out.dirty = true;

    pending_frame_.dirty = false;
    return true;
}

// Travels upstream to whepclientsrc's internal webrtcbin, which turns it into
// an RTCP PLI, so a stalled decoder can recover without a new session.
void WhepClientStream::request_keyframe() {
    if (stopping_ || !pipeline_) return;

    gst_element_send_event(
        pipeline_,
        gst_video_event_new_upstream_force_key_unit(
            GST_CLOCK_TIME_NONE, TRUE, 0));
}

// ---------------------------------------------------------------------------
// on_pad_added, whepclientsrc exposes *decoded* pads:
//   video_%s_%u  →  video/x-raw (possibly with HW memory feature)
//   audio_%s_%u  →  audio/x-raw
//
// For video we colour-convert to NV12 (handles HW surface download too) and
// push into appsink.  For audio we route to autoaudiosink.
// ---------------------------------------------------------------------------
void WhepClientStream::on_pad_added(GstPad* pad) {
    if (stopping_) return;

    const std::string name = GST_PAD_NAME(pad);

    if (name.rfind("video_", 0) == 0) {
        if (video_linked_) return;
        video_linked_ = true;

        GstElement* convert = gst_element_factory_make("videoconvert", nullptr);
        GstElement* capsfilter =
            gst_element_factory_make("capsfilter", nullptr);
        GstElement* queue = gst_element_factory_make("queue", nullptr);
        GstElement* appsink = gst_element_factory_make("appsink", nullptr);

        if (!convert || !capsfilter || !queue || !appsink) {
            std::cerr << "[" << cfg_.label
                      << "] Failed to create video sink chain\n";
            return;
        }

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

        appsink_ = appsink;
        g_signal_connect(
            appsink, "new-sample", G_CALLBACK(on_new_sample), this);

        gst_bin_add_many(
            GST_BIN(pipeline_), convert, capsfilter, queue, appsink, nullptr);

        if (!gst_element_link_many(
                convert, capsfilter, queue, appsink, nullptr)) {
            std::cerr << "[" << cfg_.label
                      << "] Failed to link video chain\n";
            return;
        }

        gst_element_sync_state_with_parent(convert);
        gst_element_sync_state_with_parent(capsfilter);
        gst_element_sync_state_with_parent(queue);
        gst_element_sync_state_with_parent(appsink);

        GstPad* sink = gst_element_get_static_pad(convert, "sink");
        GstPadLinkReturn ret = gst_pad_link(pad, sink);
        gst_object_unref(sink);

        if (ret != GST_PAD_LINK_OK) {
            std::cerr << "[" << cfg_.label
                      << "] Failed to link source pad to video chain\n";
        }

    } else if (name.rfind("audio_", 0) == 0) {
        if (audio_linked_) return;
        audio_linked_ = true;

        GstElement* queue = gst_element_factory_make("queue", nullptr);
        GstElement* sink = gst_element_factory_make("fakesink", nullptr);

        if (!queue || !sink) {
            std::cerr << "[" << cfg_.label
                    << "] Failed to create audio fakesink chain\n";
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

        g_object_set(
            sink,
            "sync",
            FALSE,
            "async",
            FALSE,
            nullptr);

        gst_bin_add_many(GST_BIN(pipeline_), queue, sink, nullptr);

        if (!gst_element_link(queue, sink)) {
            std::cerr << "[" << cfg_.label
                    << "] Failed to link audio fakesink chain\n";
            return;
        }

        gst_element_sync_state_with_parent(queue);
        gst_element_sync_state_with_parent(sink);

        GstPad* sink_pad = gst_element_get_static_pad(queue, "sink");
        GstPadLinkReturn ret = gst_pad_link(pad, sink_pad);
        gst_object_unref(sink_pad);

        if (ret != GST_PAD_LINK_OK) {
            std::cerr << "[" << cfg_.label
                    << "] Failed to link audio pad to fakesink chain\n";
        }
    }
}

// ---------------------------------------------------------------------------
// on_new_sample, unchanged; frame arrives as NV12 from capsfilter above.
// ---------------------------------------------------------------------------
GstFlowReturn WhepClientStream::on_new_sample(GstElement* sink, gpointer data) {
    auto* self = static_cast<WhepClientStream*>(data);
    if (!self || self->stopping_) return GST_FLOW_EOS;

    GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if (!sample) return GST_FLOW_ERROR;

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

        std::lock_guard<std::mutex> lock(self->frame_mutex_);
        auto& f = self->pending_frame_;
        f.width = w;
        f.height = h;

        f.y_plane.resize(static_cast<size_t>(w) * h);
        for (int row = 0; row < h; ++row) {
            memcpy(f.y_plane.data() + row * w, y_src + row * y_stride, w);
        }

        const int uv_rows = h / 2;
        f.uv_plane.resize(static_cast<size_t>(w) * uv_rows);
        for (int row = 0; row < uv_rows; ++row) {
            memcpy(f.uv_plane.data() + row * w, uv_src + row * uv_stride, w);
        }

        f.dirty = true;
        self->connected_ = true;
        gst_video_frame_unmap(&vframe);
    }

    gst_sample_unref(sample);
    return GST_FLOW_OK;
}