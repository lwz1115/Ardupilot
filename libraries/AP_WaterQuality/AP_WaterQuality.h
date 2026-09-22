#pragma once

#include "AP_WaterQuality_config.h"

#if AP_WATERQUALITY_ENABLED

#include <AP_Param/AP_Param.h>
#include <AP_HAL/AP_HAL.h>

/*
  Water quality sensor driver for ArduPilot.

  Currently supports the YSI EXO DCP (Data Collection Platform) adapter
  which is connected to a spare flight controller serial port (RS232
  converted to TTL). Communication is 9600 8N1.

  The adapter is queried with two ASCII commands, each of which must be
  prefixed with a CR character or the first character is lost:

    para -> returns the list of parameter codes, e.g. "5 18 212 48 193 215 37"
    data -> returns one line of values, in the same order as "para",
            e.g. "445.58 6.82 5.06 0.39 0.37 360.82 0.07"

  The values are made available to the vehicle through get_param_value()
  and can be sent to the GCS and logged by calling send_named_float(),
  which emits MAVLink NAMED_VALUE_FLOAT messages and also writes an NVF
  record to the on-board log.
 */
class AP_WaterQuality
{
public:
    AP_WaterQuality();

    /* Do not allow copies */
    CLASS_NO_COPY(AP_WaterQuality);

    static AP_WaterQuality *get_singleton();

    // YSI EXO parameter codes
    enum ParamCode : uint8_t {
        PARAM_CODE_CONDUCTIVITY = 5,    // uS/cm
        PARAM_CODE_PH           = 18,   // pH
        PARAM_CODE_ODO          = 212,  // mg/L optical dissolved oxygen
        PARAM_CODE_NH4          = 48,   // mg/L ammonium
        PARAM_CODE_CHLOROPHYLL  = 193,  // ug/L
        PARAM_CODE_TAL_PC       = 215,  // cells/mL total algae phycocyanin
        PARAM_CODE_TURBIDITY    = 37,   // NTU
    };

    // maximum number of parameter codes we can hold
    static const uint8_t max_params = 10;

    // Initialise the driver and open the serial port. Should be called
    // once from the vehicle init()
    void init();

    // Run the driver. Should be called from the vehicle scheduler at a
    // rate at least as fast as the configured sample rate (WQ_RATE)
    void update();

    // true if the driver has been enabled by the user (WQ_ENABLE)
    bool enabled() const { return _enable.get() != 0; }

    // true if we have received data recently
    bool healthy() const;

    // return the value for a YSI parameter code. Returns default_value
    // if the code was not reported by the sensor's "para" command
    float get_param_value(uint8_t code, float default_value = 0.0f) const;

    // convenience accessors for the standard YSI EXO parameter codes
    float conductivity() const { return get_param_value(PARAM_CODE_CONDUCTIVITY); }
    float ph() const { return get_param_value(PARAM_CODE_PH); }
    float dissolved_oxygen() const { return get_param_value(PARAM_CODE_ODO); }
    float ammonium() const { return get_param_value(PARAM_CODE_NH4); }
    float chlorophyll() const { return get_param_value(PARAM_CODE_CHLOROPHYLL); }
    float tal_pc() const { return get_param_value(PARAM_CODE_TAL_PC); }
    float turbidity() const { return get_param_value(PARAM_CODE_TURBIDITY); }

    // parameter block
    static const struct AP_Param::GroupInfo var_info[];

private:

    // driver state machine
    enum class State : uint8_t {
        WAIT_BOOT = 0,  // waiting for the adapter to finish its own boot
        SEND_PARA,      // send the "para" command
        WAIT_PARA,      // waiting for the parameter code list
        SEND_DATA,      // send the "data" command
        WAIT_DATA,      // waiting for a line of values
    };

    // drain the uart and return the next complete line in _line_buf
    bool read_line();

    // send a command with the mandatory CR prefix and CR LF suffix
    void send_command(const char *cmd);

    // parse the response to "para", e.g. "5 18 212 48 193 215 37"
    bool parse_param_line(const char *line);

    // parse the response to "data", e.g. "445.58 6.82 5.06 0.39 0.37 360.82 0.07"
    bool parse_data_line(const char *line);

    // return the index of a parameter code, or -1 if not present
    int8_t get_param_index(uint8_t code) const;

    // send the latest values to the GCS and the on-board log
    void send_to_gcs() const;

    // period between samples in milliseconds
    uint32_t sample_period_ms() const;

    // fall back to the default YSI EXO parameter code order when the
    // adapter does not answer the "para" command
    void set_default_param_codes();

    // return the MAVLink name used to report a parameter code, or nullptr
    // when this driver has no name defined for that code
    static const char *name_for_param_code(uint8_t code);

    // send a warning to the GCS. Rate limited so that a persistent fault
    // cannot flood the telemetry link.
    void send_warning(const char *msg);

    // parameters
    AP_Int8  _enable;       // WQ_ENABLE
    AP_Int8  _serial_port;  // WQ_SERIAL
    AP_Float _timeout;      // WQ_TIMEOUT
    AP_Float _rate;         // WQ_RATE
    AP_Int8  _send_gcs;     // WQ_SEND

    // serial port
    AP_HAL::UARTDriver *_uart;

    // line assembly buffer
    static const uint8_t line_buffer_size = 160;
    char _line_buf[line_buffer_size];
    uint8_t _line_len;
    bool _discard_line;     // discarding an over-long line until its terminator

    // parameter codes reported by "para" and their latest values
    uint8_t _param_codes[max_params];
    uint8_t _param_count;
    float _param_values[max_params];

    // state machine bookkeeping
    State _state;
    uint32_t _boot_start_ms;
    uint32_t _state_start_ms;
    uint32_t _last_data_ms;
    uint32_t _last_warn_ms;     // rate limiter for GCS warnings
    uint8_t _retry_count;
    bool _have_data;

    static AP_WaterQuality *_singleton;
};

#endif  // AP_WATERQUALITY_ENABLED
