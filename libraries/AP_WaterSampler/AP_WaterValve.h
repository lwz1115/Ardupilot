#pragma once

#include "AP_WaterSampler_config.h"

#if AP_WATERSAMPLER_ENABLED

#include <AP_Param/AP_Param.h>

/*
  Solenoid valve driver.

  A two position solenoid valve decides which sample bottle the pump draws
  from. Its coil is switched by a relay board whose input triggers on a LOW
  level:

    relay released (line HIGH) -> bottle 1, the de-energised default
    relay energised (line LOW) -> bottle 2

  The line is driven as a plain GPIO level rather than servo PWM, so the pin
  number is the GPIO number that the board definition gives to that output.
  Both boards used here give their second output GPIO(51), which is AUX2 on the
  SIYI N7 and PWM2 on the SkyDroid-S3:

    SIYI N7        PA10 TIM1_CH3 TIM1 PWM(2) GPIO(51)   == AUXOUT2
    SkyDroid-S3    PE11 TIM1_CH2 TIM1 PWM(2) GPIO(51)   == PWM2

  The matching SERVOn_FUNCTION must be -1 (GPIO) for the pin to be usable, and
  the channel bit must be in SERVO_GPIO_MASK (or SERVOn_FUNCTION must be -1).
  Setting the function to 0 is not enough: the pin then stays owned by the PWM
  timer and every GPIO write is silently ignored.

  init() releases the coil immediately, so the valve is sitting on bottle 1, its
  de-energised default, before the vehicle finishes booting.

  The valve is independent of the rod's WS_ENABLE, so that switching the rod
  driver off still leaves the valve in a known (released) state.
 */
class AP_WaterValve
{
public:
    AP_WaterValve();

    CLASS_NO_COPY(AP_WaterValve);

    // parameter block
    static const struct AP_Param::GroupInfo var_info[];

    // Initialise the driver. Releases the coil so the valve selects bottle 1
    void init();

    // true if the driver has been enabled by the user (WS_VALVE_ENABLE)
    bool enabled() const { return _enable.get() != 0; }

    // Select a bottle. Bottle 2 energises the coil, bottle 1 releases it.
    // A disabled driver always releases the coil, whatever is asked for
    void set_bottle(uint8_t bottle);

    // release the coil, which selects bottle 1
    void release() { set_bottle(1); }

    // the bottle that is currently selected, 1 or 2
    uint8_t bottle() const { return _bottle; }

    // time the valve needs to actuate after a bottle is selected
    float delay_s() const { return _delay.get(); }

private:
    // drive the relay input. This hardware energises on a LOW level
    void set_coil(bool energised);

    // parameters
    AP_Int8  _enable;      // WS_VALVE_ENABLE
    AP_Int16 _pin;         // WS_VALVE_PIN
    AP_Int8  _active_low;  // WS_VALVE_ACTLOW
    AP_Float _delay;       // WS_VALVE_DELAY

    // currently selected bottle, 1 or 2
    uint8_t _bottle;
};

#endif  // AP_WATERSAMPLER_ENABLED
