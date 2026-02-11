/*
 * Platform_Android.h
 *
 * Android platform layer replacing the Windows Platform class.
 * Manages the native window, lifecycle, and core platform services.
 */
#pragma once

#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/asset_manager.h>
#include <android/configuration.h>
#include <android/looper.h>
#include <android/log.h>

#include <EGL/egl.h>
#include <string>
#include <functional>
#include <atomic>
#include <pthread.h>

#define LOG_TAG "KapowEngine"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
class GfxInternal_GLES;
class SoundSystem_OpenSLES;

// ---------------------------------------------------------------------------
// App lifecycle commands sent from the UI thread to the game thread
// ---------------------------------------------------------------------------
enum AppCommand {
    APP_CMD_NONE = 0,
    APP_CMD_INIT_WINDOW,
    APP_CMD_TERM_WINDOW,
    APP_CMD_WINDOW_RESIZED,
    APP_CMD_GAINED_FOCUS,
    APP_CMD_LOST_FOCUS,
    APP_CMD_PAUSE,
    APP_CMD_RESUME,
    APP_CMD_SAVE_STATE,
    APP_CMD_DESTROY,
    APP_CMD_CONFIG_CHANGED,
    APP_CMD_LOW_MEMORY,
};

// ---------------------------------------------------------------------------
// Saved state structure
// ---------------------------------------------------------------------------
struct SavedState {
    float   cameraX;
    float   cameraY;
    float   cameraZ;
    int     currentLevel;
};

// ---------------------------------------------------------------------------
// Platform_Android - core Android platform class
// ---------------------------------------------------------------------------
class Platform_Android {
public:
    // Lifecycle
    static Platform_Android*    Create(ANativeActivity* activity, void* savedState, size_t savedStateSize);
    static void                 Destroy();
    static Platform_Android*    GetInstance();

    // Window management
    void    SetNativeWindow(ANativeWindow* window);
    ANativeWindow* GetNativeWindow() const { return m_Window; }
    int     GetWindowWidth() const;
    int     GetWindowHeight() const;

    // EGL surface management
    bool    InitDisplay();
    void    TermDisplay();
    bool    HasDisplay() const { return m_EGLDisplay != EGL_NO_DISPLAY; }

    EGLDisplay  GetEGLDisplay() const { return m_EGLDisplay; }
    EGLSurface  GetEGLSurface() const { return m_EGLSurface; }
    EGLContext  GetEGLContext() const { return m_EGLContext; }

    // Asset manager
    AAssetManager*  GetAssetManager() const { return m_AssetManager; }
    const char*     GetInternalDataPath() const { return m_InternalDataPath.c_str(); }
    const char*     GetExternalDataPath() const { return m_ExternalDataPath.c_str(); }

    // Lifecycle queries
    bool    IsRunning() const { return m_Running.load(); }
    bool    IsFocused() const { return m_HasFocus.load(); }
    bool    IsPaused() const { return m_Paused.load(); }
    void    RequestQuit() { m_Running.store(false); }

    // Game loop
    void    ProcessAppCommand(AppCommand cmd);
    bool    ProcessEvents();
    void    SwapBuffers();

    // Configuration
    void    SetGameDataPath(const char* path);
    int     GetScreenDPI() const;

    // Time
    double  GetTimeSeconds() const;
    uint32_t GetTimeMillis() const;

private:
    Platform_Android();
    ~Platform_Android();

    bool    InitEGL();
    void    TermEGL();

    // Android handles
    ANativeActivity*    m_Activity;
    ANativeWindow*      m_Window;
    AAssetManager*      m_AssetManager;
    AConfiguration*     m_Config;

    // EGL state
    EGLDisplay          m_EGLDisplay;
    EGLSurface          m_EGLSurface;
    EGLContext          m_EGLContext;
    EGLConfig           m_EGLConfig;
    int                 m_Width;
    int                 m_Height;

    // Paths
    std::string         m_InternalDataPath;
    std::string         m_ExternalDataPath;
    std::string         m_GameDataPath;

    // State
    std::atomic<bool>   m_Running;
    std::atomic<bool>   m_HasFocus;
    std::atomic<bool>   m_Paused;
    SavedState          m_SavedState;

    // Singleton
    static Platform_Android* s_Instance;
};
