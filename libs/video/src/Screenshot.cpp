#include <eer/video/Screenshot.hpp>

#include <tiffio.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

#ifndef EXIFTAG_LENSMODEL
#define EXIFTAG_LENSMODEL 0xA434
#endif

namespace {

constexpr float kMaxCrop = 0.95f;

void flipBufferVertically(
    std::vector<uint8_t>& pixels,
    int width,
    int height,
    int channels)
{
    const int stride = width * channels;
    std::vector<uint8_t> row(stride);

    for (int y = 0; y < height / 2; ++y) {
        uint8_t* top = pixels.data() + y * stride;
        uint8_t* bottom = pixels.data() + (height - 1 - y) * stride;

        std::memcpy(row.data(), top, stride);
        std::memcpy(top, bottom, stride);
        std::memcpy(bottom, row.data(), stride);
    }
}

void flipBufferHorizontally(
    std::vector<uint8_t>& pixels,
    int width,
    int height,
    int channels)
{
    const int stride = width * channels;

    for (int y = 0; y < height; ++y) {
        uint8_t* row = pixels.data() + y * stride;
        for (int x = 0; x < width / 2; ++x) {
            uint8_t* left = row + x * channels;
            uint8_t* right = row + (width - 1 - x) * channels;

            for (int c = 0; c < channels; ++c) {
                std::swap(left[c], right[c]);
            }
        }
    }
}

bool cropRgbaBuffer(
    const std::vector<uint8_t>& src,
    int srcWidth,
    int srcHeight,
    float left,
    float right,
    float top,
    float bottom,
    std::vector<uint8_t>& dst,
    int& dstWidth,
    int& dstHeight)
{
    if (srcWidth <= 0 || srcHeight <= 0) {
        return false;
    }

    left = std::clamp(left, 0.0f, kMaxCrop);
    right = std::clamp(right, 0.0f, kMaxCrop);
    top = std::clamp(top, 0.0f, kMaxCrop);
    bottom = std::clamp(bottom, 0.0f, kMaxCrop);

    int cropLeft = static_cast<int>(srcWidth * left);
    int cropRight = static_cast<int>(srcWidth * right);
    int cropTop = static_cast<int>(srcHeight * top);
    int cropBottom = static_cast<int>(srcHeight * bottom);

    int x0 = cropLeft;
    int y0 = cropTop;
    int x1 = srcWidth - cropRight;
    int y1 = srcHeight - cropBottom;

    if (x1 <= x0 || y1 <= y0) {
        return false;
    }

    dstWidth = x1 - x0;
    dstHeight = y1 - y0;

    constexpr int channels = 4;
    dst.resize(static_cast<size_t>(dstWidth) * dstHeight * channels);

    for (int y = 0; y < dstHeight; ++y) {
        const uint8_t* srcRow =
            src.data() + static_cast<size_t>(y0 + y) * srcWidth * channels +
            static_cast<size_t>(x0) * channels;

        uint8_t* dstRow =
            dst.data() + static_cast<size_t>(y) * dstWidth * channels;

        std::memcpy(dstRow, srcRow, static_cast<size_t>(dstWidth) * channels);
    }

    return true;
}

struct ScreenshotMetadata {
    std::string make;
    std::string model;
    std::string lensModel;
    std::string artist;
    std::string software;

    std::string label;
    std::string suffix;
    std::string cameraPosition;

    std::string dateTime;
    std::string subSecondTime;
    std::string description;
    std::string userComment;

    // Zero is "the config did not say", and writeTiffWithExif leaves the
    // matching tag out rather than writing it.
    double fNumber = 0.0;
    double physicalFocalLengthMm = 0.0;
    double refractionIndex = 1.0;
    double effectiveFocalLengthMm = 0.0;

    double sensorWidthMm = 0.0;
    double sensorHeightMm = 0.0;

    double effectivePixelSizeXmm = 0.0;
    double effectivePixelSizeYmm = 0.0;

    double focalPlaneXResolution = 0.0;
    double focalPlaneYResolution = 0.0;

    uint16_t focalLength35mm = 0;

