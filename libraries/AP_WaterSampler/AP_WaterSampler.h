#pragma once

#include "AP_WaterSampler_config.h"

#if AP_WATERSAMPLER_ENABLED

#include <AP_Param/AP_Param.h>

#include "AP_WaterPump.h"
#include "AP_WaterValve.h"

/*
  Water sampling driver.

  The rod is driven by a brushed DC motor through a lead screw. The motor
  driver takes an ordinary servo PWM input and has limit switches built in:

    PWM below 1500  run the rod out
    PWM above 1500  run the rod back in
    PWM of 1500     stop

  A whole sample is started from the ground station, normally from a virtual
  button in QGC, with three MAV_CMD_DO_SET_SERVO commands: the bottle and volume
  outputs say what to collect, and a rising edge on the trigger output starts
  the sequence.

    1. select the requested bottle on the valve, so the water has somewhere to
       go before anything else moves, then wait WS_VALVE_DELAY for the solenoid
       to actuate. On the first sample after a reboot the rod is then driven in
       with WS_CLOSE_PWM for WS_ROD_HOME: the rod is already against its stop so
       it cannot move, but the motor driver ignores a lower command until it has
       been driven that way once since power-up
    2. drive the rod out with WS_OPEN_PWM for WS_OPEN_TIME. The rod is held
       stopped for WS_ROD_GUARD either side of that time, so the motor driver's
       own start and stop lag cannot eat into the travel, and the limit switch
       stops the rod if it gets there early
    3. run the pump forwards for the time needed for the requested volume, from
       WS_FLOW_RATE, then stop the pump
    4. wait WS_CLOSE_DELAY
    5. retract the rod with WS_CLOSE_PWM, again with WS_ROD_GUARD either side.
       The valve is deliberately left where it is: the bottles stay sealed, so
       the next sample does not have to wait for another actuation
    6. the sequence is finished and another sample may be started

  Each step is reported with a STATUSTEXT so the ground station can follow the
  sequence, and the ground station can also show progress on its own because
  every duration comes from a parameter it can read.

  The trigger, bottle and volume outputs must have their SERVOx_FUNCTION set to
  0 (Disabled) so that nothing else claims them, and they are left unconnected.

  The motor output is held stopped (1500) from boot onwards, and WS_ENABLE=0
  makes the driver ignore the trigger as well. 1500 is the value that stops the
  rod, so the rod still does not move until the ground station asks for a
  sample, but the motor driver never has to cope with a move command while it
  has yet to see a valid signal on its input.
 */
class AP_WaterSampler
{
public:
    AP_WaterSampler();

    CLASS_NO_COPY(AP_WaterSampler);

    static AP_WaterSampler *get_singleton();

    // Initialise the driver and hold the rod stopped. Called once from the
    // vehicle init(). Does nothing at all when the driver is disabled.
    void init();

    // Run the driver. Called from the vehicle scheduler
    void update();

    // true if the driver has been enabled by the user (WS_ENABLE)
    bool enabled() const { return _enable.get() != 0; }

    // true while a sample is running
    bool running() const { return _state != State::IDLE; }

    // parameter block
    static const struct AP_Param::GroupInfo var_info[];

private:
    // driver state machine
    enum class State : uint8_t {
        IDLE = 0,       // rod at rest, nothing running, another sample may start
        VALVE_SETTLE,   // bottle selected, waiting for the valve to actuate
        ROD_HOME,       // driving the rod briefly in the retract direction
        LOWERING_ROD,   // driving the rod out
        PUMPING,        // rod down, pump drawing water in
        PUMP_SETTLE,    // pump stopped, waiting before retracting the rod
        RAISING_ROD,    // driving the rod back in
    };

    // write a pwm value to the motor channel
    void set_motor_pwm(uint16_t pwm);

    // read the pwm value the ground station last wrote to a channel. returns
    // false if the channel is disabled or out of range
    static bool get_chan_pwm(int8_t chan, uint16_t &pwm);

    // read the bottle and volume the ground station selected and start a
    // sample. A request that cannot be honoured leaves the vehicle completely
    // untouched and is reported so the operator knows why nothing happened
    void start_sequence(uint32_t now_ms);

    // select the bottle and start the pump
    void begin_pumping(uint32_t now_ms);

    // Drive the rod for one movement and report whether the phase, guards
    // included, has elapsed. The movement pwm is only asserted for the middle
    // travel_s window: the rod is held stopped for WS_ROD_GUARD either side of
    // it, so the motor driver's own start and stop lag cannot eat into the
    // travel time the user asked for
    bool rod_phase_done(uint32_t now_ms, uint16_t move_pwm, float travel_s);

    // stop the pump, release the valve and forget any sequence in progress.
    // Used when the driver is switched off part way through a sample
    void stop_sequence();

    // parameters
    AP_Int8  _enable;        // WS_ENABLE
    AP_Int8  _motor_chan;    // WS_MOTOR_CHAN
    AP_Int8  _trigger_chan;  // WS_TRIG_CHAN
    AP_Int16 _open_pwm;      // WS_OPEN_PWM
    AP_Int16 _close_pwm;     // WS_CLOSE_PWM
    AP_Float _open_time;     // WS_OPEN_TIME
    AP_Int8  _bottle_chan;   // WS_BOTTLE_CHAN
    AP_Int8  _volume_chan;   // WS_VOLUME_CHAN
    AP_Int16 _max_volume;    // WS_MAX_VOLUME
    AP_Float _flow_rate;     // WS_FLOW_RATE
    AP_Float _close_delay;   // WS_CLOSE_DELAY
    AP_Float _close_time;    // WS_CLOSE_TIME
    AP_Float _guard;         // WS_ROD_GUARD
    AP_Float _home_time;     // WS_ROD_HOME

    // water pump and bottle selection valve. They share this library with the
    // rod but are independent of it: the rod's WS_ENABLE does not gate them
    AP_WaterPump pump;
    AP_WaterValve valve;

    // state machine bookkeeping
    State _state;
    uint32_t _state_start_ms;
    bool _trigger_was_pressed;
    bool _primed;             // false until update() has recorded the first trigger state
    bool _homed;              // true once the rod has been driven in once since boot
    uint8_t _bottle;          // bottle selected for the sample in progress
    uint32_t _pump_time_ms;   // how long to pump for, from the requested volume

    static AP_WaterSampler *_singleton;
};

#endif  // AP_WATERSAMPLER_ENABLED
