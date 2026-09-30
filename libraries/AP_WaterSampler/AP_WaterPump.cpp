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

#include "AP_WaterPump.h"

#if AP_WATERSAMPLER_ENABLED

#include <AP_HAL/AP_HAL.h>
#include <SRV_Channel/SRV_Channel.h>

extern const AP_HAL::HAL& hal;

const AP_Param::GroupInfo AP_WaterPump::var_info[] = {

    // @Param: ENABLE
    // @DisplayName: Water pump enable
    // @Description: Enable the water pump driver. When disabled the pump is stopped and both control lines are driven to the idle level.
    // @Values: 0:Disabled,1:Enabled
    // @User: Standard
    // @RebootRequired: True
    AP_GROUPINFO_FLAGS("ENABLE", 1, AP_WaterPump, _enable, 1, AP_PARAM_FLAG_ENABLE),

    // @Param: FWD_PIN
    // @DisplayName: Water pump forward GPIO pin
    // @Description: GPIO number of the output that drives the water pump forward (draws water in). The pump controller triggers on a LOW level. 52 is AUXOUT3 on both the SIYI N7 and the SkyDroid-S3, and the matching SERVOn_FUNCTION must be 0 (Disabled).
    // @Values: -1:Disabled,50:AUXOUT1,51:AUXOUT2,52:AUXOUT3,53:AUXOUT4,54:AUXOUT5,55:AUXOUT6,101:MainOut1,102:MainOut2,103:MainOut3,104:MainOut4,105:MainOut5,106:MainOut6,107:MainOut7,108:MainOut8
    // @Range: -1 108
    // @User: Standard
    // @RebootRequired: True
    AP_GROUPINFO("FWD_PIN", 2, AP_WaterPump, _fwd_pin, WS_PUMP_FWD_PIN_DEFAULT),

    // @Param: REV_PIN
    // @DisplayName: Water pump reverse GPIO pin
    // @Description: GPIO number of the output that drives the water pump in reverse (pushes water out). The pump controller triggers on a LOW level. 53 is AUXOUT4 on both the SIYI N7 and the SkyDroid-S3, and the matching SERVOn_FUNCTION must be 0 (Disabled).
    // @Values: -1:Disabled,50:AUXOUT1,51:AUXOUT2,52:AUXOUT3,53:AUXOUT4,54:AUXOUT5,55:AUXOUT6,101:MainOut1,102:MainOut2,103:MainOut3,104:MainOut4,105:MainOut5,106:MainOut6,107:MainOut7,108:MainOut8
    // @Range: -1 108
    // @User: Standard
    // @RebootRequired: True
    AP_GROUPINFO("REV_PIN", 3, AP_WaterPump, _rev_pin, WS_PUMP_REV_PIN_DEFAULT),

    // @Param: CMD_CH
    // @DisplayName: Water pump command channel
    // @Description: Servo channel the ground station writes to in order to select a direction. Around 1100 runs the pump backwards, 1500 or below 1000 stops it, and 1800 or more runs it forwards. The direction latches, so it keeps running until another value is written. Nothing is wired to this output.
    // @Range: 1 16
    // @Increment: 1
    // @User: Standard
    AP_GROUPINFO("CMD_CH", 4, AP_WaterPump, _cmd_chan, WS_PUMP_CMD_CHAN_DEFAULT),

    AP_GROUPEND
};

AP_WaterPump::AP_WaterPump()
{
    AP_Param::setup_object_defaults(this, var_info);

    // nothing is running until init() has driven the lines
    _mode = Mode::STOP;
    _sequence_state = SequenceState::NONE;
}

// initialise the driver. Both control lines are driven to the idle (HIGH) state
// straight away, so the pump cannot be running when the vehicle finishes
// booting and only a ground station command can start it
void AP_WaterPump::init()
{
    _mode = Mode::STOP;
    apply_mode(_mode);
}

void AP_WaterPump::update()
{
    if (!enabled()) {
        // a disabled driver must never leave the pump running, and it must not
        // keep holding control on behalf of a sequence that can no longer run
        _sequence_state = SequenceState::NONE;
        if (_mode != Mode::STOP) {
            _mode = Mode::STOP;
            apply_mode(_mode);
        }
        return;
    }

    Mode new_mode;

    if (sequence_has_control()) {
        // the sampling sequence owns the pump, so the command channel is
        // ignored and a stale value there cannot restart the pump
        new_mode = (_sequence_state == SequenceState::FORWARD) ? Mode::FORWARD : Mode::STOP;
    } else {
        uint16_t cmd_pwm = 0;
        if (!get_chan_pwm(_cmd_chan.get(), cmd_pwm)) {
            return;
        }
        new_mode = mode_from_command(cmd_pwm);
    }

    if (new_mode != _mode) {
        _mode = new_mode;
        apply_mode(_mode);
    }
}

// drive both lines to match the requested mode. Because the direction is held
// in a single variable, the two lines can never be low at the same time, which
// is the interlock between forward and reverse
void AP_WaterPump::apply_mode(Mode mode)
{
    set_line(_fwd_pin.get(), mode == Mode::FORWARD);
    set_line(_rev_pin.get(), mode == Mode::REVERSE);
}

// turn the value on the command channel into a direction. Anything that is not
// recognisable stops the pump, which also covers the channel still reading zero
// before the ground station has written to it
AP_WaterPump::Mode AP_WaterPump::mode_from_command(uint16_t pwm)
{
    if (pwm >= WS_PUMP_CMD_FWD_PWM) {
        return Mode::FORWARD;
    }
    if (pwm >= WS_PUMP_CMD_REV_MIN_PWM && pwm <= WS_PUMP_CMD_REV_PWM) {
        return Mode::REVERSE;
    }
    return Mode::STOP;
}

// drive one control line. This pump controller triggers on a LOW level, so
// "active" means drive the pin low and "idle" means drive it high
void AP_WaterPump::set_line(int16_t pin, bool active)
{
    if (pin < 0) {
        return;
    }
    hal.gpio->pinMode(pin, HAL_GPIO_OUTPUT);
    hal.gpio->write(pin, active ? 0 : 1);
}

// read back the value the ground station last wrote to the command channel
bool AP_WaterPump::get_chan_pwm(int8_t chan, uint16_t &pwm)
{
    if (chan < 1) {
        return false;
    }
    return SRV_Channels::get_output_pwm_chan(chan - 1, pwm);
}

#endif  // AP_WATERSAMPLER_ENABLED