    int streamWidth = 0;
    int streamHeight = 0;
    int outputWidth = 0;
    int outputHeight = 0;
};

std::string makeExifDateTime(std::chrono::system_clock::time_point timePoint) {
    const std::time_t time = std::chrono::system_clock::to_time_t(timePoint);

    std::tm localTime {};
#if defined(_WIN32)
    localtime_s(&localTime, &time);
#else
    localtime_r(&time, &localTime);
#endif

    std::ostringstream stream;
    stream << std::put_time(&localTime, "%Y:%m:%d %H:%M:%S");
    return stream.str();
}

std::string makeExifSubSecondTime(
    std::chrono::system_clock::time_point timePoint)
{
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            timePoint.time_since_epoch()) %
        1000;

    std::ostringstream stream;
    stream << std::setw(3) << std::setfill('0') << milliseconds.count();
    return stream.str();
}


bool writeTiffWithExif(
    const std::string& filePath,
    int width,
    int height,
    const std::vector<uint8_t>& rgbaPixels,
    const ScreenshotMetadata& metadata)
{
    if (width <= 0 || height <= 0 || rgbaPixels.empty()) {
        return false;
    }

    TIFF* tif = TIFFOpen(filePath.c_str(), "w+");
    if (!tif) {
        return false;
    }

    constexpr uint16_t samplesPerPixel = 3;
    constexpr uint16_t bitsPerSample = 8;

    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, static_cast<uint32_t>(width));
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, static_cast<uint32_t>(height));
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, samplesPerPixel);
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, bitsPerSample);
    TIFFSetField(tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
    TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
    TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_NONE);

    TIFFSetField(
        tif,
        TIFFTAG_ROWSPERSTRIP,
        TIFFDefaultStripSize(tif, static_cast<uint32_t>(height)));

    TIFFSetField(tif, TIFFTAG_MAKE, metadata.make.c_str());
    TIFFSetField(tif, TIFFTAG_MODEL, metadata.model.c_str());
    TIFFSetField(tif, TIFFTAG_SOFTWARE, metadata.software.c_str());
    TIFFSetField(tif, TIFFTAG_ARTIST, metadata.artist.c_str());
    TIFFSetField(tif, TIFFTAG_DATETIME, metadata.dateTime.c_str());
    TIFFSetField(tif, TIFFTAG_IMAGEDESCRIPTION, metadata.description.c_str());

    std::vector<uint8_t> rgbRow(static_cast<size_t>(width) * 3);

    for (int row = 0; row < height; ++row) {
        const uint8_t* rgbaRow =
            rgbaPixels.data() + static_cast<size_t>(row) * width * 4;

        for (int x = 0; x < width; ++x) {
            rgbRow[static_cast<size_t>(x) * 3 + 0] =
                rgbaRow[static_cast<size_t>(x) * 4 + 0];

            rgbRow[static_cast<size_t>(x) * 3 + 1] =
                rgbaRow[static_cast<size_t>(x) * 4 + 1];

            rgbRow[static_cast<size_t>(x) * 3 + 2] =
                rgbaRow[static_cast<size_t>(x) * 4 + 2];
        }

        if (TIFFWriteScanline(
                tif,
                rgbRow.data(),
                static_cast<uint32_t>(row),
                0) < 0) {
            TIFFClose(tif);
            return false;
        }
    }

    if (!TIFFWriteDirectory(tif)) {
        TIFFClose(tif);
        return false;
    }

    uint64_t exifDirectoryOffset = 0;

    // TIFFCreateEXIFDirectory returns 0 on success, unlike the calls around it.
    if (TIFFCreateEXIFDirectory(tif) != 0) {
        TIFFClose(tif);
        return false;
    }

    TIFFSetField(tif, EXIFTAG_DATETIMEORIGINAL, metadata.dateTime.c_str());
    TIFFSetField(tif, EXIFTAG_DATETIMEDIGITIZED, metadata.dateTime.c_str());

    TIFFSetField(
        tif,
        EXIFTAG_SUBSECTIMEORIGINAL,
        metadata.subSecondTime.c_str());

    // Anything the config did not state is left out, not written as zero.
    if (metadata.fNumber > 0.0) {
        TIFFSetField(tif, EXIFTAG_FNUMBER, metadata.fNumber);
    }

    if (metadata.effectiveFocalLengthMm > 0.0) {
        TIFFSetField(
            tif,
            EXIFTAG_FOCALLENGTH,
            metadata.effectiveFocalLengthMm);
    }

    if (metadata.focalLength35mm > 0) {
        TIFFSetField(
            tif,
            EXIFTAG_FOCALLENGTHIN35MMFILM,
            metadata.focalLength35mm);
    }

    if (metadata.focalPlaneXResolution > 0.0 &&
        metadata.focalPlaneYResolution > 0.0) {
        TIFFSetField(
            tif,
            EXIFTAG_FOCALPLANEXRESOLUTION,
            metadata.focalPlaneXResolution);

        TIFFSetField(
            tif,
            EXIFTAG_FOCALPLANEYRESOLUTION,
            metadata.focalPlaneYResolution);

        TIFFSetField(
            tif,
            EXIFTAG_FOCALPLANERESOLUTIONUNIT,
            static_cast<uint16_t>(RESUNIT_INCH));
    }

    TIFFSetField(
        tif,
        EXIFTAG_SENSINGMETHOD,
        static_cast<uint16_t>(2));

    TIFFSetField(
        tif,
        EXIFTAG_COLORSPACE,
        static_cast<uint16_t>(1));

