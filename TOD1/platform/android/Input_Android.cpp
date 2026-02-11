/*
 * Input_Android.cpp
 *
 * Android input system implementation.
 * Replaces DirectInput with Android NDK input handling.
 */
#include "Input_Android.h"
#include "Platform_Android.h"
#include <cstring>
#include <cmath>

Input_Android* Input_Android::s_Instance = nullptr;

Input_Android::Input_Android()
    : m_TouchCount(0)
    , m_GamepadConnected(false)
    , m_SensorManager(nullptr)
    , m_AccelerometerSensor(nullptr)
    , m_SensorQueue(nullptr)
    , m_AccelX(0), m_AccelY(0), m_AccelZ(0)
    , m_AccelEnabled(false)
    , m_ScreenWidth(1920)
    , m_ScreenHeight(1080)
{
    memset(m_TouchPoints, 0, sizeof(m_TouchPoints));
    memset(m_KeyState, 0, sizeof(m_KeyState));
    memset(m_KeyStatePrev, 0, sizeof(m_KeyStatePrev));
    memset(m_GamepadAxes, 0, sizeof(m_GamepadAxes));
    memset(m_GamepadButtons, 0, sizeof(m_GamepadButtons));
    memset(m_GamepadButtonsPrev, 0, sizeof(m_GamepadButtonsPrev));
    memset(&m_VirtualStickLeft, 0, sizeof(m_VirtualStickLeft));
    memset(&m_VirtualStickRight, 0, sizeof(m_VirtualStickRight));
}

Input_Android::~Input_Android() {
    Shutdown();
}

bool Input_Android::Initialize(int screenWidth, int screenHeight) {
    s_Instance = this;
    m_ScreenWidth = screenWidth;
    m_ScreenHeight = screenHeight;

    // Setup virtual controls layout for touch screens
    SetupVirtualControls();

    LOGI("Input_Android initialized: screen %dx%d", screenWidth, screenHeight);
    return true;
}

void Input_Android::Shutdown() {
    if (m_SensorQueue && m_AccelerometerSensor) {
        ASensorEventQueue_disableSensor(m_SensorQueue, m_AccelerometerSensor);
    }
    s_Instance = nullptr;
}

void Input_Android::SetScreenSize(int width, int height) {
    m_ScreenWidth = width;
    m_ScreenHeight = height;
}

