/*
 * Platform_Android.cpp
 *
 * Android platform implementation.
 * Manages EGL display, native window, and application lifecycle.
 */
#include "Platform_Android.h"
#include <GLES3/gl3.h>
#include <cstring>
#include <ctime>

Platform_Android* Platform_Android::s_Instance = nullptr;

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------
Platform_Android::Platform_Android()
    : m_Activity(nullptr)
    , m_Window(nullptr)
    , m_AssetManager(nullptr)
    , m_Config(nullptr)
    , m_EGLDisplay(EGL_NO_DISPLAY)
    , m_EGLSurface(EGL_NO_SURFACE)
    , m_EGLContext(EGL_NO_CONTEXT)
    , m_EGLConfig(nullptr)
    , m_Width(0)
    , m_Height(0)
    , m_Running(true)
    , m_HasFocus(false)
    , m_Paused(true)
{
    memset(&m_SavedState, 0, sizeof(m_SavedState));
}

Platform_Android::~Platform_Android()
{
    TermEGL();
    if (m_Config) {
        AConfiguration_delete(m_Config);
        m_Config = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------
Platform_Android* Platform_Android::Create(ANativeActivity* activity, void* savedState, size_t savedStateSize)
{
    if (s_Instance) {
        LOGW("Platform_Android already created");
        return s_Instance;
    }

    s_Instance = new Platform_Android();
    s_Instance->m_Activity = activity;
    s_Instance->m_AssetManager = activity->assetManager;

    if (activity->internalDataPath)
        s_Instance->m_InternalDataPath = activity->internalDataPath;
    if (activity->externalDataPath)
        s_Instance->m_ExternalDataPath = activity->externalDataPath;

    s_Instance->m_Config = AConfiguration_new();
    AConfiguration_fromAssetManager(s_Instance->m_Config, activity->assetManager);

    if (savedState && savedStateSize == sizeof(SavedState)) {
        memcpy(&s_Instance->m_SavedState, savedState, sizeof(SavedState));
        LOGI("Restored saved state: level=%d", s_Instance->m_SavedState.currentLevel);
    }

    LOGI("Platform_Android created. Internal: %s, External: %s",
         s_Instance->m_InternalDataPath.c_str(),
         s_Instance->m_ExternalDataPath.c_str());

    return s_Instance;
}

void Platform_Android::Destroy()
{
    if (s_Instance) {
        delete s_Instance;
        s_Instance = nullptr;
    }
}

Platform_Android* Platform_Android::GetInstance()
{
    return s_Instance;
}

// ---------------------------------------------------------------------------
// Window management
// ---------------------------------------------------------------------------
void Platform_Android::SetNativeWindow(ANativeWindow* window)
{
    m_Window = window;
    if (window) {
        m_Width = ANativeWindow_getWidth(window);
        m_Height = ANativeWindow_getHeight(window);
        LOGI("Native window set: %dx%d", m_Width, m_Height);
    }
}

int Platform_Android::GetWindowWidth() const
{
    if (m_Window)
        return ANativeWindow_getWidth(m_Window);
    return m_Width;
}

int Platform_Android::GetWindowHeight() const
{
    if (m_Window)
        return ANativeWindow_getHeight(m_Window);
    return m_Height;
}

// ---------------------------------------------------------------------------
// EGL initialization
// ---------------------------------------------------------------------------
bool Platform_Android::InitEGL()
{
    m_EGLDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (m_EGLDisplay == EGL_NO_DISPLAY) {
        LOGE("eglGetDisplay failed");
        return false;
    }

    EGLint major, minor;
    if (!eglInitialize(m_EGLDisplay, &major, &minor)) {
        LOGE("eglInitialize failed");
        return false;
    }
    LOGI("EGL initialized: %d.%d", major, minor);

    // Request OpenGL ES 3.0 context with 24-bit depth, 8-bit stencil
    const EGLint attribs[] = {
        EGL_RENDERABLE_TYPE,    EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE,       EGL_WINDOW_BIT,
        EGL_RED_SIZE,           8,
        EGL_GREEN_SIZE,         8,
        EGL_BLUE_SIZE,          8,
        EGL_ALPHA_SIZE,         8,
        EGL_DEPTH_SIZE,         24,
        EGL_STENCIL_SIZE,       8,
        EGL_NONE
    };

    EGLint numConfigs;
    eglChooseConfig(m_EGLDisplay, attribs, &m_EGLConfig, 1, &numConfigs);
    if (numConfigs == 0) {
        LOGE("eglChooseConfig failed, no matching config");
        // Fallback: try ES 2.0
        const EGLint fallbackAttribs[] = {
            EGL_RENDERABLE_TYPE,    EGL_OPENGL_ES2_BIT,
            EGL_SURFACE_TYPE,       EGL_WINDOW_BIT,
            EGL_RED_SIZE,           8,
            EGL_GREEN_SIZE,         8,
            EGL_BLUE_SIZE,          8,
            EGL_DEPTH_SIZE,         16,
            EGL_NONE
        };
        eglChooseConfig(m_EGLDisplay, fallbackAttribs, &m_EGLConfig, 1, &numConfigs);
        if (numConfigs == 0) {
            LOGE("EGL fallback config also failed");
            return false;
        }
        LOGW("Using ES 2.0 fallback config");
    }

    // Set native window buffer format
    EGLint format;
    eglGetConfigAttrib(m_EGLDisplay, m_EGLConfig, EGL_NATIVE_VISUAL_ID, &format);
    ANativeWindow_setBuffersGeometry(m_Window, 0, 0, format);

    // Create surface
    m_EGLSurface = eglCreateWindowSurface(m_EGLDisplay, m_EGLConfig, m_Window, nullptr);
    if (m_EGLSurface == EGL_NO_SURFACE) {
        LOGE("eglCreateWindowSurface failed");
        return false;
    }

    // Create OpenGL ES 3.0 context
    const EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };
    m_EGLContext = eglCreateContext(m_EGLDisplay, m_EGLConfig, EGL_NO_CONTEXT, contextAttribs);
    if (m_EGLContext == EGL_NO_CONTEXT) {
        // Fallback to ES 2.0
        const EGLint ctx2Attribs[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2,
            EGL_NONE
        };
        m_EGLContext = eglCreateContext(m_EGLDisplay, m_EGLConfig, EGL_NO_CONTEXT, ctx2Attribs);
        if (m_EGLContext == EGL_NO_CONTEXT) {
            LOGE("eglCreateContext failed");
            return false;
        }
        LOGW("Using OpenGL ES 2.0 context");
    }

    if (!eglMakeCurrent(m_EGLDisplay, m_EGLSurface, m_EGLSurface, m_EGLContext)) {
        LOGE("eglMakeCurrent failed");
        return false;
    }

    eglQuerySurface(m_EGLDisplay, m_EGLSurface, EGL_WIDTH, &m_Width);
    eglQuerySurface(m_EGLDisplay, m_EGLSurface, EGL_HEIGHT, &m_Height);

    LOGI("EGL surface: %dx%d, GL_VENDOR: %s, GL_RENDERER: %s",
         m_Width, m_Height,
         glGetString(GL_VENDOR),
         glGetString(GL_RENDERER));

    return true;
}

void Platform_Android::TermEGL()
{
    if (m_EGLDisplay != EGL_NO_DISPLAY) {
        eglMakeCurrent(m_EGLDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (m_EGLContext != EGL_NO_CONTEXT) {
            eglDestroyContext(m_EGLDisplay, m_EGLContext);
            m_EGLContext = EGL_NO_CONTEXT;
        }
        if (m_EGLSurface != EGL_NO_SURFACE) {
            eglDestroySurface(m_EGLDisplay, m_EGLSurface);
            m_EGLSurface = EGL_NO_SURFACE;
        }
        eglTerminate(m_EGLDisplay);
        m_EGLDisplay = EGL_NO_DISPLAY;
    }
}

bool Platform_Android::InitDisplay()
{
    if (!m_Window) {
        LOGE("InitDisplay called with no native window");
        return false;
    }
    return InitEGL();
}

void Platform_Android::TermDisplay()
{
    TermEGL();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
void Platform_Android::ProcessAppCommand(AppCommand cmd)
{
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (m_Window) {
                InitDisplay();
            }
            break;

        case APP_CMD_TERM_WINDOW:
            TermDisplay();
            break;

        case APP_CMD_GAINED_FOCUS:
            m_HasFocus.store(true);
            break;

        case APP_CMD_LOST_FOCUS:
            m_HasFocus.store(false);
            break;

        case APP_CMD_PAUSE:
            m_Paused.store(true);
            break;

        case APP_CMD_RESUME:
            m_Paused.store(false);
            break;

        case APP_CMD_DESTROY:
            m_Running.store(false);
            break;

        case APP_CMD_LOW_MEMORY:
            LOGW("Low memory warning received");
            break;

        case APP_CMD_CONFIG_CHANGED:
            if (m_Config)
                AConfiguration_fromAssetManager(m_Config, m_AssetManager);
            break;

        default:
            break;
    }
}

bool Platform_Android::ProcessEvents()
{
    // Returns true if the game loop should continue
    return m_Running.load();
}

void Platform_Android::SwapBuffers()
{
    if (m_EGLDisplay != EGL_NO_DISPLAY && m_EGLSurface != EGL_NO_SURFACE) {
        eglSwapBuffers(m_EGLDisplay, m_EGLSurface);
    }
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
void Platform_Android::SetGameDataPath(const char* path)
{
    m_GameDataPath = path;
}

int Platform_Android::GetScreenDPI() const
{
    if (m_Config)
        return AConfiguration_getDensity(m_Config);
    return 160; // Default mdpi
}

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------
double Platform_Android::GetTimeSeconds() const
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

uint32_t Platform_Android::GetTimeMillis() const
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}
