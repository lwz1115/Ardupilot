/*
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "AP_WaterSampler.h"

#if AP_WATERSAMPLER_ENABLED

#include <AP_HAL/AP_HAL.h>
#include <AP_Math/AP_Math.h>
#include <GCS_MAVLink/GCS.h>
#include <SRV_Channel/SRV_Channel.h>

extern const AP_HAL::HAL& hal;

// lowest and highest pwm we will write to the motor channel
static const uint16_t WS_PWM_MIN = 800;
static const uint16_t WS_PWM_MAX = 2200;

// pwm that holds the rod still. The motor driver takes an ordinary servo
// signal: anything below 1500 runs the rod out, anything above 1500 runs it
// back in, and 1500 stops it
static const uint16_t WS_ROD_STOP_PWM = 1500;

const AP_Param::GroupInfo AP_WaterSampler::var_info[] = {

    // @Param: ENABLE
    // @DisplayName: Water sampler enable
    // @Description: Enable the water sampling rod driver. When disabled nothing is written to the motor or trigger output, and no sample can be started.
    // @Values: 0:Disabled,1:Enabled
    // @User: Standard
    // @RebootRequired: True
    AP_GROUPINFO_FLAGS("ENABLE", 1, AP_WaterSampler, _enable, 1, AP_PARAM_FLAG_ENABLE),

    // @Param: MOTOR_CHAN
    // @DisplayName: Water sampler motor servo channel
    // @Description: Servo output channel that drives the sampling rod motor. 9 is AUX1 (SERVO9) on boards that have an IOMCU, such as the SIYI N7. Boards without one number their outputs from SERVO1, so they set this in their board defaults. The matching SERVOx_FUNCTION must be set to 0 (Disabled) so that no other subsystem drives the output.
    // @Range: 1 16
    // @Increment: 1
    // @User: Standard
    // @RebootRequired: True
    AP_GROUPINFO("MOTOR_CHAN", 2, AP_WaterSampler, _motor_chan, WS_DEFAULT_MOTOR_CHAN),

    // @Param: TRIG_CHAN
    // @DisplayName: Water sampler trigger servo channel
    // @Description: Servo output channel the ground station uses to start a sample. A value above 1500 starts the whole sequence: lower the rod, select the bottle, pump the requested volume, then retract the rod. The channel must sit at 1500 or below between samples, otherwise there is no rising edge and the next request is ignored. The matching SERVOx_FUNCTION must be set to 0 (Disabled), as the command is rejected on a channel that is already in use. Leave this output unconnected, and do not use it for a GPIO.
    // @Range: 1 16
    // @Increment: 1
    // @User: Standard
    // @RebootRequired: True
    AP_GROUPINFO("TRIG_CHAN", 3, AP_WaterSampler, _trigger_chan, WS_DEFAULT_TRIGGER_CHAN),

    // @Param: OPEN_PWM
    // @DisplayName: Water sampler open pwm
    // @Description: PWM written to the motor channel to drive the rod out. Must be below 1500. The motor driver stops the rod at its limit switch.
    // @Range: 800 1500
    // @Increment: 1
    // @User: Standard
    AP_GROUPINFO("OPEN_PWM", 4, AP_WaterSampler, _open_pwm, 1100),

    // @Param: CLOSE_PWM
    // @DisplayName: Water sampler close pwm
    // @Description: PWM written to the motor channel to drive the rod back in. Must be above 1500. The motor driver stops the rod at its limit switch.
    // @Range: 1500 2200
    // @Increment: 1
    // @User: Standard
    AP_GROUPINFO("CLOSE_PWM", 5, AP_WaterSampler, _close_pwm, 1900),

    // @Param: OPEN_TIME
    // @DisplayName: Water sampler rod lower time
    // @Description: Time the rod is driven out for during a sample, not counting the WS_ROD_GUARD held either side of it. The rod normally reaches its limit switch part way through this time.
    // @Units: s
    // @Range: 0.5 60
    // @Increment: 0.5
    // @User: Standard
    AP_GROUPINFO("OPEN_TIME", 6, AP_WaterSampler, _open_time, WS_DEFAULT_OPEN_TIME_S),

    // @Param: BOTTLE_CHAN
    // @DisplayName: Water sampler bottle select channel
    // @Description: Servo output channel the ground station uses to choose which bottle to fill. A value above 1500 selects bottle 2, which energises the valve relay. Any other value selects bottle 1, the released default. The value must be written before the trigger, as it is only read when a sample starts. The matching SERVOx_FUNCTION must be set to 0 (Disabled). Leave this output unconnected.
    // @Range: 1 16
    // @Increment: 1
    // @User: Standard
    // @RebootRequired: True
    AP_GROUPINFO("BOTTLE_CHAN", 8, AP_WaterSampler, _bottle_chan, WS_DEFAULT_BOTTLE_CHAN),

    // @Param: VOLUME_CHAN
    // @DisplayName: Water sampler volume select channel
    // @Description: Servo output channel the ground station writes the requested sample volume to, in millilitres, for example 500 or 1500. The value must be written before the trigger, as it is only read when a sample starts. The matching SERVOx_FUNCTION must be set to 0 (Disabled). Leave this output unconnected.
    // @Range: 1 16
    // @Increment: 1
    // @User: Standard
    // @RebootRequired: True
    AP_GROUPINFO("VOLUME_CHAN", 9, AP_WaterSampler, _volume_chan, WS_DEFAULT_VOLUME_CHAN),

    // @Param: MAX_VOLUME
    // @DisplayName: Water sampler maximum volume
    // @Description: Largest volume this hull can collect, in millilitres. A request above this is refused so the bottles cannot be overfilled. Set this for the bottles actually carried. The ground station reads this to decide which volumes it offers.
    // @Range: 1 2000
    // @Increment: 1
    // @User: Standard
    AP_GROUPINFO("MAX_VOLUME", 10, AP_WaterSampler, _max_volume, WS_DEFAULT_MAX_VOLUME_ML),

    // @Param: FLOW_RATE
    // @DisplayName: Water sampler pump flow rate
    // @Description: Volume the pump draws per second, in millilitres per second, used to turn a requested volume into a pumping time. 10 means 500 millilitres takes 50 seconds. Measure this on the vehicle, as it depends on the tubing and the head height.
    // @Range: 0.1 100
    // @Increment: 0.1
    // @User: Standard
    AP_GROUPINFO("FLOW_RATE", 11, AP_WaterSampler, _flow_rate, WS_DEFAULT_FLOW_RATE_ML_S),

    // @Param: CLOSE_DELAY
    // @DisplayName: Water sampler rod retract delay
    // @Description: Time to wait after the pump stops before retracting the rod, so the water in the tubing has time to settle.
    // @Units: s
    // @Range: 0 60
    // @Increment: 0.5
    // @User: Standard
    AP_GROUPINFO("CLOSE_DELAY", 12, AP_WaterSampler, _close_delay, WS_DEFAULT_CLOSE_DELAY_S),

    // @Param: CLOSE_TIME
    // @DisplayName: Water sampler rod retract time
    // @Description: Time the rod is driven back in for, not counting the WS_ROD_GUARD held either side of it. The sequence is finished, and another sample can be started, once this time and the trailing guard have elapsed.
    // @Units: s
    // @Range: 0.5 60
    // @Increment: 0.5
    // @User: Standard
    AP_GROUPINFO("CLOSE_TIME", 13, AP_WaterSampler, _close_time, WS_DEFAULT_CLOSE_TIME_S),

    // @Param: ROD_GUARD
    // @DisplayName: Water sampler rod guard time
    // @Description: Time the rod is held stopped either side of each movement, before and after WS_OPEN_TIME and WS_CLOSE_TIME. The motor driver needs a moment to start and to stop, so this guard is what makes the travel time really available for the rod to reach its limit switch.
    // @Units: s
    // @Range: 0 10
    // @Increment: 0.5
    // @User: Standard
    AP_GROUPINFO("ROD_GUARD", 15, AP_WaterSampler, _guard, WS_DEFAULT_ROD_GUARD_S),

    // @Param: ROD_HOME
    // @DisplayName: Water sampler rod home time
    // @Description: Time the sampling rod is driven in the retract direction before the first lower of each boot, so the motor driver sees a retract command before it is asked to lower. The rod is already against its retract stop at that point, so it does not move. Only the first sample after a reboot does this.
    // @Units: s
    // @Range: 0 10
    // @Increment: 0.1
    // @User: Standard
    AP_GROUPINFO("ROD_HOME", 16, AP_WaterSampler, _home_time, WS_DEFAULT_ROD_HOME_S),

    // @Group: PUMP_
    // @Path: AP_WaterPump.cpp
    AP_SUBGROUPINFO(pump, "PUMP_", 7, AP_WaterSampler, AP_WaterPump),

    // @Group: VALVE_
    // @Path: AP_WaterValve.cpp
    AP_SUBGROUPINFO(valve, "VALVE_", 14, AP_WaterSampler, AP_WaterValve),

    AP_GROUPEND
};

AP_WaterSampler *AP_WaterSampler::_singleton = nullptr;

AP_WaterSampler::AP_WaterSampler()
{
    AP_Param::setup_object_defaults(this, var_info);

    // bring the driver state to a known value. init() repeats this, but the
    // driver must be safe to query before init() has been called
    _state = State::IDLE;
    _state_start_ms = 0;
    _trigger_was_pressed = false;
    _primed = false;
    _bottle = 1;
    _pump_time_ms = 0;

    if (_singleton != nullptr) {
        AP_HAL::panic("AP_WaterSampler must be singleton");
    }
    _singleton = this;
}

AP_WaterSampler *AP_WaterSampler::get_singleton()
{
    return _singleton;
}

// Initialise the driver and hold the rod still. The motor channel is driven
// with WS_ROD_STOP_PWM from boot: that is the "stop" value, so the rod does not
// move, but the motor driver keeps seeing a valid pulse train. Drivers that are
// sent a move command before they have ever seen a signal can ignore it, which
// is what stopped the first lowering of the first sample
void AP_WaterSampler::init()
{
    // the pump and the valve are independent of the rod, so they are not gated
    // by WS_ENABLE
    pump.init();
    valve.init();

    _state = State::IDLE;
    _state_start_ms = AP_HAL::millis();
    _trigger_was_pressed = false;
    _primed = false;
    _homed = false;
    _bottle = 1;
    _pump_time_ms = 0;

    if (enabled()) {
        set_motor_pwm(WS_ROD_STOP_PWM);
    }
}

void AP_WaterSampler::update()
{
    const uint32_t now_ms = AP_HAL::millis();

    if (!enabled()) {
        // the rod driver has been switched off. Stop anything a sequence
        // started, but leave the rod output alone: a disabled driver must not
        // write to it. The pump has its own WS_PUMP_ENABLE, so it is still
        // stepped and keeps working on its own
        if (_state != State::IDLE || pump.sequence_has_control()) {
            stop_sequence();
        }
        pump.update();
        return;
    }

    uint16_t trig_pwm = 0;
    const bool trigger_pressed = get_chan_pwm(_trigger_chan.get(), trig_pwm) && (trig_pwm > WS_TRIGGER_PRESS_PWM);

    if (!_primed) {
        // First call after boot. Only record the state of the trigger so that
        // a channel which is already high (stuck button, or an output that has
        // not been reset) does not start a sample by itself. No output is
        // written here either.
        _trigger_was_pressed = trigger_pressed;
        _primed = true;
        pump.update();
        return;
    }

    // Only the transition into "pressed" starts a sample, so holding the
    // button down does not repeat it, and a request that arrives while a
    // sample is running is ignored
    const bool trigger_edge = trigger_pressed && !_trigger_was_pressed;
    _trigger_was_pressed = trigger_pressed;

    switch (_state) {
    case State::IDLE:
        if (trigger_edge) {
            start_sequence(now_ms);
        }
        break;

    case State::VALVE_SETTLE:
        // hold the rod stopped for the whole wait: the motor driver sees one
        // continuous neutral signal from the moment the sample starts, so it is
        // ready to act on the move command that follows
        set_motor_pwm(WS_ROD_STOP_PWM);
        if (now_ms - _state_start_ms >= (uint32_t)(valve.delay_s() * 1000.0f)) {
            _state_start_ms = now_ms;
            if (_homed) {
                _state = State::LOWERING_ROD;
                GCS_SEND_TEXT(MAV_SEVERITY_INFO, "WS: rod lowering");
            } else {
                // first sample since boot, so drive the rod in first
                _state = State::ROD_HOME;
            }
        }
        break;

    case State::ROD_HOME:
        // drive the rod briefly in the retract direction. It is already against
        // its retract stop, so this cannot move it, but a motor driver that has
        // not been driven that way since power-up will ignore a lower command
        set_motor_pwm(_close_pwm.get());
        if (now_ms - _state_start_ms >= (uint32_t)(_home_time.get() * 1000.0f)) {
            _homed = true;
            _state = State::LOWERING_ROD;
            _state_start_ms = now_ms;
            GCS_SEND_TEXT(MAV_SEVERITY_INFO, "WS: rod lowering");
        }
        break;

    case State::LOWERING_ROD:
        if (rod_phase_done(now_ms, _open_pwm.get(), _open_time.get())) {
            begin_pumping(now_ms);
        }
        break;

    case State::PUMPING:
        if (now_ms - _state_start_ms >= _pump_time_ms) {
            // the requested volume has been drawn, so stop the pump. The rod
            // stays down for now
            pump.set_sequence_state(AP_WaterPump::SequenceState::STOP);
            _state = State::PUMP_SETTLE;
            _state_start_ms = now_ms;
            GCS_SEND_TEXT(MAV_SEVERITY_INFO, "WS: volume reached");
        }
        break;

    case State::PUMP_SETTLE:
        if (now_ms - _state_start_ms >= (uint32_t)(_close_delay.get() * 1000.0f)) {
            // retract the rod. The valve is deliberately left where it is: the
            // bottles stay sealed, so keeping the coil energised costs nothing
            // and saves the next sample a valve actuation
            _state = State::RAISING_ROD;
            _state_start_ms = now_ms;
            set_motor_pwm(WS_ROD_STOP_PWM);
            GCS_SEND_TEXT(MAV_SEVERITY_INFO, "WS: rod retracting");
        }
        break;

    case State::RAISING_ROD:
        if (rod_phase_done(now_ms, _close_pwm.get(), _close_time.get())) {
            // hand the pump back to the ground station's own controls
            pump.set_sequence_state(AP_WaterPump::SequenceState::NONE);
            _state = State::IDLE;
            _state_start_ms = now_ms;
            GCS_SEND_TEXT(MAV_SEVERITY_INFO, "WS: sample complete");
        }
        break;
    }

    // run the pump last so that a state the sequence has just asked for takes
    // effect on this iteration rather than the next one
    pump.update();
}

// Read the bottle and the volume the ground station selected and start a
// sample. A request that cannot be honoured leaves the vehicle completely
// untouched and is reported, so the operator can see why nothing happened
void AP_WaterSampler::start_sequence(uint32_t now_ms)
{
    uint8_t bottle = 1;
    uint16_t bottle_pwm = 0;
    if (get_chan_pwm(_bottle_chan.get(), bottle_pwm) && (bottle_pwm > WS_TRIGGER_PRESS_PWM)) {
        bottle = 2;
    }

    uint16_t volume_ml = 0;
    if (!get_chan_pwm(_volume_chan.get(), volume_ml)) {
        GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "WS: volume channel %d is not available", (int)_volume_chan.get());
        return;
    }
    if (volume_ml == 0) {
        GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "WS: no volume selected on channel %d", (int)_volume_chan.get());
        return;
    }

    const int16_t max_volume = _max_volume.get();
    if (max_volume <= 0 || volume_ml > (uint16_t)max_volume) {
        GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "WS: %uml exceeds the %dml limit", (unsigned)volume_ml, (int)max_volume);
        return;
    }

    const float flow_rate = _flow_rate.get();
    if (!is_positive(flow_rate)) {
        GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "WS: WS_FLOW_RATE must be positive");
        return;
    }

    _bottle = bottle;
    _pump_time_ms = (uint32_t)((float)volume_ml / flow_rate * 1000.0f);

    // select the bottle first, so the water has somewhere to go before the rod
    // goes into the water. The rod is not touched yet: the valve is given
    // WS_VALVE_DELAY to actuate before anything else moves
    valve.set_bottle(_bottle);
    _state = State::VALVE_SETTLE;
    _state_start_ms = now_ms;

    GCS_SEND_TEXT(MAV_SEVERITY_INFO, "WS: bottle %u %uml selected", (unsigned)bottle, (unsigned)volume_ml);
}

// the rod is down, so start drawing water
void AP_WaterSampler::begin_pumping(uint32_t now_ms)
{
    pump.set_sequence_state(AP_WaterPump::SequenceState::FORWARD);
    _state = State::PUMPING;
    _state_start_ms = now_ms;
    GCS_SEND_TEXT(MAV_SEVERITY_INFO, "WS: pumping %us", (unsigned)(_pump_time_ms / 1000));
}

// drive the rod for one movement, holding it stopped for WS_ROD_GUARD either
// side of the travel time. Returns true once the whole phase has elapsed, at
// which point the rod has already been left stopped
bool AP_WaterSampler::rod_phase_done(uint32_t now_ms, uint16_t move_pwm, float travel_s)
{
    const uint32_t guard_ms = (uint32_t)(_guard.get() * 1000.0f);
    const uint32_t travel_ms = (uint32_t)(travel_s * 1000.0f);
    const uint32_t elapsed_ms = now_ms - _state_start_ms;

    if (elapsed_ms < guard_ms || elapsed_ms >= guard_ms + travel_ms) {
        set_motor_pwm(WS_ROD_STOP_PWM);
    } else {
        set_motor_pwm(move_pwm);
    }

    return elapsed_ms >= guard_ms + travel_ms + guard_ms;
}

// stop the pump and release the valve, and forget any sequence in progress.
// The rod is deliberately left where it is: this runs when the driver has been
// switched off, and a disabled driver must not write to the rod output
void AP_WaterSampler::stop_sequence()
{
    pump.set_sequence_state(AP_WaterPump::SequenceState::NONE);
    valve.release();
    _state = State::IDLE;
    _state_start_ms = AP_HAL::millis();
    _bottle = 1;
    _pump_time_ms = 0;
}

// write a pwm value to the motor channel. WS_MOTOR_CHAN is a 1 based SERVOn
// number, while SRV_Channels uses a 0 based index
void AP_WaterSampler::set_motor_pwm(uint16_t pwm)
{
    const int8_t chan = _motor_chan.get();
    if (chan < 1) {
        return;
    }
    SRV_Channels::set_output_pwm_chan(chan - 1, constrain_int16(pwm, WS_PWM_MIN, WS_PWM_MAX));
}

// read back the pwm the ground station last wrote to a channel
bool AP_WaterSampler::get_chan_pwm(int8_t chan, uint16_t &pwm)
{
    if (chan < 1) {
        return false;
    }
    return SRV_Channels::get_output_pwm_chan(chan - 1, pwm);
}

#endif  // AP_WATERSAMPLER_ENABLED
