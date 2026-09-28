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
#include <SRV_Channel/SRV_Channel.h>

extern const AP_HAL::HAL& hal;

// lowest and highest pwm we will write to the motor channel
static const uint16_t WS_PWM_MIN = 800;
static const uint16_t WS_PWM_MAX = 2200;

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
    // @Description: Servo output channel that drives the sampling rod motor. 9 is AUX1 (SERVO9) on both the SIYI N7 and the SkyDroid-S3. The matching SERVOx_FUNCTION must be set to 0 (Disabled) so that no other subsystem drives the output.
    // @Range: 1 16
    // @Increment: 1
    // @User: Standard
    // @RebootRequired: True
    AP_GROUPINFO("MOTOR_CHAN", 2, AP_WaterSampler, _motor_chan, WS_DEFAULT_MOTOR_CHAN),

    // @Param: TRIG_CHAN
    // @DisplayName: Water sampler trigger servo channel
    // @Description: Servo output channel used by the ground station to request a sample. The ground station starts a sample by sending MAV_CMD_DO_SET_SERVO to this channel with a value above 1500. The matching SERVOx_FUNCTION must be set to 0 (Disabled), as the command is rejected on a channel that is already in use. Leave this output unconnected.
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
    // @Description: PWM written to the motor channel to drive the rod back in, and the value held while idle. Must be above 1500. The motor driver stops the rod at its limit switch.
    // @Range: 1500 2200
    // @Increment: 1
    // @User: Standard
    AP_GROUPINFO("CLOSE_PWM", 5, AP_WaterSampler, _close_pwm, 1900),

    // @Param: OPEN_TIME
    // @DisplayName: Water sampler open time
    // @Description: Time that the rod is driven out for. The rod normally reaches its limit switch part way through this time, and the remainder simply holds it there.
    // @Units: s
    // @Range: 0.5 60
    // @Increment: 0.5
    // @User: Standard
    AP_GROUPINFO("OPEN_TIME", 6, AP_WaterSampler, _open_time, 10.0f),

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

    if (_singleton != nullptr) {
        AP_HAL::panic("AP_WaterSampler must be singleton");
    }
    _singleton = this;
}

AP_WaterSampler *AP_WaterSampler::get_singleton()
{
    return _singleton;
}

// initialise the driver. This deliberately does not touch any servo output:
// writing to an AUX output this early in the boot is not safe, and the rod
// does not need to move until the ground station asks for a sample. The motor
// output therefore stays completely untouched until a sample is requested
void AP_WaterSampler::init()
{
    _state = State::IDLE;
    _state_start_ms = AP_HAL::millis();
    _trigger_was_pressed = false;
    _primed = false;
}

void AP_WaterSampler::update()
{
    if (!enabled()) {
        return;
    }

    const uint32_t now_ms = AP_HAL::millis();

    uint16_t trig_pwm = 0;
    const bool trigger_pressed = get_trigger_pwm(trig_pwm) && (trig_pwm > WS_TRIGGER_PRESS_PWM);

    if (!_primed) {
        // First call after boot. Only record the state of the trigger so that
        // a channel which is already high (stuck button, or an output that has
        // not been reset) does not start a sample by itself. No output is
        // written here either.
        _trigger_was_pressed = trigger_pressed;
        _primed = true;
        return;
    }

    // Only the transition into "pressed" starts a sample, so holding the
    // button down does not repeat it
    const bool trigger_edge = trigger_pressed && !_trigger_was_pressed;
    _trigger_was_pressed = trigger_pressed;

    if (trigger_edge && _state == State::IDLE) {
        _state = State::OPENING;
        _state_start_ms = now_ms;
        set_motor_pwm(_open_pwm.get());
    }

    switch (_state) {
    case State::OPENING:
        if (now_ms - _state_start_ms >= (uint32_t)(_open_time.get() * 1000.0f)) {
            // drive the rod back in, which is also the idle output
            _state = State::IDLE;
            _state_start_ms = now_ms;
            set_motor_pwm(_close_pwm.get());
        }
        break;

    case State::IDLE:
        break;
    }
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

// read back the pwm the ground station last wrote to the trigger channel
bool AP_WaterSampler::get_trigger_pwm(uint16_t &pwm) const
{
    const int8_t chan = _trigger_chan.get();
    if (chan < 1) {
        return false;
    }
    return SRV_Channels::get_output_pwm_chan(chan - 1, pwm);
}

#endif  // AP_WATERSAMPLER_ENABLED
