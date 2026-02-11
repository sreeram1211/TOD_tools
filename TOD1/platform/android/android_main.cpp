/*
 * android_main.cpp
 *
 * JNI bridge and NativeActivity glue.
 * This is the Android entry point, replacing DllMain/WinMain.
 */
#include "Platform_Android.h"
#include "GfxInternal_GLES.h"
#include "SoundSystem_OpenSLES.h"
#include "Input_Android.h"
#include "FileSystem_Android.h"

#include <android/native_activity.h>
#include <android/log.h>
#include <pthread.h>
#include <unistd.h>
#include <cstring>

#define LOG_TAG "KapowEngine"

// ---------------------------------------------------------------------------
// Debug logging (replaces Windows debug() function)
// ---------------------------------------------------------------------------
void debug(char* message, ...) {
    va_list args;
    va_start(args, message);
    __android_log_vprint(ANDROID_LOG_INFO, LOG_TAG, message, args);
    va_end(args);
}

// ---------------------------------------------------------------------------
// Engine subsystems
// ---------------------------------------------------------------------------
static GfxInternal_GLES*        g_Gfx = nullptr;
static SoundSystem_OpenSLES*    g_Sound = nullptr;
static Input_Android*           g_Input = nullptr;
static FileSystem_Android*      g_FileSystem = nullptr;

// Game thread handle
static pthread_t                g_GameThread;
static bool                     g_GameThreadRunning = false;

// ---------------------------------------------------------------------------
// Engine initialization
// ---------------------------------------------------------------------------
static bool InitializeEngine(Platform_Android* platform) {
    LOGI("Initializing Kapow Engine for Android...");
    LOGI("Engine version: %d.%d.%d by %s",
         34, 7, 1925, "Kasper.Fauerby"); // KAPOW_ENGINE_VERSION_*

    // Initialize file system
    g_FileSystem = new FileSystem_Android();
    if (!g_FileSystem->Initialize(
            platform->GetAssetManager(),
            platform->GetInternalDataPath(),
            platform->GetExternalDataPath())) {
        LOGE("Failed to initialize file system");
        return false;
    }

    // Initialize graphics
    if (!platform->InitDisplay()) {
        LOGE("Failed to initialize display");
        return false;
    }

    g_Gfx = new GfxInternal_GLES();
    if (!g_Gfx->Initialize(platform->GetWindowWidth(), platform->GetWindowHeight())) {
        LOGE("Failed to initialize GLES renderer");
        return false;
    }

    // Initialize audio
    g_Sound = new SoundSystem_OpenSLES();
    if (!g_Sound->Initialize()) {
        LOGW("Failed to initialize audio (continuing without sound)");
        // Non-fatal - continue without sound
    }

    // Initialize input
    g_Input = new Input_Android();
    if (!g_Input->Initialize(platform->GetWindowWidth(), platform->GetWindowHeight())) {
        LOGE("Failed to initialize input system");
        return false;
    }

    LOGI("Engine initialization complete");
    return true;
}

// ---------------------------------------------------------------------------
// Engine shutdown
// ---------------------------------------------------------------------------
static void ShutdownEngine() {
    LOGI("Shutting down Kapow Engine...");

    if (g_Input) { g_Input->Shutdown(); delete g_Input; g_Input = nullptr; }
    if (g_Sound) { g_Sound->Shutdown(); delete g_Sound; g_Sound = nullptr; }
    if (g_Gfx)   { g_Gfx->Shutdown();  delete g_Gfx;   g_Gfx = nullptr; }
    if (g_FileSystem) { g_FileSystem->Shutdown(); delete g_FileSystem; g_FileSystem = nullptr; }

    LOGI("Engine shutdown complete");
}

// ---------------------------------------------------------------------------
// Game loop (runs in its own thread)
// ---------------------------------------------------------------------------
static void* GameThreadFunc(void* arg) {
    Platform_Android* platform = (Platform_Android*)arg;

    if (!InitializeEngine(platform)) {
        LOGE("Engine initialization failed!");
        platform->RequestQuit();
        return nullptr;
    }

    LOGI("Game loop starting...");

    while (platform->IsRunning()) {
        // Process platform events
        if (!platform->ProcessEvents())
            break;

        // Skip rendering if paused or no focus
        if (platform->IsPaused() || !platform->IsFocused() || !platform->HasDisplay()) {
            usleep(16000); // ~60fps sleep to avoid spinning
            continue;
        }

        // Update input
        g_Input->Update();

        // Begin frame
        g_Gfx->BeginFrame();
        g_Gfx->Clear(0.1f, 0.1f, 0.2f, 1.0f);

        // ===================================================================
        // GAME UPDATE & RENDER GOES HERE
        //
        // This is where the engine's Scene::Update(), Node::Render(), etc.
        // would be called once the full engine port is connected.
        //
        // For now, just clear to a dark blue color to show the engine is alive.
        // ===================================================================

        // Update 3D audio
        if (g_Sound) {
            g_Sound->Update();
        }

        // End frame and swap
        g_Gfx->EndFrame();
        platform->SwapBuffers();
    }

    ShutdownEngine();
    LOGI("Game loop exited");
    g_GameThreadRunning = false;
    return nullptr;
}

// ===========================================================================
// NativeActivity callbacks
// ===========================================================================

