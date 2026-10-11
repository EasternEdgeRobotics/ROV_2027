#include <eer/video/CameraStreamFactory.hpp>

#if defined(EER_VIDEO_BACKEND_WEBRTCBIN)
#include <eer/video/backend_webrtcbin.hpp>
#elif defined(EER_VIDEO_BACKEND_WHEPCLIENTSRC)
#include <eer/video/backend_whepclientsrc.hpp>
#else
#error "No camera backend selected"
#endif

std::unique_ptr<CameraStream> createCameraStream() {
#if defined(EER_VIDEO_BACKEND_WEBRTCBIN)
    return std::make_unique<WebRtcBinStream>();
#elif defined(EER_VIDEO_BACKEND_WHEPCLIENTSRC)
    return std::make_unique<WhepClientStream>();
#endif
}

const char* cameraStreamBackendName() {
#if defined(EER_VIDEO_BACKEND_WEBRTCBIN)
    return "GStreamer webrtcbin";
#elif defined(EER_VIDEO_BACKEND_WHEPCLIENTSRC)
    return "GStreamer whepclientsrc";
#endif
}