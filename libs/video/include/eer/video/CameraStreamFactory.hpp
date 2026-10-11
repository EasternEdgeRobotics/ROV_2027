#pragma once

#include <eer/video/CameraStream.hpp>

#include <memory>

std::unique_ptr<CameraStream> createCameraStream();

const char* cameraStreamBackendName();