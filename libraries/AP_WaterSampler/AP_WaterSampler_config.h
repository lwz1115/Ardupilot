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
// the SIYI N7. Boards without an IOMCU number their outputs from SERVO1, so
// they override this in their own defaults.parm.
#ifndef WS_DEFAULT_MOTOR_CHAN
#define WS_DEFAULT_MOTOR_CHAN 9
#endif

// Servo outputs the ground station writes to with MAV_CMD_DO_SET_SERVO in order
// to request a sample: the trigger starts the sequence, and the bottle and
// volume outputs say what to collect. Their SERVOx_FUNCTION must be 0
// (Disabled) or the command is rejected, and nothing is wired to them.
//
// The values used here are deliberately on outputs that no board has: the
// trigger output must not double as a GPIO, because a channel marked -1 (GPIO)
// is refused by MAV_CMD_DO_SET_SERVO, and the real outputs are busy with the
// rod, the valve and the pump.
#ifndef WS_DEFAULT_TRIGGER_CHAN
#define WS_DEFAULT_TRIGGER_CHAN 14
#endif
#ifndef WS_DEFAULT_BOTTLE_CHAN
#define WS_DEFAULT_BOTTLE_CHAN 15
#endif
#ifndef WS_DEFAULT_VOLUME_CHAN
#define WS_DEFAULT_VOLUME_CHAN 16
#endif

// A value above this counts as the trigger being pressed, and on the bottle
// output it selects bottle 2
#ifndef WS_TRIGGER_PRESS_PWM
#define WS_TRIGGER_PRESS_PWM 1500
#endif

// Default timings of a sample
#ifndef WS_DEFAULT_OPEN_TIME_S
#define WS_DEFAULT_OPEN_TIME_S 8.0f
#endif
#ifndef WS_DEFAULT_CLOSE_DELAY_S
#define WS_DEFAULT_CLOSE_DELAY_S 3.0f
#endif
#ifndef WS_DEFAULT_CLOSE_TIME_S
#define WS_DEFAULT_CLOSE_TIME_S 8.0f
#endif

// Time the rod is held stopped either side of each movement. The motor driver
// needs a moment to start and to stop, so this guard is what makes the travel
// time the user asks for really available for the rod to reach its stop
#ifndef WS_DEFAULT_ROD_GUARD_S
#define WS_DEFAULT_ROD_GUARD_S 1.0f
#endif

// Time the rod is driven in the retract direction before the first lower of a
// boot. The rod is already against its retract stop, so this cannot move it,
// but a driver that has not been driven that way since power-up will ignore a
// lower command without it
#ifndef WS_DEFAULT_ROD_HOME_S
#define WS_DEFAULT_ROD_HOME_S 1.0f
#endif

// Pumping rate used to turn a requested volume into a pumping time. 10 ml/s
// means 500 ml takes 50 seconds
#ifndef WS_DEFAULT_FLOW_RATE_ML_S
#define WS_DEFAULT_FLOW_RATE_ML_S 10.0f
#endif

// Largest volume the hull can hold. A request above this is refused so the
// bottles cannot be overfilled
#ifndef WS_DEFAULT_MAX_VOLUME_ML
#define WS_DEFAULT_MAX_VOLUME_ML 2000
#endif

// Default GPIO numbers for the water pump direction lines, and for the sample
// bottle selection relay. These are the GPIO numbers that the board definition
// gives to those outputs; GPIO 51, 52 and 53 are the second, third and fourth
// outputs on both boards used here, even though the SERVO numbers they answer
// to are different.
// The matching SERVOn_FUNCTION must be -1 (GPIO) for these pins to be usable:
// setting it to 0 leaves the pin owned by the PWM timer and every GPIO write is
// then silently ignored.
#ifndef WS_PUMP_FWD_PIN_DEFAULT
#define WS_PUMP_FWD_PIN_DEFAULT 52
#endif
#ifndef WS_PUMP_REV_PIN_DEFAULT
#define WS_PUMP_REV_PIN_DEFAULT 53
#endif

// GPIO that drives the sample bottle selection relay (SERVO10 / AUX2 on the
// SIYI N7, SERVO2 / PWM2 on the SkyDroid-S3)
#ifndef WS_VALVE_PIN_DEFAULT
#define WS_VALVE_PIN_DEFAULT 51
#endif

// Time the bottle selection relay is given to actuate the solenoid valve. The
// sequence waits this long after choosing a bottle - whether or not the coil
// ends up energised - before the rod starts moving
#ifndef WS_DEFAULT_VALVE_DELAY_S
#define WS_DEFAULT_VALVE_DELAY_S 5.0f
#endif

// Command channel the ground station writes to in order to select a pump
// direction. Nothing is wired to this output - it only carries the request.
#ifndef WS_PUMP_CMD_CHAN_DEFAULT
#define WS_PUMP_CMD_CHAN_DEFAULT 13
#endif

// Values on the pump command channel. The pump controller triggers on a LOW
// level, so the driver drives the selected direction line low and the other
// high. 1900 or more runs forwards, 1100 runs backwards, and 1500 stops.
// Anything below 1000 also stops, which is what the channel reads before the
// ground station has written to it.
#ifndef WS_PUMP_CMD_FWD_PWM
#define WS_PUMP_CMD_FWD_PWM 1800
#endif
#ifndef WS_PUMP_CMD_REV_PWM
#define WS_PUMP_CMD_REV_PWM 1200
#endif
#ifndef WS_PUMP_CMD_REV_MIN_PWM
#define WS_PUMP_CMD_REV_MIN_PWM 1000
#endif