// ---------------------------------------------------------------------------
// Event handling
// ---------------------------------------------------------------------------
int32_t Input_Android::HandleInputEvent(AInputEvent* event) {
    int32_t type = AInputEvent_getType(event);
    int32_t source = AInputEvent_getSource(event);

    if (type == AINPUT_EVENT_TYPE_MOTION) {
        if ((source & AINPUT_SOURCE_TOUCHSCREEN) == AINPUT_SOURCE_TOUCHSCREEN) {
            return HandleTouchEvent(event);
        }
        if ((source & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK) {
            return HandleGamepadEvent(event);
        }
    } else if (type == AINPUT_EVENT_TYPE_KEY) {
        return HandleKeyEvent(event);
    }

    return 0;
}

int32_t Input_Android::HandleTouchEvent(AInputEvent* event) {
    std::lock_guard<std::mutex> lock(m_InputMutex);

    int32_t action = AMotionEvent_getAction(event);
    int32_t actionMasked = action & AMOTION_EVENT_ACTION_MASK;
    int32_t pointerIndex = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                            >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;

    size_t pointerCount = AMotionEvent_getPointerCount(event);
    m_TouchCount = 0;

    for (size_t i = 0; i < pointerCount && i < MAX_TOUCH_POINTS; i++) {
        int32_t id = AMotionEvent_getPointerId(event, i);

        m_TouchPoints[i].id = id;
        m_TouchPoints[i].x = AMotionEvent_getX(event, i);
        m_TouchPoints[i].y = AMotionEvent_getY(event, i);
        m_TouchPoints[i].pressure = AMotionEvent_getPressure(event, i);

        switch (actionMasked) {
            case AMOTION_EVENT_ACTION_DOWN:
            case AMOTION_EVENT_ACTION_POINTER_DOWN:
                m_TouchPoints[i].active = true;
                break;
            case AMOTION_EVENT_ACTION_UP:
            case AMOTION_EVENT_ACTION_CANCEL:
                m_TouchPoints[i].active = false;
                break;
            case AMOTION_EVENT_ACTION_POINTER_UP:
                if ((int)i == pointerIndex)
                    m_TouchPoints[i].active = false;
                else
                    m_TouchPoints[i].active = true;
                break;
            case AMOTION_EVENT_ACTION_MOVE:
                m_TouchPoints[i].active = true;
                break;
        }

        if (m_TouchPoints[i].active)
            m_TouchCount++;
    }

    // Update virtual controls from touch input
    for (auto& btn : m_VirtualButtons) {
        btn.pressed = false;
        for (int i = 0; i < (int)pointerCount && i < MAX_TOUCH_POINTS; i++) {
            if (!m_TouchPoints[i].active) continue;
            float nx = m_TouchPoints[i].x / m_ScreenWidth;
            float ny = m_TouchPoints[i].y / m_ScreenHeight;
            float dx = nx - btn.centerX;
            float dy = ny - btn.centerY;
            if (sqrtf(dx*dx + dy*dy) <= btn.radius) {
                btn.pressed = true;
                break;
            }
        }
    }

    // Update virtual sticks
    auto updateStick = [&](VirtualStick& stick) {
        stick.axisX = 0;
        stick.axisY = 0;
        stick.activePointerId = -1;
        for (int i = 0; i < (int)pointerCount && i < MAX_TOUCH_POINTS; i++) {
            if (!m_TouchPoints[i].active) continue;
            float nx = m_TouchPoints[i].x / m_ScreenWidth;
            float ny = m_TouchPoints[i].y / m_ScreenHeight;
            float dx = nx - stick.centerX;
            float dy = ny - stick.centerY;
            float dist = sqrtf(dx*dx + dy*dy);
            if (dist <= stick.radius) {
                if (dist > stick.deadZone) {
                    stick.axisX = dx / stick.radius;
                    stick.axisY = dy / stick.radius;
                    // Clamp to unit circle
                    float mag = sqrtf(stick.axisX * stick.axisX + stick.axisY * stick.axisY);
                    if (mag > 1.0f) {
                        stick.axisX /= mag;
                        stick.axisY /= mag;
                    }
                }
                stick.activePointerId = m_TouchPoints[i].id;
                break;
            }
        }
    };
    updateStick(m_VirtualStickLeft);
    updateStick(m_VirtualStickRight);

    return 1;
}

int32_t Input_Android::HandleKeyEvent(AInputEvent* event) {
    std::lock_guard<std::mutex> lock(m_InputMutex);

    int32_t keyCode = AKeyEvent_getKeyCode(event);
    int32_t action = AKeyEvent_getAction(event);

    if (keyCode >= 0 && keyCode < 512) {
        if (action == AKEY_EVENT_ACTION_DOWN) {
            m_KeyState[keyCode] = true;
        } else if (action == AKEY_EVENT_ACTION_UP) {
            m_KeyState[keyCode] = false;
        }
    }

    // Map gamepad buttons
    int32_t source = AInputEvent_getSource(event);
    if ((source & AINPUT_SOURCE_GAMEPAD) == AINPUT_SOURCE_GAMEPAD) {
        m_GamepadConnected = true;
        int mappedBtn = MapGamepadButtonToEngineButton(keyCode);
        if (mappedBtn >= 0 && mappedBtn < 32) {
            m_GamepadButtons[mappedBtn] = (action == AKEY_EVENT_ACTION_DOWN);
        }
    }

    return 1;
}

int32_t Input_Android::HandleGamepadEvent(AInputEvent* event) {
    std::lock_guard<std::mutex> lock(m_InputMutex);

    m_GamepadConnected = true;

    // Read analog axes
    m_GamepadAxes[0] = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_X, 0);        // Left stick X
    m_GamepadAxes[1] = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Y, 0);        // Left stick Y
    m_GamepadAxes[2] = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Z, 0);        // Right stick X
    m_GamepadAxes[3] = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RZ, 0);       // Right stick Y
    m_GamepadAxes[4] = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_LTRIGGER, 0); // Left trigger
    m_GamepadAxes[5] = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RTRIGGER, 0); // Right trigger
    m_GamepadAxes[6] = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_X, 0);    // D-pad X
    m_GamepadAxes[7] = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_Y, 0);    // D-pad Y

    return 1;
}

// ---------------------------------------------------------------------------
// Per-frame update
// ---------------------------------------------------------------------------
void Input_Android::Update() {
    std::lock_guard<std::mutex> lock(m_InputMutex);

    // Save previous state for edge detection
    memcpy(m_KeyStatePrev, m_KeyState, sizeof(m_KeyState));
    memcpy(m_GamepadButtonsPrev, m_GamepadButtons, sizeof(m_GamepadButtons));

    // Read accelerometer if enabled
    if (m_AccelEnabled && m_SensorQueue) {
        ASensorEvent sensorEvent;
        while (ASensorEventQueue_getEvents(m_SensorQueue, &sensorEvent, 1) > 0) {
            m_AccelX = sensorEvent.acceleration.x;
            m_AccelY = sensorEvent.acceleration.y;
            m_AccelZ = sensorEvent.acceleration.z;
        }
    }
}

