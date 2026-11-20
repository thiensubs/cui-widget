# libcui-widget — the CUI widget-renderer library (standalone).
#
# Builds a self-contained static lib that renders a document (a stream of
# RenderOps) through CUI's software rasterizer and exposes it over a C ABI
# (include/cuiw.h). Depends on libcui ONLY — no widget sandbox, no QuickJS,
# no validator, no CPU/memory caps. An application that starts with CUI can
# embed this lib and render widgets itself.
#
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#
# Point CUI_ROOT at a CUI checkout that has been built (build/libcui.a).
cmake_minimum_required(VERSION 3.20)
project(cui-widget C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ── locate CUI ──────────────────────────────────────────────────────────────
# Default: a CUI checkout vendored alongside this repo (../CUI/vendor/CUI),
# matching the widget-sandbox layout. Override with -DCUI_ROOT=/path/to/CUI.
get_filename_component(_default_cui "${CMAKE_CURRENT_SOURCE_DIR}/../CUI/vendor/CUI" ABSOLUTE)
set(CUI_ROOT "${_default_cui}" CACHE PATH "CUI checkout with build/libcui.a and include/")

if(NOT EXISTS "${CUI_ROOT}/include/cui/cui.h")
    message(FATAL_ERROR "CUI: cui.h not found at ${CUI_ROOT}/include — set -DCUI_ROOT=<CUI checkout>")
endif()
if(NOT EXISTS "${CUI_ROOT}/build/${CMAKE_STATIC_LIBRARY_PREFIX}cui${CMAKE_STATIC_LIBRARY_SUFFIX}")
    message(FATAL_ERROR "CUI: libcui not built at ${CUI_ROOT}/build — build CUI first (cmake --build its build dir)")
endif()

add_library(cui-rs STATIC IMPORTED)
set_target_properties(cui-rs PROPERTIES IMPORTED_LOCATION
    "${CUI_ROOT}/build/${CMAKE_STATIC_LIBRARY_PREFIX}cui${CMAKE_STATIC_LIBRARY_SUFFIX}")

# ── the library ─────────────────────────────────────────────────────────────
add_library(cui-widget STATIC
    src/cuiw.cpp
    src/cui_renderer.cpp
    src/cui_svg.cpp
)
target_compile_features(cui-widget PUBLIC cxx_std_20)
target_include_directories(cui-widget PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
# CUI's headers are C; include them as such from our C++ sources.
target_include_directories(cui-widget SYSTEM PUBLIC "${CUI_ROOT}/include")
target_link_libraries(cui-widget PUBLIC cui-rs)

if(APPLE)
    # libcui draws through the system compositor/rasterizer; it needs these
    # frameworks. They are also what a GUI host links, so listing them here
    # keeps a console host from having to know.
    target_link_libraries(cui-widget PUBLIC
        "-framework Cocoa" "-framework OpenGL" "-framework CoreVideo"
        "-framework IOKit" "-framework Metal" "-framework MetalKit")
endif()

if(MSVC)
    target_compile_options(cui-widget PRIVATE /W4)
else()
    target_compile_options(cui-widget PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# ── the example (also the isolation proof) ──────────────────────────────────
# Links ONLY cui-widget (+ its libcui transitively). If this builds and runs
# without any widget-sandbox symbol, the lib is genuinely standalone.
add_executable(cuiw-render examples/render_png.c)
target_link_libraries(cuiw-render PRIVATE cui-widget)

# Pixel probe: prints the actual RGBA values the rasterizer produced, so layout
# can be checked without an image viewer in the loop.
add_executable(cuiw-probe examples/probe.c)
target_link_libraries(cuiw-probe PRIVATE cui-widget)

# ── the reference app + the pixel-regression gate ───────────────────────────
# widget_app lays out a representative document and writes a PNG per theme. CI
# builds it on every OS, then pxdiff compares each PNG against a committed
# golden image pixel by pixel, so a layout/paint regression fails the build.
add_executable(widget-app examples/app/widget_app.c)
target_link_libraries(widget-app PRIVATE cui-widget)

# pxdiff decodes two PNGs and compares pixels. It needs stb_image to decode
# (libcui only exposes a PNG writer) and libcui's writer for the diff image.
add_executable(pxdiff tools/pxdiff.c)
target_link_libraries(pxdiff PRIVATE cui-widget)
target_include_directories(pxdiff PRIVATE "${CUI_ROOT}/third_party")

# Transition self-check: drives a color transition on a virtual clock and asserts
# the rendered pixel interpolates start -> mid -> end. Deterministic (no sleeping);
# a silent transition regression fails the build. Run: ./build/cuiw-transitions
add_executable(cuiw-transitions tests/transitions.c)
target_link_libraries(cuiw-transitions PRIVATE cui-widget)
add_executable(cuiw-keyframes tests/keyframes.c)
target_link_libraries(cuiw-keyframes PRIVATE cui-widget)

message(STATUS "cui-widget: CUI_ROOT=${CUI_ROOT}")