#ifdef EXIFTAG_LENSMODEL
    if (!metadata.lensModel.empty()) {
        TIFFSetField(tif, EXIFTAG_LENSMODEL, metadata.lensModel.c_str());
    }
#endif

    std::string userComment("ASCII\0\0\0", 8);
    userComment += metadata.userComment;

    TIFFSetField(
        tif,
        EXIFTAG_USERCOMMENT,
        static_cast<uint32_t>(userComment.size()),
        reinterpret_cast<const unsigned char*>(userComment.data()));

    if (!TIFFWriteCustomDirectory(tif, &exifDirectoryOffset)) {
        TIFFClose(tif);
        return false;
    }

    if (!TIFFSetDirectory(tif, 0)) {
        TIFFClose(tif);
        return false;
    }

    TIFFSetField(tif, TIFFTAG_EXIFIFD, exifDirectoryOffset);

    TIFFClose(tif);
    return true;
}


uint16_t calculateFocalLength35mm(
    double focalLengthMm,
    int outputWidth,
    int outputHeight,
    double pixelSizeXmm,
    double pixelSizeYmm)
{
    if (outputWidth <= 0 ||
        outputHeight <= 0 ||
        pixelSizeXmm <= 0.0 ||
        pixelSizeYmm <= 0.0) {
        return 0;
    }

    constexpr double fullFrameDiagonalMm = 43.2666;

    const double usedWidthMm = static_cast<double>(outputWidth) * pixelSizeXmm;
    const double usedHeightMm =
        static_cast<double>(outputHeight) * pixelSizeYmm;

    const double usedDiagonalMm =
        std::sqrt((usedWidthMm * usedWidthMm) + (usedHeightMm * usedHeightMm));

    if (usedDiagonalMm <= 0.0) {
        return 0;
    }

    const double equivalent = focalLengthMm * fullFrameDiagonalMm /
                              usedDiagonalMm;

    return static_cast<uint16_t>(std::lround(equivalent));
}

