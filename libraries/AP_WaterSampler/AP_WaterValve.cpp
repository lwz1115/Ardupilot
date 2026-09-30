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

#include "AP_WaterValve.h"

#if AP_WATERSAMPLER_ENABLED

#include <AP_HAL/AP_HAL.h>

extern const AP_HAL::HAL& hal;

const AP_Param::GroupInfo AP_WaterValve::var_info[] = {

    // @Param: ENABLE
    // @DisplayName: Water valve enable
    // @Description: Enable the sample bottle selection valve. When disabled the relay is released, which selects bottle 1.
    // @Values: 0:Disabled,1:Enabled
    // @User: Standard
    // @RebootRequired: True
    AP_GROUPINFO_FLAGS("ENABLE", 1, AP_WaterValve, _enable, 1, AP_PARAM_FLAG_ENABLE),

    // @Param: PIN
    // @DisplayName: Water valve GPIO pin
    // @Description: GPIO number of the output that drives the bottle selection relay. The relay triggers on a LOW level. 51 is the second output on both the SIYI N7 and the SkyDroid-S3, and the matching SERVOn_FUNCTION must be -1 (GPIO).
    // @Values: -1:Disabled,50:AUXOUT1,51:AUXOUT2,52:AUXOUT3,53:AUXOUT4,54:AUXOUT5,55:AUXOUT6,101:MainOut1,102:MainOut2,103:MainOut3,104:MainOut4,105:MainOut5,106:MainOut6,107:MainOut7,108:MainOut8
    // @Range: -1 108
    // @User: Standard
    // @RebootRequired: True
    AP_GROUPINFO("PIN", 2, AP_WaterValve, _pin, WS_VALVE_PIN_DEFAULT),

    // @Param: ACTLOW
    // @DisplayName: Water valve relay active low
    // @Description: Set to 1 when the relay board is energised by a LOW level, which is the usual case for relay modules with an opto-isolated input. Set to 0 for a relay that is energised by a HIGH level.
    // @Values: 0:Relay is active high,1:Relay is active low
    // @User: Standard
    AP_GROUPINFO("ACTLOW", 3, AP_WaterValve, _active_low, 1),

    // @Param: DELAY
    // @DisplayName: Water valve actuation time
    // @Description: Time the sampling sequence waits after selecting a bottle before the rod starts moving, so the relay has time to actuate the solenoid valve. The wait happens for bottle 1 as well, where the coil is released rather than energised.
    // @Units: s
    // @Range: 0 60
    // @Increment: 0.5
    // @User: Standard
    AP_GROUPINFO("DELAY", 4, AP_WaterValve, _delay, WS_DEFAULT_VALVE_DELAY_S),

    AP_GROUPEND
};

AP_WaterValve::AP_WaterValve()
{
    AP_Param::setup_object_defaults(this, var_info);

    // bottle 1 is the released, de-energised default
    _bottle = 1;
}

// initialise the driver. The coil is released straight away so the valve is
// already sitting on bottle 1 when the vehicle finishes booting, and only the
// sampling sequence can select bottle 2
void AP_WaterValve::init()
{
    _bottle = 1;
    set_coil(false);
}

void AP_WaterValve::set_bottle(uint8_t bottle)
{
    if (!enabled()) {
        // a disabled driver must never leave the coil energised
        bottle = 1;
    }
    if (bottle < 1 || bottle > 2) {
        return;
    }
    _bottle = bottle;
    set_coil(bottle == 2);
}

// drive the relay input. "energised" selects bottle 2, and on this hardware
// that means driving the pin low
void AP_WaterValve::set_coil(bool energised)
{
    const int16_t pin = _pin.get();
    if (pin < 0) {
        return;
    }
    const bool active_low = _active_low.get() != 0;
    const bool level = active_low ? !energised : energised;
    hal.gpio->pinMode(pin, HAL_GPIO_OUTPUT);
    hal.gpio->write(pin, level ? 1 : 0);
}

#endif  // AP_WATERSAMPLER_ENABLED
