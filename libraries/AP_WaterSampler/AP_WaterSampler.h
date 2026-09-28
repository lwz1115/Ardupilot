#pragma once

#include "AP_WaterSampler_config.h"

#if AP_WATERSAMPLER_ENABLED

#include <AP_Param/AP_Param.h>

/*
  Water sampling rod driver.

  The rod is driven by a brushed DC motor through a lead screw. The motor
  driver takes an ordinary servo PWM input and has limit switches built in:

    PWM below 1500  run the rod out
    PWM above 1500  run the rod back in
    PWM of 1500     stop

  A sample is started from the ground station, normally from a virtual button
  in QGC, which sends MAV_CMD_DO_SET_SERVO aimed at the trigger channel.
  Pressing the button:

    1. drives the rod out by writing WS_OPEN_PWM to the motor channel
    2. waits WS_OPEN_TIME. The rod reaches its limit switch part way through
       this time, the remainder simply holds it there
    3. drives the rod back in by writing WS_CLOSE_PWM, which is also the value
       held while idle

  Both the trigger channel and the motor channel must have their
  SERVOx_FUNCTION set to 0 (Disabled) so that nothing else claims them.

  The motor output is left completely untouched until a sample is requested:
  the rod is deliberately not moved at boot. Writing to an AUX output that
  early is not safe, and the rod only needs to move when the ground station
  asks. WS_ENABLE=0 additionally makes the driver ignore the trigger.
 */
class AP_WaterSampler
{
public:
    AP_WaterSampler();

    CLASS_NO_COPY(AP_WaterSampler);

    static AP_WaterSampler *get_singleton();

    // Initialise the driver and hold the rod closed. Called once from the
    // vehicle init(). Does nothing at all when the driver is disabled.
    void init();

    // Run the driver. Called from the vehicle scheduler
    void update();

    // true if the driver has been enabled by the user (WS_ENABLE)
    bool enabled() const { return _enable.get() != 0; }

    // true while the rod is being driven out
    bool running() const { return _state == State::OPENING; }

    // parameter block
    static const struct AP_Param::GroupInfo var_info[];

private:
    // driver state machine
    enum class State : uint8_t {
        IDLE = 0,   // rod at rest, motor held closed
        OPENING,    // driving the rod out
    };

    // write a pwm value to the motor channel
    void set_motor_pwm(uint16_t pwm);

    // read the pwm value the ground station last wrote to the trigger
    // channel. returns false if the channel is disabled or out of range
    bool get_trigger_pwm(uint16_t &pwm) const;

    // parameters
    AP_Int8  _enable;        // WS_ENABLE
    AP_Int8  _motor_chan;    // WS_MOTOR_CHAN
    AP_Int8  _trigger_chan;  // WS_TRIG_CHAN
    AP_Int16 _open_pwm;      // WS_OPEN_PWM
    AP_Int16 _close_pwm;     // WS_CLOSE_PWM
    AP_Float _open_time;     // WS_OPEN_TIME

    // state machine bookkeeping
    State _state;
    uint32_t _state_start_ms;
    bool _trigger_was_pressed;
    bool _primed;             // false until update() has recorded the first trigger state

    static AP_WaterSampler *_singleton;
};

#endif  // AP_WATERSAMPLER_ENABLED
