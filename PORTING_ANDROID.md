# Porting Total Overdose (Kapow Engine) to Android

## Architecture Overview

This port replaces all Windows-specific APIs with Android NDK equivalents while
keeping the core engine logic intact.

### API Replacement Map

| Original (Windows)       | Android Replacement            | File                        |
|--------------------------|--------------------------------|-----------------------------|
| `DirectX 9`             | OpenGL ES 3.0                  | `GfxInternal_GLES.h/cpp`   |
| `DirectSound`           | OpenSL ES                      | `SoundSystem_OpenSLES.h/cpp`|
| `DirectInput 8`         | Android NDK Input + Touch      | `Input_Android.h/cpp`      |
| `Win32 File I/O`        | AAssetManager + POSIX          | `FileSystem_Android.h/cpp` |
| `HWND / WinMain`        | NativeActivity / JNI           | `android_main.cpp`         |
| `CRITICAL_SECTION`      | `pthread_mutex`                | `stdafx_android.h`         |
| `CreateThread`          | `pthread_create`               | `android_main.cpp`         |
| `timeGetTime`           | `clock_gettime(CLOCK_MONOTONIC)` | `stdafx_android.h`       |
| `SSE intrinsics`        | ARM NEON                       | `stdafx_android.h`         |
| `DLL injection`         | Native shared library          | `CMakeLists.txt`           |

## File Structure

```
TOD1/
  stdafx_android.h          # Windows type compatibility layer
  platform/android/
    Platform_Android.h/cpp   # EGL + lifecycle management
    GfxInternal_GLES.h/cpp   # OpenGL ES 3.0 renderer
    SoundSystem_OpenSLES.h/cpp # OpenSL ES audio
    Input_Android.h/cpp      # Touch, gamepad, keyboard input
    FileSystem_Android.h/cpp # Asset + file I/O
    android_main.cpp         # Entry point (ANativeActivity_onCreate)
    CMakeLists.txt           # NDK build configuration

android/                     # Android Studio project
  build.gradle
  settings.gradle
  app/
    build.gradle
    src/main/
      AndroidManifest.xml
      cpp/CMakeLists.txt     # Delegates to engine CMake
      res/values/strings.xml
      assets/                # Place game data files here
```

## Building

### Prerequisites
- Android Studio (latest)
- Android NDK r25+ (via SDK Manager)
- CMake 3.22+ (via SDK Manager)

### Steps

1. Open `android/` directory in Android Studio
2. SDK Manager -> install NDK and CMake
3. Build -> Make Project (or `./gradlew assembleDebug`)

### Command-line build
```bash
cd android
./gradlew assembleDebug

# APK output: app/build/outputs/apk/debug/app-debug.apk
```

## Game Data

The game requires the original Total Overdose data files. You must own the game.

### Option A: Bundled in APK
Place data files in `android/app/src/main/assets/`:
```
assets/
  data/
    *.main files
    *.map files
    scripts/
    textures/
    sounds/
```

### Option B: External storage
Copy data to device:
```bash
adb push gamedata/ /sdcard/Android/data/com.kapow.tod/files/gamedata/
```

The FileSystem_Android class searches both locations automatically.

## Porting Progress

### Completed
- [x] Windows type compatibility (`stdafx_android.h`)
- [x] Platform/lifecycle management (`Platform_Android`)
- [x] OpenGL ES 3.0 renderer with shaders (`GfxInternal_GLES`)
- [x] OpenSL ES audio with 3D simulation (`SoundSystem_OpenSLES`)
- [x] Touch + gamepad + keyboard input (`Input_Android`)
- [x] File I/O with AAssetManager (`FileSystem_Android`)
- [x] NativeActivity entry point (`android_main.cpp`)
- [x] CMake build system
- [x] Android Studio project structure

### Remaining Work
- [ ] Connect engine core classes (Scene, Node, etc.) to Android backends
- [ ] Port MemoryManager to use Android-friendly allocators
- [ ] Port Texture/AssetManager to load through FileSystem_Android
- [ ] Port MeshBuffer to use VertexBufferGLES/IndexBufferGLES
- [ ] Convert D3D render states to GLES equivalents in GfxInternal
- [ ] Port StreamedSoundBuffer to use SoundBufferSLES
- [ ] Implement virtual on-screen HUD overlay renderer
- [ ] Port font rendering (TextBox) to GLES
- [ ] Port particle system rendering to GLES
- [ ] Port shadow/decal rendering to GLES
- [ ] Test with actual game data files
- [ ] Performance optimization (shader LOD, texture compression)
- [ ] Implement save/load via Android internal storage

## Key Design Decisions

1. **NativeActivity** instead of Java Activity: Minimizes JNI overhead since
   the engine is 100% C++. All rendering, input, and audio happens in native code.

2. **OpenGL ES 3.0** minimum: Provides compute shaders, instancing, and
   multiple render targets needed by the engine. Android 7.0+ (API 24) guarantees
   ES 3.0 support.

3. **Separate game thread**: The engine runs in its own pthread, separate from
   the Android UI thread. This matches the original engine's threading model and
   avoids ANR timeouts.

4. **3D audio simulation**: OpenSL ES doesn't have built-in 3D audio like
   DirectSound3D. Distance attenuation and panning are computed manually in
   `SoundSystem_OpenSLES::Update()`.

5. **Virtual controls**: Touch-screen users get on-screen dual sticks and
   action buttons. Physical gamepads (Bluetooth/USB) are also fully supported
   with automatic detection.
