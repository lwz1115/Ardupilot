#pragma once

#include <AP_HAL/AP_HAL_Boards.h>

// Water sampling rod support.
//
// The driver is built in by default so that the feature is available without a
// custom build. Define AP_WATERSAMPLER_ENABLED to 0 to remove it entirely.
#ifndef AP_WATERSAMPLER_ENABLED
#define AP_WATERSAMPLER_ENABLED 1
#endif

// Default servo output that drives the sampling rod motor. SERVO9 is AUX1 on
// both the SIYI N7 and the SkyDroid-S3.
#ifndef WS_DEFAULT_MOTOR_CHAN
#define WS_DEFAULT_MOTOR_CHAN 9
#endif

// Default servo output used by the ground station to request a sample. Its
// SERVOx_FUNCTION must be 0 (Disabled), otherwise MAV_CMD_DO_SET_SERVO is
// rejected on it. The output is never wired to anything.
#ifndef WS_DEFAULT_TRIGGER_CHAN
#define WS_DEFAULT_TRIGGER_CHAN 10
#endif

// A value above this counts as the button being pressed.
#ifndef WS_TRIGGER_PRESS_PWM
#define WS_TRIGGER_PRESS_PWM 1500
#endif
