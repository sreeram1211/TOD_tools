# CMakeLists.txt for Kapow Engine Android port
# Build with: cmake -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake
#              -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 ..

cmake_minimum_required(VERSION 3.18)
project(KapowEngine VERSION 34.7.1925 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Android-specific defines
add_definitions(
    -DPLATFORM_ANDROID
    -DINCLUDE_FIXES
    -DKAPOW_ENGINE_VERSION_MAJOR=34
    -DKAPOW_ENGINE_VERSION_MINOR=7
    -DKAPOW_ENGINE_VERSION_BUILD=1925
)

# ---------------------------------------------------------------------------
# Source files: Android platform layer
# ---------------------------------------------------------------------------
set(ANDROID_PLATFORM_SOURCES
    Platform_Android.cpp
    GfxInternal_GLES.cpp
    SoundSystem_OpenSLES.cpp
    Input_Android.cpp
    FileSystem_Android.cpp
    android_main.cpp
)

# ---------------------------------------------------------------------------
# Source files: Engine core (platform-independent parts from TOD1)
# These are the .cpp files that don't depend on Windows APIs.
# As more engine code is ported, add files here.
# ---------------------------------------------------------------------------
set(ENGINE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# Third-party libraries (already in the repo)
set(THIRDPARTY_ROOT ${ENGINE_ROOT}/../ThirdParty)

# PNG library sources
file(GLOB LIBPNG_SOURCES ${THIRDPARTY_ROOT}/libpng/*.c)

# Ogg/Vorbis sources
file(GLOB LIBOGG_SOURCES ${THIRDPARTY_ROOT}/libogg/src/*.c)
file(GLOB LIBVORBIS_SOURCES ${THIRDPARTY_ROOT}/libvorbis/lib/*.c)

# zlib sources
file(GLOB ZLIB_SOURCES ${THIRDPARTY_ROOT}/zlib/*.c)

# ---------------------------------------------------------------------------
# Build the native shared library
# ---------------------------------------------------------------------------
add_library(kapow_engine SHARED
    ${ANDROID_PLATFORM_SOURCES}
    ${LIBPNG_SOURCES}
    ${LIBOGG_SOURCES}
    ${LIBVORBIS_SOURCES}
    ${ZLIB_SOURCES}
)

# Include directories
target_include_directories(kapow_engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ENGINE_ROOT}
    ${THIRDPARTY_ROOT}/libpng
    ${THIRDPARTY_ROOT}/libogg/include
    ${THIRDPARTY_ROOT}/libvorbis/include
    ${THIRDPARTY_ROOT}/zlib
)

# Link Android NDK libraries
target_link_libraries(kapow_engine
    # Android platform
    android
    log

    # Graphics
    EGL
    GLESv3

    # Audio
    OpenSLES

    # Standard
    m
    z
)

# Compiler flags
target_compile_options(kapow_engine PRIVATE
    -Wall
    -Wno-unused-variable
    -Wno-unused-function
    -fno-exceptions        # Match original engine (no exceptions)
    -fno-rtti              # Match original engine (no RTTI)
    -O2                    # Optimize for speed
)

# ARM NEON for math operations (replaces SSE)
if(ANDROID_ABI STREQUAL "arm64-v8a")
    target_compile_options(kapow_engine PRIVATE -march=armv8-a)
elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
    target_compile_options(kapow_engine PRIVATE -mfpu=neon -mfloat-abi=softfp)
endif()

# Strip debug info for release builds
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(kapow_engine PRIVATE -g0)
    set_target_properties(kapow_engine PROPERTIES
        LINK_FLAGS "-s"
    )
endif()

# Export ANativeActivity_onCreate
set_target_properties(kapow_engine PROPERTIES
    LINK_FLAGS "-u ANativeActivity_onCreate"
)