ScreenshotMetadata makeScreenshotMetadata(
    int cameraNumber,
    const CameraExif& exif,
    const std::string& vehicleName,
    const std::string& label,
    const std::string& screenshotSuffix,
    std::chrono::system_clock::time_point now,
    int streamWidth,
    int streamHeight,
    int outputWidth,
    int outputHeight,
    bool flipVertically,
    bool flipHorizontally,
    bool cropEnabled,
    float cropLeft,
    float cropRight,
    float cropTop,
    float cropBottom)
{
    ScreenshotMetadata metadata;

    const std::string number = std::to_string(cameraNumber);
    const std::string vehiclePrefix =
        vehicleName.empty() ? std::string{} : vehicleName + " ";

    // "BlueStar Front Camera". The vehicle comes first because photogrammetry
    // groups pictures by Model, and the same camera on two ROVs is not the
    // same camera.
    const std::string cameraName =
        vehiclePrefix + (exif.model.empty() ? "Camera " + number : exif.model);

    metadata.make = "Eastern Edge";
    metadata.artist = "Eastern Edge";
    metadata.software = "Eastern Edge ROV Frontend";

    metadata.label = label;
    metadata.suffix = screenshotSuffix;

    // Names fall back to the camera's number. Physical values never fall back.
    metadata.cameraPosition =
        exif.position.empty() ? "camera " + number : exif.position;

    metadata.lensModel = exif.lens;

    metadata.physicalFocalLengthMm =
        exif.focalLengthMm > 0.0 ? exif.focalLengthMm : 0.0;
    metadata.refractionIndex =
        exif.refractionIndex > 0.0 ? exif.refractionIndex : 1.0;
    metadata.effectiveFocalLengthMm =
        metadata.physicalFocalLengthMm * metadata.refractionIndex;

    metadata.fNumber = exif.fNumber > 0.0 ? exif.fNumber : 0.0;

    if (exif.sensorActiveWidthPx > 0 && exif.sensorActiveHeightPx > 0 &&
        exif.pixelSizeUm > 0.0) {
        metadata.sensorWidthMm =
            exif.sensorActiveWidthPx * exif.pixelSizeUm / 1000.0;
        metadata.sensorHeightMm =
            exif.sensorActiveHeightPx * exif.pixelSizeUm / 1000.0;
    }

    metadata.streamWidth = streamWidth;
    metadata.streamHeight = streamHeight;
    metadata.outputWidth = outputWidth;
    metadata.outputHeight = outputHeight;

    // Effective pixel size is calculated from the actual stream resolution.
    if (streamWidth > 0) {
        metadata.effectivePixelSizeXmm =
            metadata.sensorWidthMm / static_cast<double>(streamWidth);
    }

    if (streamHeight > 0) {
        metadata.effectivePixelSizeYmm =
            metadata.sensorHeightMm / static_cast<double>(streamHeight);
    }

    if (metadata.effectivePixelSizeXmm > 0.0) {
        metadata.focalPlaneXResolution =
            25.4 / metadata.effectivePixelSizeXmm;
    }

    if (metadata.effectivePixelSizeYmm > 0.0) {
        metadata.focalPlaneYResolution =
            25.4 / metadata.effectivePixelSizeYmm;
    }

    metadata.focalLength35mm = calculateFocalLength35mm(
        metadata.effectiveFocalLengthMm,
        outputWidth,
        outputHeight,
        metadata.effectivePixelSizeXmm,
        metadata.effectivePixelSizeYmm);

    metadata.dateTime = makeExifDateTime(now);
    metadata.subSecondTime = makeExifSubSecondTime(now);

    std::ostringstream model;
    model << cameraName << " stream " << streamWidth << "x" << streamHeight;

    if (outputWidth != streamWidth || outputHeight != streamHeight) {
        model << " crop " << outputWidth << "x" << outputHeight;
    }

    metadata.model = model.str();

    const bool lensKnown = metadata.physicalFocalLengthMm > 0.0;
    const bool sensorKnown = metadata.sensorWidthMm > 0.0;

    std::ostringstream description;
    description << vehiclePrefix
                << (exif.sensor.empty() ? "Camera " : exif.sensor + " camera ")
                << cameraNumber << ", " << metadata.cameraPosition
                << "; stream " << streamWidth << "x" << streamHeight
                << "; final image " << outputWidth << "x" << outputHeight;

    if (lensKnown) {
        description << "; physical lens " << metadata.physicalFocalLengthMm
                    << "mm";

        if (metadata.fNumber > 0.0) {
            description << " F" << metadata.fNumber;
        }

        description << "; effective focal length "
                    << metadata.effectiveFocalLengthMm << "mm";
    }

    metadata.description = description.str();

    std::ostringstream comment;
    comment << "camera_id=" << cameraNumber
            << "; camera_position=" << metadata.cameraPosition;

    if (!vehicleName.empty()) {
        comment << "; vehicle=" << vehicleName;
    }

    if (!exif.sensor.empty()) {
        comment << "; sensor=" << exif.sensor;
    }

    if (lensKnown) {
        comment << "; physical_focal_length="
                << metadata.physicalFocalLengthMm << "mm"
                << "; effective_focal_length="
                << metadata.effectiveFocalLengthMm << "mm"
                << "; refraction_index=" << metadata.refractionIndex;
    }

    if (sensorKnown) {
        comment << "; native_active_area=" << exif.sensorActiveWidthPx << "x"
                << exif.sensorActiveHeightPx
                << "; native_pixel_size=" << exif.pixelSizeUm << "um"
                << "; effective_pixel_size_x="
                << metadata.effectivePixelSizeXmm * 1000.0 << "um"
                << "; effective_pixel_size_y="
                << metadata.effectivePixelSizeYmm * 1000.0 << "um"
                << "; focal_plane_x_resolution="
                << metadata.focalPlaneXResolution << "ppi"
                << "; focal_plane_y_resolution="
                << metadata.focalPlaneYResolution << "ppi";
    }

    if (metadata.focalLength35mm > 0) {
        comment << "; focal_length_35mm_equivalent="
                << metadata.focalLength35mm << "mm";
    }

    comment << "; stream_size=" << streamWidth << "x" << streamHeight
            << "; final_size=" << outputWidth << "x" << outputHeight;

    for (const auto& [key, value] : exif.notes) {
        comment << "; " << key << "=" << value;
    }

    comment << "; flip_vertical=" << (flipVertically ? "true" : "false")
            << "; flip_horizontal=" << (flipHorizontally ? "true" : "false")
            << "; crop_enabled=" << (cropEnabled ? "true" : "false")
            << "; crop_left=" << cropLeft
            << "; crop_right=" << cropRight
            << "; crop_top=" << cropTop
            << "; crop_bottom=" << cropBottom;

    if (!screenshotSuffix.empty()) {
        comment << "; section=" << screenshotSuffix;
    }

    metadata.userComment = comment.str();

    return metadata;
}

}  // namespace

