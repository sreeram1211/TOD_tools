# Top-level CMakeLists.txt for Android Studio integration
# This file is referenced by build.gradle

cmake_minimum_required(VERSION 3.18)
project(KapowEngine)

# Include the actual build file from the engine source tree
add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../TOD1/platform/android
    ${CMAKE_CURRENT_BINARY_DIR}/kapow_engine
)
