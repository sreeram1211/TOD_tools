/*
 * Input_Android.h
 *
 * Android input system replacing DirectInput.
 * Handles touch, gamepad, keyboard (physical & virtual), and accelerometer input.
 */
#pragma once

#include <android/input.h>
#include <android/keycodes.h>
#include <android/sensor.h>
#include <cstdint>
#include <vector>
#include <mutex>

// ---------------------------------------------------------------------------
// Touch input
// ---------------------------------------------------------------------------
struct TouchPoint {
    int32_t     id;
    float       x;
    float       y;
    float       pressure;
    bool        active;
};

#define MAX_TOUCH_POINTS 10

// ---------------------------------------------------------------------------
// Virtual gamepad for touch-screen overlay
// ---------------------------------------------------------------------------
struct VirtualButton {
    float   centerX, centerY;  // Normalized [0..1]
    float   radius;            // Normalized
    int     mappedButton;      // Maps to engine's button index
    bool    pressed;
};

struct VirtualStick {
    float   centerX, centerY;  // Normalized
    float   radius;
    float   deadZone;
    float   axisX, axisY;      // Output: -1..1
    int     activePointerId;
};

// ---------------------------------------------------------------------------
// Input_Android - main input handler
// ---------------------------------------------------------------------------
class Input_Android {
public:
    Input_Android();
    ~Input_Android();

    bool    Initialize(int screenWidth, int screenHeight);
    void    Shutdown();

    // Process raw Android input events (call from native activity callback)
    int32_t HandleInputEvent(AInputEvent* event);

    // Per-frame update
    void    Update();

    // ----- Touch queries -----
    int     GetTouchCount() const;
    bool    GetTouch(int index, float& outX, float& outY, float& outPressure) const;
    bool    IsTouching() const;

    // ----- Keyboard queries (physical keyboard or virtual keys) -----
    bool    IsKeyDown(int32_t keyCode) const;
    bool    IsKeyPressed(int32_t keyCode) const;     // Just pressed this frame
    bool    IsKeyReleased(int32_t keyCode) const;    // Just released this frame

    // ----- Gamepad queries (physical gamepad via Bluetooth/USB) -----
    bool    IsGamepadConnected() const;
    float   GetGamepadAxis(int axis) const;          // Axis index: 0=LX, 1=LY, 2=RX, 3=RY, 4=LT, 5=RT
    bool    IsGamepadButtonDown(int button) const;
    bool    IsGamepadButtonPressed(int button) const;

    // ----- Virtual on-screen controls -----
    void    SetupVirtualControls();
    bool    GetVirtualStickLeft(float& outX, float& outY) const;
    bool    GetVirtualStickRight(float& outX, float& outY) const;
    bool    IsVirtualButtonDown(int buttonIndex) const;

    // ----- Accelerometer -----
    bool    EnableAccelerometer(bool enable);
    void    GetAccelerometer(float& outX, float& outY, float& outZ) const;

    // ----- Mapping from Android keycodes to original engine keycodes -----
    static int  MapAndroidKeyToEngineKey(int32_t androidKeyCode);
    static int  MapGamepadButtonToEngineButton(int32_t androidButton);

    // Screen dimensions (for normalizing touch)
    void    SetScreenSize(int width, int height);

    // Singleton
    static Input_Android* GetInstance() { return s_Instance; }

private:
    int32_t HandleTouchEvent(AInputEvent* event);
    int32_t HandleKeyEvent(AInputEvent* event);
    int32_t HandleGamepadEvent(AInputEvent* event);

    // Touch state
    TouchPoint      m_TouchPoints[MAX_TOUCH_POINTS];
    int             m_TouchCount;

    // Keyboard state
    bool            m_KeyState[512];        // Current frame
    bool            m_KeyStatePrev[512];    // Previous frame

    // Gamepad state
    float           m_GamepadAxes[8];
    bool            m_GamepadButtons[32];
    bool            m_GamepadButtonsPrev[32];
    bool            m_GamepadConnected;

    // Virtual controls
    VirtualStick    m_VirtualStickLeft;
    VirtualStick    m_VirtualStickRight;
    std::vector<VirtualButton> m_VirtualButtons;

    // Accelerometer
    ASensorManager*     m_SensorManager;
    const ASensor*      m_AccelerometerSensor;
    ASensorEventQueue*  m_SensorQueue;
    float               m_AccelX, m_AccelY, m_AccelZ;
    bool                m_AccelEnabled;

    // Screen info
    int             m_ScreenWidth;
    int             m_ScreenHeight;

    mutable std::mutex m_InputMutex;
    static Input_Android* s_Instance;
};