// ---------------------------------------------------------------------------
// Touch queries
// ---------------------------------------------------------------------------
int Input_Android::GetTouchCount() const {
    return m_TouchCount;
}

bool Input_Android::GetTouch(int index, float& outX, float& outY, float& outPressure) const {
    if (index < 0 || index >= MAX_TOUCH_POINTS || !m_TouchPoints[index].active)
        return false;
    outX = m_TouchPoints[index].x;
    outY = m_TouchPoints[index].y;
    outPressure = m_TouchPoints[index].pressure;
    return true;
}

bool Input_Android::IsTouching() const {
    return m_TouchCount > 0;
}

// ---------------------------------------------------------------------------
// Keyboard queries
// ---------------------------------------------------------------------------
bool Input_Android::IsKeyDown(int32_t keyCode) const {
    if (keyCode < 0 || keyCode >= 512) return false;
    return m_KeyState[keyCode];
}

bool Input_Android::IsKeyPressed(int32_t keyCode) const {
    if (keyCode < 0 || keyCode >= 512) return false;
    return m_KeyState[keyCode] && !m_KeyStatePrev[keyCode];
}

bool Input_Android::IsKeyReleased(int32_t keyCode) const {
    if (keyCode < 0 || keyCode >= 512) return false;
    return !m_KeyState[keyCode] && m_KeyStatePrev[keyCode];
}

// ---------------------------------------------------------------------------
// Gamepad queries
// ---------------------------------------------------------------------------
bool Input_Android::IsGamepadConnected() const {
    return m_GamepadConnected;
}

float Input_Android::GetGamepadAxis(int axis) const {
    if (axis < 0 || axis >= 8) return 0.0f;
    return m_GamepadAxes[axis];
}

bool Input_Android::IsGamepadButtonDown(int button) const {
    if (button < 0 || button >= 32) return false;
    return m_GamepadButtons[button];
}

bool Input_Android::IsGamepadButtonPressed(int button) const {
    if (button < 0 || button >= 32) return false;
    return m_GamepadButtons[button] && !m_GamepadButtonsPrev[button];
}

// ---------------------------------------------------------------------------
// Virtual controls
// ---------------------------------------------------------------------------
void Input_Android::SetupVirtualControls() {
    // Left stick: bottom-left corner
    m_VirtualStickLeft.centerX = 0.15f;
    m_VirtualStickLeft.centerY = 0.75f;
    m_VirtualStickLeft.radius = 0.12f;
    m_VirtualStickLeft.deadZone = 0.02f;
    m_VirtualStickLeft.axisX = 0;
    m_VirtualStickLeft.axisY = 0;
    m_VirtualStickLeft.activePointerId = -1;

    // Right stick: bottom-right corner
    m_VirtualStickRight.centerX = 0.85f;
    m_VirtualStickRight.centerY = 0.75f;
    m_VirtualStickRight.radius = 0.12f;
    m_VirtualStickRight.deadZone = 0.02f;
    m_VirtualStickRight.axisX = 0;
    m_VirtualStickRight.axisY = 0;
    m_VirtualStickRight.activePointerId = -1;

    // Action buttons (right side)
    VirtualButton btnA = { 0.88f, 0.55f, 0.04f, 0, false }; // A / Fire
    VirtualButton btnB = { 0.95f, 0.48f, 0.04f, 1, false }; // B / Jump
    VirtualButton btnX = { 0.81f, 0.48f, 0.04f, 2, false }; // X / Action
    VirtualButton btnY = { 0.88f, 0.41f, 0.04f, 3, false }; // Y / Special

    m_VirtualButtons.push_back(btnA);
    m_VirtualButtons.push_back(btnB);
    m_VirtualButtons.push_back(btnX);
    m_VirtualButtons.push_back(btnY);
}

bool Input_Android::GetVirtualStickLeft(float& outX, float& outY) const {
    outX = m_VirtualStickLeft.axisX;
    outY = m_VirtualStickLeft.axisY;
    return m_VirtualStickLeft.activePointerId >= 0;
}

bool Input_Android::GetVirtualStickRight(float& outX, float& outY) const {
    outX = m_VirtualStickRight.axisX;
    outY = m_VirtualStickRight.axisY;
    return m_VirtualStickRight.activePointerId >= 0;
}

bool Input_Android::IsVirtualButtonDown(int buttonIndex) const {
    if (buttonIndex < 0 || buttonIndex >= (int)m_VirtualButtons.size())
        return false;
    return m_VirtualButtons[buttonIndex].pressed;
}

