# Vendored upstream code. Nothing in the subdirectories is edited, they are
# updated by bumping the checkout. The wrappers here are for projects that
# ship no CMake of their own.

add_subdirectory(sdl EXCLUDE_FROM_ALL)

# SDL3 exports a different target name depending on how it was configured, so
# everything downstream links this alias instead of guessing.
add_library(eer_sdl3 INTERFACE)
add_library(eer::sdl3 ALIAS eer_sdl3)

if(TARGET SDL3::SDL3)
  target_link_libraries(eer_sdl3 INTERFACE SDL3::SDL3)
elseif(TARGET SDL3::SDL3-shared)
  target_link_libraries(eer_sdl3 INTERFACE SDL3::SDL3-shared)
elseif(TARGET SDL3::SDL3-static)
  target_link_libraries(eer_sdl3 INTERFACE SDL3::SDL3-static)
else()
  message(FATAL_ERROR
    "SDL3 is vendored at third_party/sdl, but no usable SDL3 target exists. "
    "Is the checkout empty? git submodule update --init --recursive"
  )
endif()

add_subdirectory(json EXCLUDE_FROM_ALL)

# Dear ImGui ships sources but no CMake, so we compile the core plus the two
# backends this project uses.
add_library(eer_imgui STATIC
  imgui/imgui.cpp
  imgui/imgui_draw.cpp
  imgui/imgui_widgets.cpp
  imgui/imgui_tables.cpp
  imgui/backends/imgui_impl_sdl3.cpp
  imgui/backends/imgui_impl_sdlgpu3.cpp
)

add_library(eer::imgui ALIAS eer_imgui)

# Both roots are exposed: sources include "imgui.h" and, for the backends,
# "backends/imgui_impl_sdl3.h".
target_include_directories(eer_imgui
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends
)

target_link_libraries(eer_imgui PUBLIC eer::sdl3)

# stb is header only.
add_library(eer_stb INTERFACE)
add_library(eer::stb ALIAS eer_stb)

target_include_directories(eer_stb
  INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/stb
)
