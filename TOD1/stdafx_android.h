/*
 *
 * Android port of the Kapow Engine (Total Overdose)
 *
 * This header replaces stdafx.h for Android/NDK builds.
 * It provides compatibility types and macros to replace Windows-specific APIs.
 *
 * Preprocessor defines:
 * PLATFORM_ANDROID -- use Android-specific code
 * INCLUDE_FIXES    -- includes fixes to obvious bugs and improvements
 *
 */
#pragma once

#define PLATFORM_ANDROID

#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <ctime>
#include <cassert>
#include <list>
#include <vector>
#include <map>
#include <string>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>
#include <android/log.h>
#include <android/asset_manager.h>
#include <android/native_window.h>

// ---------------------------------------------------------------------------
// Windows type compatibility layer
// ---------------------------------------------------------------------------
typedef uint8_t     BYTE;
typedef uint16_t    WORD;
typedef uint32_t    DWORD;
typedef int32_t     LONG;
typedef uint64_t    ULONGLONG;
typedef int32_t     BOOL;
typedef int32_t     HRESULT;
typedef void*       HANDLE;
typedef void*       HMODULE;
typedef void*       HINSTANCE;
typedef void*       HWND;
typedef void*       HCURSOR;
typedef void*       LPVOID;
typedef DWORD*      LPDWORD;
typedef const char* LPCSTR;
typedef char*       LPSTR;
typedef uint16_t    UINT16;
typedef uint16_t    ATOM;
typedef uintptr_t   WPARAM;
typedef intptr_t    LPARAM;
typedef intptr_t    LRESULT;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef NULL
#define NULL 0
#endif

#define CALLBACK
#define __stdcall
#define __cdecl
#define __declspec(x)
#define APIENTRY

// S_OK / FAILED / SUCCEEDED
#ifndef S_OK
#define S_OK        ((HRESULT)0L)
#define S_FALSE     ((HRESULT)1L)
#define E_FAIL      ((HRESULT)0x80004005L)
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr)    (((HRESULT)(hr)) < 0)
#endif

#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)

// ULARGE_INTEGER compatibility
typedef union _ULARGE_INTEGER {
    struct {
        DWORD LowPart;
        DWORD HighPart;
    };
    ULONGLONG QuadPart;
} ULARGE_INTEGER;

// GUID compatibility
typedef struct _GUID {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t  Data4[8];
} GUID;

typedef const GUID& IID;

// tagPOINT compatibility
typedef struct tagPOINT {
    LONG x;
    LONG y;
} POINT;

// SYSTEMTIME compatibility
typedef struct _SYSTEMTIME {
    uint16_t wYear;
    uint16_t wMonth;
    uint16_t wDayOfWeek;
    uint16_t wDay;
    uint16_t wHour;
    uint16_t wMinute;
    uint16_t wSecond;
    uint16_t wMilliseconds;
} SYSTEMTIME;

