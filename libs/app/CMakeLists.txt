# eer_app: the window, theme, config paths and image loading every app uses.
# Needs only SDL and ImGui, so it builds anywhere SDL does.

add_library(eer_app STATIC
  src/App.cpp
  src/Image.cpp
  src/Paths.cpp
  src/Theme.cpp
  src/Ui.cpp
)

add_library(eer::app ALIAS eer_app)

# Build date for About pages. See cmake/write_build_info.cmake.
set(EER_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${EER_GENERATED_DIR})

add_custom_target(eer_build_info ALL
  COMMAND ${CMAKE_COMMAND}
    -DOUTPUT=${EER_GENERATED_DIR}/build_info.h
    -P ${PROJECT_SOURCE_DIR}/cmake/write_build_info.cmake
  BYPRODUCTS ${EER_GENERATED_DIR}/build_info.h
  COMMENT "Stamping build date"
)

add_dependencies(eer_app eer_build_info)

target_include_directories(eer_app PUBLIC include ${EER_GENERATED_DIR})
target_compile_definitions(eer_app PUBLIC EER_VERSION="${PROJECT_VERSION}")
target_compile_options(eer_app PRIVATE ${EER_WARNINGS})

target_link_libraries(eer_app
  PUBLIC eer::sdl3 eer::imgui
  PRIVATE eer::stb
)

# Theme.cpp asks CoreText where the system font is. CFRelease and friends are
# CoreFoundation, so both are needed.
if(APPLE)
  find_library(EER_CORETEXT CoreText REQUIRED)
  find_library(EER_COREFOUNDATION CoreFoundation REQUIRED)
  target_link_libraries(eer_app PRIVATE ${EER_CORETEXT} ${EER_COREFOUNDATION})
endif()
