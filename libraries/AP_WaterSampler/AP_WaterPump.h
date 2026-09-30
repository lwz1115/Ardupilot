#pragma once

#include "AP_WaterSampler_config.h"

#if AP_WATERSAMPLER_ENABLED

#include <AP_Param/AP_Param.h>

/*
  Water pump driver.

  The 24V pump has its own controller board with two direction inputs, one per
  rotation direction, both triggered by a LOW level:

    line A low  -> pump runs forwards (draws water in)
    line B low  -> pump runs backwards (pushes water out)
    both high   -> pump stopped

  The two inputs are driven as plain GPIO levels, not as servo PWM, so the pin
  numbers are the GPIO numbers that the board definition gives to the AUX
  outputs. For the SIYI N7 and the SkyDroid-S3:

    PE11 TIM1_CH2 TIM1 PWM(3) GPIO(52)    52 = AUXOUT3
    PE9  TIM1_CH1 TIM1 PWM(4) GPIO(53)    53 = AUXOUT4

  The matching SERVOn_FUNCTION must be 0 (Disabled).

  init() drives both lines to the idle (HIGH) state immediately, so the pump is
  already stopped by the time the vehicle finishes booting and only a ground
  station command can start it.

  The ground station selects a direction with a single value on one command
  channel, written with MAV_CMD_DO_SET_SERVO:

    value 1800 or more   -> run forwards  (draw water in)
    value around 1100    -> run backwards (push water out)
    anything else, eg 1500, or below 1000 (the value before anything is written)
                         -> stop

  The direction latches, so the pump keeps running until another command
  arrives. That is what the three ground station buttons send: forward, reverse
  and stop.

  Forward and reverse cannot be active at the same time: the whole driver keeps
  a single direction state and apply_mode() drives one line low and the other
  high from that state.

  The sampling sequence in AP_WaterSampler can take control of the pump while a
  sample is running. While it holds control the command channel is ignored, so
  a stale value written there cannot restart the pump part way through a sample.
 */
class AP_WaterPump
{
public:
    AP_WaterPump();

    CLASS_NO_COPY(AP_WaterPump);

    // parameter block
    static const struct AP_Param::GroupInfo var_info[];

    // Initialise the driver. Drives both control lines to the idle state so the
    // pump cannot be running once the vehicle has finished booting
    void init();

    // called from the vehicle scheduler
    void update();

    // true if the driver has been enabled by the user (WS_PUMP_ENABLE)
    bool enabled() const { return _enable.get() != 0; }

    // The sampling sequence can take control of the pump. While it holds
    // control the command channel is ignored, so a stale value written by the
    // ground station cannot restart the pump part way through a sample
    enum class SequenceState : uint8_t {
        NONE = 0,   // manual: the command channel decides the direction
        FORWARD,    // the sequence is drawing water in
        STOP,       // the sequence has finished with the pump
    };

    // hand control to the sampling sequence, or give it back with NONE
    void set_sequence_state(SequenceState state) { _sequence_state = state; }

    // true while the sampling sequence, rather than the command channel, owns
    // the pump
    bool sequence_has_control() const { return _sequence_state != SequenceState::NONE; }

private:
    // the direction the pump is running in
    enum class Mode : uint8_t {
        STOP = 0,   // both lines high
        FORWARD,    // forward line low, draws water in
        REVERSE,    // reverse line low, pushes water out
    };

    // drive one control line. active means "request this direction", which for
    // this hardware is a LOW level
    static void set_line(int16_t pin, bool active);

    // drive both lines to match the requested mode
    void apply_mode(Mode mode);

    // turn the value on the command channel into a direction
    static Mode mode_from_command(uint16_t pwm);

    // read the value the ground station last wrote to the command channel
    static bool get_chan_pwm(int8_t chan, uint16_t &pwm);

    // parameters
    AP_Int8  _enable;     // WS_PUMP_ENABLE
    AP_Int16 _fwd_pin;    // WS_PUMP_FWD_PIN
    AP_Int16 _rev_pin;    // WS_PUMP_REV_PIN
    AP_Int8  _cmd_chan;   // WS_PUMP_CMD_CH

    // current direction. A single value, so the two lines can never be low at
    // the same time
    Mode _mode;

    // set while the sampling sequence owns the pump
    SequenceState _sequence_state;
};

#endif  // AP_WATERSAMPLER_ENABLED
