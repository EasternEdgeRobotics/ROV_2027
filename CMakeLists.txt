cmake_minimum_required(VERSION 3.25)

project(ROV_2027 VERSION 0.1.0 LANGUAGES C CXX)

# An empty build type means no optimisation at all, for our code and for the
# vendored ImGui and SDL with it, and video decode is unusable like that.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
    "Build type (Debug, Release, RelWithDebInfo, MinSizeRel)" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Applied to our own targets only, so a dependency bump cannot bury our
# warnings under theirs.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set(EER_WARNINGS -Wall -Wextra -Wpedantic)
else()
  set(EER_WARNINGS)
endif()

# Without submodules the failure is a wall of missing files deep inside
# third_party/, so check one file and name the fix.
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdl/CMakeLists.txt)
  message(FATAL_ERROR
    "third_party/sdl is empty. Clone with submodules, or run:\n"
    "  git submodule update --init --recursive")
endif()

# For the icon resource script. Has to be enabled at file scope, so it cannot
# live in eer_package_app().
if(WIN32)
  enable_language(RC)
endif()

include(cmake/Packaging.cmake)

add_subdirectory(third_party)
add_subdirectory(libs/app)
add_subdirectory(libs/video)
add_subdirectory(apps/frontend)
