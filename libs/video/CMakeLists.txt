# eer_video: WHEP camera streams, the camera widget and screenshots.
# Needs GStreamer, libtiff and libcurl, so an app that shows no video should
# not link it.

find_package(PkgConfig REQUIRED)
find_package(CURL REQUIRED)

pkg_check_modules(GST REQUIRED IMPORTED_TARGET
  gstreamer-1.0
  gstreamer-app-1.0
  gstreamer-webrtc-1.0
  gstreamer-sdp-1.0
  gstreamer-video-1.0
)

pkg_check_modules(TIFF REQUIRED IMPORTED_TARGET libtiff-4)

set(EER_VIDEO_BACKEND "webrtcbin" CACHE STRING
  "Camera backend: webrtcbin or whepclientsrc")
set_property(CACHE EER_VIDEO_BACKEND PROPERTY STRINGS webrtcbin whepclientsrc)

if(EER_VIDEO_BACKEND STREQUAL "webrtcbin")
  set(BACKEND_SOURCE src/backend_webrtcbin.cpp)
  set(BACKEND_DEFINE EER_VIDEO_BACKEND_WEBRTCBIN=1)
elseif(EER_VIDEO_BACKEND STREQUAL "whepclientsrc")
  set(BACKEND_SOURCE src/backend_whepclientsrc.cpp)
  set(BACKEND_DEFINE EER_VIDEO_BACKEND_WHEPCLIENTSRC=1)
else()
  message(FATAL_ERROR
    "Invalid EER_VIDEO_BACKEND: ${EER_VIDEO_BACKEND}. "
    "Expected 'webrtcbin' or 'whepclientsrc'.")
endif()

message(STATUS "Camera backend: ${EER_VIDEO_BACKEND}")

# SDL_GPU's D3D12 backend takes DXIL and not SPIR-V, so Windows needs a second
# copy of the NV12 shaders.
option(EER_VIDEO_ENABLE_DXIL
  "Compile the NV12 shaders to DXIL for the D3D12 backend" ${WIN32})

find_program(GLSLC glslc)

if(NOT GLSLC)
  message(FATAL_ERROR
    "glslc not found. It ships with the Vulkan SDK, or as the 'shaderc' "
    "package on Linux and Homebrew.")
endif()

if(EER_VIDEO_ENABLE_DXIL)
  find_program(DXC dxc)

  if(NOT DXC)
    message(FATAL_ERROR
      "dxc not found, but EER_VIDEO_ENABLE_DXIL is ON. It ships with the "
      "Vulkan SDK. Configure with -DEER_VIDEO_ENABLE_DXIL=OFF to build "
      "without D3D12, which then needs a working Vulkan driver.")
  endif()
endif()

set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GENERATED_DIR})

set(GENERATED_HEADERS)

# Embeds FILE as SYMBOL in generated/<SYMBOL>.h
function(embed FILE SYMBOL)
  add_custom_command(
    OUTPUT ${GENERATED_DIR}/${SYMBOL}.h
    COMMAND ${CMAKE_COMMAND}
      -DINPUT=${FILE}
      -DOUTPUT=${GENERATED_DIR}/${SYMBOL}.h
      -DSYMBOL=${SYMBOL}
      -P ${PROJECT_SOURCE_DIR}/cmake/embed_file.cmake
    DEPENDS ${FILE} ${PROJECT_SOURCE_DIR}/cmake/embed_file.cmake
    COMMENT "Embedding ${SYMBOL}"
  )

  list(APPEND GENERATED_HEADERS ${GENERATED_DIR}/${SYMBOL}.h)
  set(GENERATED_HEADERS ${GENERATED_HEADERS} PARENT_SCOPE)
endfunction()

function(compile_glsl STAGE NAME)
  set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/shaders/nv12.${NAME})
  set(SPV ${GENERATED_DIR}/nv12_${NAME}.spv)

  add_custom_command(
    OUTPUT ${SPV}
    COMMAND ${GLSLC} -fshader-stage=${STAGE} ${SRC} -o ${SPV}
    DEPENDS ${SRC}
    COMMENT "Compiling nv12.${NAME} to SPIR-V"
  )

  embed(${SPV} nv12_${NAME}_spv)
  set(GENERATED_HEADERS ${GENERATED_HEADERS} PARENT_SCOPE)
endfunction()

function(compile_hlsl PROFILE NAME)
  set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/shaders/nv12.${NAME}.hlsl)
  set(DXIL ${GENERATED_DIR}/nv12_${NAME}.dxil)

  add_custom_command(
    OUTPUT ${DXIL}
    COMMAND ${DXC} -T ${PROFILE} -E main -Fo ${DXIL} ${SRC}
    DEPENDS ${SRC}
    COMMENT "Compiling nv12.${NAME}.hlsl to DXIL"
  )

  embed(${DXIL} nv12_${NAME}_dxil)
  set(GENERATED_HEADERS ${GENERATED_HEADERS} PARENT_SCOPE)
endfunction()

compile_glsl(vertex vert)
compile_glsl(fragment frag)

if(EER_VIDEO_ENABLE_DXIL)
  compile_hlsl(vs_6_0 vert)
  compile_hlsl(ps_6_0 frag)
endif()

# The image a camera shows while it has no picture.
embed(${PROJECT_SOURCE_DIR}/assets/nosignal/nosignal.jpg nosignal_jpg)

add_library(eer_video STATIC
  src/Camera.cpp
  src/CameraStreamFactory.cpp
  src/GpuShared.cpp
  src/Screenshot.cpp
  ${BACKEND_SOURCE}
  ${GENERATED_HEADERS}
)

add_library(eer::video ALIAS eer_video)

target_include_directories(eer_video PUBLIC include ${GENERATED_DIR})

target_compile_definitions(eer_video
  PUBLIC
    ${BACKEND_DEFINE}
  PRIVATE
    GST_USE_UNSTABLE_API
    $<$<BOOL:${EER_VIDEO_ENABLE_DXIL}>:EER_VIDEO_HAVE_DXIL=1>
)

target_compile_options(eer_video PRIVATE ${EER_WARNINGS})

target_link_libraries(eer_video
  PUBLIC eer::sdl3 eer::imgui
  PRIVATE PkgConfig::GST PkgConfig::TIFF CURL::libcurl
)