void CropRect::normalize() {
    left = std::clamp(left, 0.0f, kMaxCrop);
    right = std::clamp(right, 0.0f, kMaxCrop);
    top = std::clamp(top, 0.0f, kMaxCrop);
    bottom = std::clamp(bottom, 0.0f, kMaxCrop);

    if (left + right > kMaxCrop) {
        const float scale = kMaxCrop / (left + right);
        left *= scale;
        right *= scale;
    }

    if (top + bottom > kMaxCrop) {
        const float scale = kMaxCrop / (top + bottom);
        top *= scale;
        bottom *= scale;
    }
}

bool writeScreenshot(ScreenshotRequest request) {
    if (request.flipVertical) {
        flipBufferVertically(request.pixels, request.width, request.height, 4);
    }

    if (request.flipHorizontal) {
        flipBufferHorizontally(request.pixels, request.width, request.height, 4);
    }

    std::vector<uint8_t> output = std::move(request.pixels);
    int outputWidth = request.width;
    int outputHeight = request.height;

    const CropRect& crop = request.crop;

    if (crop.enabled) {
        std::vector<uint8_t> cropped;
        int croppedWidth = 0;
        int croppedHeight = 0;

        if (cropRgbaBuffer(
                output, outputWidth, outputHeight,
                crop.left, crop.right, crop.top, crop.bottom,
                cropped, croppedWidth, croppedHeight)) {
            output = std::move(cropped);
            outputWidth = croppedWidth;
            outputHeight = croppedHeight;
        } else {
            std::cerr << "Invalid screenshot crop, saving uncropped" << std::endl;
        }
    }

    const ScreenshotMetadata metadata = makeScreenshotMetadata(
        request.cameraNumber,
        request.exif,
        request.vehicle,
        request.label,
        request.section,
        request.time,
        request.width,
        request.height,
        outputWidth,
        outputHeight,
        request.flipVertical,
        request.flipHorizontal,
        crop.enabled,
        crop.left,
        crop.right,
        crop.top,
        crop.bottom);

    return writeTiffWithExif(
        request.file.string(), outputWidth, outputHeight, output, metadata);
}