static void onNativeWindowCreated(ANativeActivity* activity, ANativeWindow* window) {
    LOGI("onNativeWindowCreated");
    Platform_Android* platform = Platform_Android::GetInstance();
    if (platform) {
        platform->SetNativeWindow(window);
        platform->ProcessAppCommand(APP_CMD_INIT_WINDOW);

        // Start game thread if not running
        if (!g_GameThreadRunning) {
            g_GameThreadRunning = true;
            pthread_create(&g_GameThread, nullptr, GameThreadFunc, platform);
        }
    }
}

static void onNativeWindowDestroyed(ANativeActivity* activity, ANativeWindow* window) {
    LOGI("onNativeWindowDestroyed");
    Platform_Android* platform = Platform_Android::GetInstance();
    if (platform) {
        platform->ProcessAppCommand(APP_CMD_TERM_WINDOW);
        platform->SetNativeWindow(nullptr);
    }
}

static void onNativeWindowResized(ANativeActivity* activity, ANativeWindow* window) {
    LOGI("onNativeWindowResized");
    Platform_Android* platform = Platform_Android::GetInstance();
    if (platform) {
        platform->SetNativeWindow(window);
        platform->ProcessAppCommand(APP_CMD_WINDOW_RESIZED);
        if (g_Input) {
            g_Input->SetScreenSize(platform->GetWindowWidth(), platform->GetWindowHeight());
        }
    }
}

static void onWindowFocusChanged(ANativeActivity* activity, int hasFocus) {
    LOGI("onWindowFocusChanged: %d", hasFocus);
    Platform_Android* platform = Platform_Android::GetInstance();
    if (platform) {
        platform->ProcessAppCommand(hasFocus ? APP_CMD_GAINED_FOCUS : APP_CMD_LOST_FOCUS);
    }
}

static void onPause(ANativeActivity* activity) {
    LOGI("onPause");
    Platform_Android* platform = Platform_Android::GetInstance();
    if (platform) {
        platform->ProcessAppCommand(APP_CMD_PAUSE);
    }
}

static void onResume(ANativeActivity* activity) {
    LOGI("onResume");
    Platform_Android* platform = Platform_Android::GetInstance();
    if (platform) {
        platform->ProcessAppCommand(APP_CMD_RESUME);
    }
}

static void onDestroy(ANativeActivity* activity) {
    LOGI("onDestroy");
    Platform_Android* platform = Platform_Android::GetInstance();
    if (platform) {
        platform->ProcessAppCommand(APP_CMD_DESTROY);

        // Wait for game thread to finish
        if (g_GameThreadRunning) {
            pthread_join(g_GameThread, nullptr);
            g_GameThreadRunning = false;
        }

        Platform_Android::Destroy();
    }
}

static void onStart(ANativeActivity* activity) {
    LOGI("onStart");
}

static void onStop(ANativeActivity* activity) {
    LOGI("onStop");
}

static void onConfigurationChanged(ANativeActivity* activity) {
    LOGI("onConfigurationChanged");
    Platform_Android* platform = Platform_Android::GetInstance();
    if (platform) {
        platform->ProcessAppCommand(APP_CMD_CONFIG_CHANGED);
    }
}

static void onLowMemory(ANativeActivity* activity) {
    LOGW("onLowMemory");
    Platform_Android* platform = Platform_Android::GetInstance();
    if (platform) {
        platform->ProcessAppCommand(APP_CMD_LOW_MEMORY);
    }
}

static void* onSaveInstanceState(ANativeActivity* activity, size_t* outSize) {
    LOGI("onSaveInstanceState");
    // Could save game state here
    *outSize = 0;
    return nullptr;
}

static int32_t onInputEvent(ANativeActivity* activity, AInputEvent* event) {
    if (g_Input) {
        return g_Input->HandleInputEvent(event);
    }
    return 0;
}

// ===========================================================================
// ANativeActivity_onCreate - Android entry point
// ===========================================================================
void ANativeActivity_onCreate(ANativeActivity* activity, void* savedState, size_t savedStateSize) {
    LOGI("==============================================");
    LOGI("  Kapow Engine - Total Overdose (Android)");
    LOGI("  Version %d.%d.%d", 34, 7, 1925);
    LOGI("==============================================");

    // Create platform instance
    Platform_Android::Create(activity, savedState, savedStateSize);

    // Register native activity callbacks
    activity->callbacks->onNativeWindowCreated = onNativeWindowCreated;
    activity->callbacks->onNativeWindowDestroyed = onNativeWindowDestroyed;
    activity->callbacks->onNativeWindowResized = onNativeWindowResized;
    activity->callbacks->onWindowFocusChanged = onWindowFocusChanged;
    activity->callbacks->onPause = onPause;
    activity->callbacks->onResume = onResume;
    activity->callbacks->onDestroy = onDestroy;
    activity->callbacks->onStart = onStart;
    activity->callbacks->onStop = onStop;
    activity->callbacks->onConfigurationChanged = onConfigurationChanged;
    activity->callbacks->onLowMemory = onLowMemory;
    activity->callbacks->onSaveInstanceState = onSaveInstanceState;
    activity->callbacks->onInputQueueCreated = nullptr;  // Using onInputEvent instead
    activity->callbacks->onInputQueueDestroyed = nullptr;

    activity->instance = Platform_Android::GetInstance();
}