// RECT compatibility
typedef struct _RECT {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECT;

// ---------------------------------------------------------------------------
// CRITICAL_SECTION -> pthread_mutex
// ---------------------------------------------------------------------------
typedef struct _RTL_CRITICAL_SECTION {
    pthread_mutex_t mutex;
} RTL_CRITICAL_SECTION, CRITICAL_SECTION;

static inline void InitializeCriticalSection(CRITICAL_SECTION* cs) {
    pthread_mutex_init(&cs->mutex, nullptr);
}
static inline void DeleteCriticalSection(CRITICAL_SECTION* cs) {
    pthread_mutex_destroy(&cs->mutex);
}
static inline void EnterCriticalSection(CRITICAL_SECTION* cs) {
    pthread_mutex_lock(&cs->mutex);
}
static inline void LeaveCriticalSection(CRITICAL_SECTION* cs) {
    pthread_mutex_unlock(&cs->mutex);
}

// ---------------------------------------------------------------------------
// Time functions
// ---------------------------------------------------------------------------
static inline DWORD timeGetTime() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (DWORD)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static inline void GetLocalTime(SYSTEMTIME* st) {
    time_t t = time(nullptr);
    struct tm* lt = localtime(&t);
    st->wYear = lt->tm_year + 1900;
    st->wMonth = lt->tm_mon + 1;
    st->wDay = lt->tm_mday;
    st->wDayOfWeek = lt->tm_wday;
    st->wHour = lt->tm_hour;
    st->wMinute = lt->tm_min;
    st->wSecond = lt->tm_sec;
    st->wMilliseconds = 0;
}

static inline void Sleep(DWORD milliseconds) {
    usleep(milliseconds * 1000);
}

// ---------------------------------------------------------------------------
// Event/thread helpers (simplified emulation via pthreads)
// ---------------------------------------------------------------------------
static inline HANDLE CreateEvent(void*, BOOL, BOOL, const char*) {
    // Stub - real implementation in platform layer
    return nullptr;
}

static inline BOOL SetEvent(HANDLE) {
    return TRUE;
}

static inline BOOL ResetEvent(HANDLE) {
    return TRUE;
}

static inline DWORD WaitForSingleObject(HANDLE, DWORD) {
    return 0;
}

#define WAIT_OBJECT_0 0
#define INFINITE 0xFFFFFFFF

// ---------------------------------------------------------------------------
// String compatibility
// ---------------------------------------------------------------------------
#define sprintf_s snprintf
#define _snprintf snprintf
#define _vsnprintf vsnprintf
#define sscanf_s sscanf
#define fopen_s(pFile, filename, mode) (*(pFile) = fopen((filename),(mode)))
#define strcpy_s(dst, size, src) strncpy((dst), (src), (size))
#define strcat_s(dst, size, src) strncat((dst), (src), (size) - strlen(dst) - 1)
#define _stricmp strcasecmp
#define _strnicmp strncasecmp

// ---------------------------------------------------------------------------
// SSE/SIMD -> NEON compatibility
// ---------------------------------------------------------------------------
#if defined(__aarch64__) || defined(__ARM_NEON)
#include <arm_neon.h>
typedef float32x4_t __m128;
#else
// Fallback: plain struct for non-NEON ARM
typedef struct { float m128_f32[4]; } __m128;
#endif

// ---------------------------------------------------------------------------
// __rdtsc replacement
// ---------------------------------------------------------------------------
static inline uint64_t __rdtsc() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

// ---------------------------------------------------------------------------
// Kapow Engine version
// ---------------------------------------------------------------------------
#define KAPOW_ENGINE_VERSION_MAJOR 34
#define KAPOW_ENGINE_VERSION_MINOR 7
#define KAPOW_ENGINE_VERSION_BUILD 1925
#define KAPOW_ENGINE_BUILDBY "Kasper.Fauerby"

#define MESSAGE_NOT_IMPLEMENTED(x) debug(#x " is not implemented!\n")
#define MESSAGE_WRONG_CLASS_SIZE(x) "Wrong size for " #x " class!"
#define MESSAGE_CLASS_CREATED(x) debug(#x " created at %X\n", this)
#define MESSAGE_CLASS_DESTROYED(x) debug(#x " destroyed!\n")
#define ASSERT_CLASS_SIZE(x, size) static_assert(sizeof(x) == size, MESSAGE_WRONG_CLASS_SIZE(x))

#define ALIGN_4BYTES(x)    ((int)(x) & 0xFFFFFFFC)
#define ALIGN_4BYTESUP(x)  ((int)(x + 3) & 0xFFFFFFFC)
#define ALIGN_8BYTESUP(x)  ((int)(x + 7) & 0xFFFFFFF8)
#define ALIGN_16BYTESUP(x) ((uint32_t)(x + 15) & 0xFFFFFFF0)
#define ALIGN_64BYTESUP(x) ((uint32_t)(x + 63) & 0xFFFFFFC0)
#define D3DCOLOR_DWORD(r, g, b, a) (DWORD)((unsigned char)(b * 255.f) | (((unsigned char)(g * 255.f) | (((unsigned char)(r * 255.f) | ((unsigned char)(a * 255.f) << 8)) << 8)) << 8))
#define DEG2RAD(deg) (0.017453292f * (deg))

// No-op the DX release macro
#define RELEASE_SAFE(p) \
    if (p) { \
        p = nullptr; \
    }

#ifdef INCLUDE_FIXES
#define Stringify( L ) #L
#define MakeString( M, L ) M(L)
#define $Line MakeString( Stringify, __LINE__ )
#define TODO_IMPLEMENTATION __FILE__ "(" $Line "): TODO: implementation!"
#else
#define TODO_IMPLEMENTATION
#endif

// Script entity macros (same as original, no Windows deps)
#define DECLARE_SCRIPT_ENTITY_CLASS(className, baseClassName) \
    class className : public baseClassName \
    { \
    protected: \
        virtual ~className(); \
        className(); \

#define DECLARE_SCRIPT_ENTITY_PROPERTY(propName, propType, propScriptType) \
    private: \
        propScriptType propName; \
    \
    public: \
        propType Get_ ## propName(); \
        void  Set_ ## propName(propType); \

#define DECLARE_SCRIPT_ENTITY_CLASS_END(className, classSize) \
    public: \
        static void Register(); \
        static className* Create(AllocatorIndex); \
    }; \
    \
    extern EntityType* t##className; \
    \
    ASSERT_CLASS_SIZE(className, classSize);

// Memory patching stubs (not used on Android - engine runs natively)
#define PATCH_NOTHING       0x00
#define PATCH_CALL          0xE8
#define PATCH_JUMP_SHORT    0xEB
#define PATCH_JUMP          0xE9

#define nop(a, s)  ((void)0)
static inline void hook(uintptr_t, void*, uint8_t) { /* no-op on Android */ }

extern void debug(char* message, ...);

template <typename T>
static inline T clamp(T val, T min, T max)
{
    if (val < min)
        return min;
    else if (val > max)
        return max;
    else
        return val;
}

// No-op on Android - patches are not needed since we build natively
static inline void PATCH_WINDOW() {}