// ---------------------------------------------------------------------------
// Accelerometer
// ---------------------------------------------------------------------------
bool Input_Android::EnableAccelerometer(bool enable) {
    if (enable && !m_AccelEnabled) {
        m_SensorManager = ASensorManager_getInstance();
        if (!m_SensorManager) return false;

        m_AccelerometerSensor = ASensorManager_getDefaultSensor(m_SensorManager, ASENSOR_TYPE_ACCELEROMETER);
        if (!m_AccelerometerSensor) return false;

        ALooper* looper = ALooper_forThread();
        if (!looper) looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);

        m_SensorQueue = ASensorManager_createEventQueue(m_SensorManager, looper, 3, nullptr, nullptr);
        ASensorEventQueue_enableSensor(m_SensorQueue, m_AccelerometerSensor);
        ASensorEventQueue_setEventRate(m_SensorQueue, m_AccelerometerSensor, 16667); // ~60Hz

        m_AccelEnabled = true;
        LOGI("Accelerometer enabled");
    } else if (!enable && m_AccelEnabled) {
        if (m_SensorQueue && m_AccelerometerSensor) {
            ASensorEventQueue_disableSensor(m_SensorQueue, m_AccelerometerSensor);
        }
        m_AccelEnabled = false;
        LOGI("Accelerometer disabled");
    }
    return true;
}

void Input_Android::GetAccelerometer(float& outX, float& outY, float& outZ) const {
    outX = m_AccelX;
    outY = m_AccelY;
    outZ = m_AccelZ;
}

// ---------------------------------------------------------------------------
// Key mapping: Android -> Engine
// ---------------------------------------------------------------------------
int Input_Android::MapAndroidKeyToEngineKey(int32_t androidKeyCode) {
    // Map Android AKEYCODE_* to original engine DirectInput keycodes (DIK_*)
    switch (androidKeyCode) {
        case AKEYCODE_W:            return 0x11; // DIK_W
        case AKEYCODE_A:            return 0x1E; // DIK_A
        case AKEYCODE_S:            return 0x1F; // DIK_S
        case AKEYCODE_D:            return 0x20; // DIK_D
        case AKEYCODE_SPACE:        return 0x39; // DIK_SPACE
        case AKEYCODE_ENTER:        return 0x1C; // DIK_RETURN
        case AKEYCODE_ESCAPE:       return 0x01; // DIK_ESCAPE
        case AKEYCODE_BACK:         return 0x01; // Android Back -> ESC
        case AKEYCODE_TAB:          return 0x0F; // DIK_TAB
        case AKEYCODE_SHIFT_LEFT:   return 0x2A; // DIK_LSHIFT
        case AKEYCODE_CTRL_LEFT:    return 0x1D; // DIK_LCONTROL
        case AKEYCODE_DPAD_UP:      return 0xC8; // DIK_UP
        case AKEYCODE_DPAD_DOWN:    return 0xD0; // DIK_DOWN
        case AKEYCODE_DPAD_LEFT:    return 0xCB; // DIK_LEFT
        case AKEYCODE_DPAD_RIGHT:   return 0xCD; // DIK_RIGHT
        case AKEYCODE_1:            return 0x02; // DIK_1
        case AKEYCODE_2:            return 0x03; // DIK_2
        case AKEYCODE_3:            return 0x04; // DIK_3
        case AKEYCODE_4:            return 0x05; // DIK_4
        case AKEYCODE_F1:           return 0x3B; // DIK_F1
        case AKEYCODE_F2:           return 0x3C; // DIK_F2
        case AKEYCODE_F3:           return 0x3D; // DIK_F3
        default:                    return -1;
    }
}

int Input_Android::MapGamepadButtonToEngineButton(int32_t androidButton) {
    // Map Android AKEYCODE_BUTTON_* to engine's gamepad button indices
    switch (androidButton) {
        case AKEYCODE_BUTTON_A:         return 0;
        case AKEYCODE_BUTTON_B:         return 1;
        case AKEYCODE_BUTTON_X:         return 2;
        case AKEYCODE_BUTTON_Y:         return 3;
        case AKEYCODE_BUTTON_L1:        return 4;
        case AKEYCODE_BUTTON_R1:        return 5;
        case AKEYCODE_BUTTON_L2:        return 6;
        case AKEYCODE_BUTTON_R2:        return 7;
        case AKEYCODE_BUTTON_THUMBL:    return 8;
        case AKEYCODE_BUTTON_THUMBR:    return 9;
        case AKEYCODE_BUTTON_START:     return 10;
        case AKEYCODE_BUTTON_SELECT:    return 11;
        case AKEYCODE_DPAD_UP:          return 12;
        case AKEYCODE_DPAD_DOWN:        return 13;
        case AKEYCODE_DPAD_LEFT:        return 14;
        case AKEYCODE_DPAD_RIGHT:       return 15;
        default:                        return -1;
    }
}
