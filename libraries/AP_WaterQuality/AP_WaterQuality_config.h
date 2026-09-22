#pragma once

#include <AP_HAL/AP_HAL_Boards.h>

// Water quality sensor support. Enabled by default so that the driver is
// available without a custom build.
#ifndef AP_WATERQUALITY_ENABLED
#define AP_WATERQUALITY_ENABLED 1
#endif

// Default serial port for the sensor. Port 6 is UART7. The port itself is
// still controlled by the WQ_ENABLE parameter at runtime.
#ifndef WQ_DEFAULT_SERIAL_PORT
#define WQ_DEFAULT_SERIAL_PORT 6
#endif

