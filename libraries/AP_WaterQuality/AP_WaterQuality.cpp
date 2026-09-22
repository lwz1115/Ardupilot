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

#include "AP_WaterQuality.h"

#if AP_WATERQUALITY_ENABLED

#include <AP_HAL/AP_HAL.h>
#include <AP_Math/AP_Math.h>
#include <GCS_MAVLink/GCS.h>

#include <cmath>
#include <stdlib.h>
#include <string.h>

extern const AP_HAL::HAL& hal;

// the YSI EXO DCP adapter needs at least 19 seconds after power up
// before it starts accepting commands
static const uint32_t WQ_BOOT_DELAY_MS = 19000;

// number of times a command is retried before we give up on it
static const uint8_t WQ_MAX_RETRIES = 3;

// the DCP adapter is fixed at 9600 baud, 8 data bits, no parity, 1 stop bit
static const uint16_t WQ_BAUD = 9600;

// maximum number of lines parsed per update() call, so that a burst of
// data from the sensor can never stall the scheduler
static const uint8_t WQ_MAX_LINES_PER_UPDATE = 4;

// data older than this is treated as unhealthy
static const uint32_t WQ_STALE_MS = 10000;

// minimum interval between GCS warnings, so that a persistent fault
// cannot flood the telemetry link
static const uint32_t WQ_WARN_INTERVAL_MS = 30000;

const AP_Param::GroupInfo AP_WaterQuality::var_info[] = {

    // @Param: ENABLE
    // @DisplayName: Water quality sensor enable
    // @Description: Enable the water quality sensor driver. When disabled no serial port is opened and no data is sent to the GCS. This is a compile-time enabled feature, see AP_WATERQUALITY_ENABLED.
    // @Values: 0:Disabled,1:YSI EXO DCP adapter
    // @User: Standard
    // @RebootRequired: True
    AP_GROUPINFO_FLAGS("ENABLE", 1, AP_WaterQuality, _enable, 1, AP_PARAM_FLAG_ENABLE),

    // @Param: SERIAL
    // @DisplayName: Water quality sensor serial port
    // @Description: Serial port number that the water quality sensor is connected to. The matching SERIALx_PROTOCOL parameter must be set to WaterQuality (51) so that the port is not claimed by another driver. Do not set it to None, as that disables the port RX and TX pins. The port is always opened at 9600 baud, 8N1.
    // @Range: 0 8
    // @Increment: 1
    // @User: Standard
    // @RebootRequired: True
    AP_GROUPINFO("SERIAL", 2, AP_WaterQuality, _serial_port, WQ_DEFAULT_SERIAL_PORT),

    // @Param: TIMEOUT
    // @DisplayName: Water quality sensor response timeout
    // @Description: Time to wait for a response from the sensor before retrying the command.
    // @Units: s
    // @Range: 0.2 10
    // @Increment: 0.1
    // @User: Advanced
    AP_GROUPINFO("TIMEOUT", 3, AP_WaterQuality, _timeout, 2.0f),

    // @Param: RATE
    // @DisplayName: Water quality sensor sample rate
    // @Description: Rate that the sensor is polled for a new line of values. Each sample sends one NAMED_VALUE_FLOAT message per measurement, and those messages are not subject to the SRx_ stream rate limits, so raising this rate increases telemetry bandwidth usage. Set WQ_SEND to 0 to log without sending. The vehicle scheduler must call the driver at least this fast.
    // @Units: Hz
    // @Range: 0.1 5
    // @Increment: 0.1
    // @User: Advanced
    AP_GROUPINFO("RATE", 4, AP_WaterQuality, _rate, 1.0f),

    // @Param: SEND
    // @DisplayName: Water quality send to GCS
    // @Description: Send the water quality values to the GCS as NAMED_VALUE_FLOAT messages. The values are also written to the on-board log as NVF records.
    // @Values: 0:Disabled,1:Enabled
    // @User: Advanced
    AP_GROUPINFO("SEND", 5, AP_WaterQuality, _send_gcs, 1),

    AP_GROUPEND
};

AP_WaterQuality *AP_WaterQuality::_singleton = nullptr;

AP_WaterQuality::AP_WaterQuality()
{
    AP_Param::setup_object_defaults(this, var_info);

    // bring the driver state to a known value. init() repeats this, but
    // the driver must be safe to query before init() has been called
    _uart = nullptr;
    _line_len = 0;
    _discard_line = false;
    _param_count = 0;
    _retry_count = 0;
    _have_data = false;
    _boot_start_ms = 0;
    _state_start_ms = 0;
    _last_data_ms = 0;
    _last_warn_ms = 0;
    _state = State::WAIT_BOOT;
    for (uint8_t i = 0; i < max_params; i++) {
        _param_codes[i] = 0;
        _param_values[i] = NAN;
    }

    if (_singleton != nullptr) {
        AP_HAL::panic("AP_WaterQuality must be singleton");
    }
    _singleton = this;
}

AP_WaterQuality *AP_WaterQuality::get_singleton()
{
    return _singleton;
}

// initialise the driver. Safe to call when the driver is disabled, in
// which case no serial port is touched
void AP_WaterQuality::init()
{
    _uart = nullptr;
    _line_len = 0;
    _discard_line = false;
    _param_count = 0;
    _retry_count = 0;
    _have_data = false;
    _state = State::WAIT_BOOT;
    _boot_start_ms = AP_HAL::millis();
    _state_start_ms = _boot_start_ms;
    _last_data_ms = 0;
    _last_warn_ms = 0;

    for (uint8_t i = 0; i < max_params; i++) {
        _param_codes[i] = 0;
        _param_values[i] = NAN;
    }

    if (!enabled()) {
        return;
    }

    const int8_t port = _serial_port.get();
    if (port < 0) {
        return;
    }

    _uart = hal.serial(port);
    if (_uart == nullptr) {
        return;
    }

    _uart->begin(WQ_BAUD);
}

void AP_WaterQuality::update()
{
    if (!enabled() || _uart == nullptr) {
        return;
    }

    const uint32_t now_ms = AP_HAL::millis();

    // parse any complete lines that have arrived
    for (uint8_t i = 0; i < WQ_MAX_LINES_PER_UPDATE; i++) {
        if (!read_line()) {
            break;
        }

        switch (_state) {
        case State::WAIT_PARA:
            if (parse_param_line(_line_buf)) {
                _state = State::SEND_DATA;
                _state_start_ms = now_ms;
                _retry_count = 0;
            }
            break;

        case State::WAIT_DATA:
            if (parse_data_line(_line_buf)) {
                _have_data = true;
                _last_data_ms = now_ms;
                _state_start_ms = now_ms;
                _retry_count = 0;
                send_to_gcs();
            }
            break;

        default:
            // lines received in any other state are not expected, ignore them
            break;
        }
    }

    // run the state machine
    switch (_state) {
    case State::WAIT_BOOT:
        if (now_ms - _boot_start_ms >= WQ_BOOT_DELAY_MS) {
            _state = State::SEND_PARA;
        }
        break;

    case State::SEND_PARA:
        send_command("para");
        _state = State::WAIT_PARA;
        _state_start_ms = now_ms;
        break;

    case State::WAIT_PARA:
        if (now_ms - _state_start_ms >= (uint32_t)(_timeout.get() * 1000.0f)) {
            if (++_retry_count > WQ_MAX_RETRIES) {
                // The adapter is not answering the "para" command. Fall
                // back to the default YSI EXO parameter order so that we
                // can still try to read values. Warn about it, because if
                // the sonde is configured differently then the parameter
                // codes and the values will not line up.
                set_default_param_codes();
                send_warning("WQ: para timeout, using default param codes");
                _state = State::SEND_DATA;
                _state_start_ms = now_ms;
                _retry_count = 0;
            } else {
                _state = State::SEND_PARA;
            }
        }
        break;

    case State::SEND_DATA: {
        // rate limit the sampling
        if (_have_data && (now_ms - _last_data_ms) < sample_period_ms()) {
            break;
        }
        send_command("data");
        _state = State::WAIT_DATA;
        _state_start_ms = now_ms;
        break;
    }

    case State::WAIT_DATA:
        if (now_ms - _state_start_ms >= (uint32_t)(_timeout.get() * 1000.0f)) {
            if (++_retry_count > WQ_MAX_RETRIES) {
                // resynchronise with the adapter and re-read the
                // parameter list
                _retry_count = 0;
                _state = State::SEND_PARA;
            } else {
                _state = State::SEND_DATA;
            }
        }
        break;
    }
}

bool AP_WaterQuality::healthy() const
{
    if (!enabled() || !_have_data || _last_data_ms == 0) {
        return false;
    }
    if ((AP_HAL::millis() - _last_data_ms) >= WQ_STALE_MS) {
        return false;
    }
    // Require at least one usable reading. A line that parsed but held
    // nothing except NaN means the sonde answered without returning any
    // usable data, which is not a healthy sensor.
    for (uint8_t i = 0; i < _param_count; i++) {
        if (!std::isnan(_param_values[i])) {
            return true;
        }
    }
    return false;
}

float AP_WaterQuality::get_param_value(uint8_t code, float default_value) const
{
    const int8_t index = get_param_index(code);
    if (index < 0) {
        return default_value;
    }
    return _param_values[index];
}

int8_t AP_WaterQuality::get_param_index(uint8_t code) const
{
    for (uint8_t i = 0; i < _param_count; i++) {
        if (_param_codes[i] == code) {
            return (int8_t)i;
        }
    }
    return -1;
}

uint32_t AP_WaterQuality::sample_period_ms() const
{
    const float rate = _rate.get();
    if (!is_positive(rate)) {
        return 1000;
    }
    return (uint32_t)constrain_float(1000.0f / rate, 100.0f, 60000.0f);
}

// read from the uart until one complete line is available. Any bytes
// after the line terminator are left in the uart buffer for the next call
bool AP_WaterQuality::read_line()
{
    if (_uart == nullptr) {
        return false;
    }

    while (_uart->available() > 0) {
        const int16_t c = _uart->read();
        if (c < 0) {
            break;
        }

        // the adapter terminates lines with CR LF
        if (c == '\r' || c == '\n') {
            if (_discard_line) {
                // this terminator ends the over-long line being dropped
                _discard_line = false;
                _line_len = 0;
                continue;
            }
            if (_line_len == 0) {
                // leading or repeated line terminator, ignore it
                continue;
            }
            _line_buf[_line_len] = '\0';
            _line_len = 0;
            return true;
        }

        if (_discard_line) {
            // still dropping an over-long line
            continue;
        }

        if (_line_len < ARRAY_SIZE(_line_buf) - 1) {
            _line_buf[_line_len++] = (char)c;
        } else {
            // The line does not fit in the buffer. Drop the whole line
            // rather than treating its tail as the start of a new one.
            _discard_line = true;
            _line_len = 0;
        }
    }

    return false;
}

// send a command to the adapter. The DCP adapter requires a leading CR
// or the first character of the command is lost
void AP_WaterQuality::send_command(const char *cmd)
{
    if (_uart == nullptr) {
        return;
    }

    const uint8_t prefix = '\r';
    _uart->write(&prefix, 1);

    _uart->write((const uint8_t *)cmd, strlen(cmd));

    const uint8_t suffix[] = { '\r', '\n' };
    _uart->write(suffix, ARRAY_SIZE(suffix));
}

// parse the response to "para", e.g. "5 18 212 48 193 215 37"
bool AP_WaterQuality::parse_param_line(const char *line)
{
    // a "data" response always contains decimal points, a "para" response
    // never does. Reject a data line so that a stale reading cannot be
    // mistaken for a list of parameter codes
    if (strchr(line, '.') != nullptr) {
        return false;
    }

    uint8_t count = 0;
    const char *p = line;

    while (count < max_params && *p != '\0') {
        // skip separators
        while (*p == ' ' || *p == '\t' || *p == ',') {
            p++;
        }
        if (*p == '\0') {
            break;
        }

        char *endp = nullptr;
        const long code = strtol(p, &endp, 10);

        if (endp == p) {
            // this token is not a number. Skip it and carry on so that a
            // text label does not stop us parsing the code list
            while (*p != '\0' && *p != ' ' && *p != '\t' && *p != ',') {
                p++;
            }
            continue;
        }
        p = endp;

        if (code >= 0 && code <= 255) {
            _param_codes[count++] = (uint8_t)code;
        }
    }

    if (count == 0) {
        return false;
    }

    _param_count = count;

    // the previous values are no longer valid
    for (uint8_t i = 0; i < max_params; i++) {
        _param_values[i] = NAN;
    }

    return true;
}

// parse the response to "data", e.g. "445.58 6.82 5.06 0.39 0.37 360.82 0.07"
bool AP_WaterQuality::parse_data_line(const char *line)
{
    if (_param_count == 0) {
        return false;
    }

    uint8_t index = 0;
    // Number of fields strtof could actually parse. This is used to tell
    // a data line apart from a prompt or error response such as
    // "?Command", which also occupies one position per token but
    // contains no numbers at all.
    uint8_t parsed = 0;
    const char *p = line;

    while (index < _param_count && *p != '\0') {
        // skip separators
        while (*p == ' ' || *p == '\t' || *p == ',') {
            p++;
        }
        if (*p == '\0') {
            break;
        }

        char *endp = nullptr;
        const float value = strtof(p, &endp);

        if (endp == p) {
            // This field is not a number, but it still occupies a
            // position in the line. Consume an index so that the
            // remaining values stay aligned with their parameter codes,
            // and mark the field as having no reading.
            _param_values[index] = NAN;
            while (*p != '\0' && *p != ' ' && *p != '\t' && *p != ',') {
                p++;
            }
            index++;
            continue;
        }
        p = endp;
        parsed++;

        // An unusable value marks the field as having no reading. It is
        // stored as NaN rather than zero so that a missing measurement
        // cannot be mistaken for a genuine reading, and rather than
        // keeping the previous value so that stale data is never
        // reported as fresh.
        if (std::isnan(value) || std::isinf(value)) {
            _param_values[index] = NAN;
        } else {
            _param_values[index] = value;
        }
        index++;
    }

    // mark any trailing values we did not receive as having no reading
    for (uint8_t i = index; i < _param_count; i++) {
        _param_values[i] = NAN;
    }

    if (parsed == 0) {
        // No field on this line held a number, so this is not a data line at
        // all. The adapter sends a prompt, a command echo and an error
        // response around every reading, and none of those may be reported
        // as a value count mismatch.
        return false;
    }

    if (parsed != _param_count) {
        // The line did carry readings, but not one for every measurement the
        // sonde reported. Surface it rather than silently presenting partial
        // data as if it were complete.
        send_warning("WQ: parameter/value count mismatch");
    }

    return true;
}

void AP_WaterQuality::set_default_param_codes()
{
    // parameter order used by a YSI EXO sonde, applied when the adapter
    // does not answer the "para" command
    static const uint8_t default_codes[] = {
        PARAM_CODE_CONDUCTIVITY,
        PARAM_CODE_PH,
        PARAM_CODE_ODO,
        PARAM_CODE_NH4,
        PARAM_CODE_CHLOROPHYLL,
        PARAM_CODE_TAL_PC,
        PARAM_CODE_TURBIDITY,
    };

    const uint8_t count = MIN(ARRAY_SIZE(default_codes), max_params);

    for (uint8_t i = 0; i < count; i++) {
        _param_codes[i] = default_codes[i];
        _param_values[i] = NAN;
    }
    _param_count = count;
}

// Mapping from YSI parameter code to the MAVLink name used to report it.
// MAVLink limits the name field to 10 characters.
struct WaterQualityParamName {
    uint8_t code;
    const char *name;
};

static const WaterQualityParamName wq_param_names[] = {
    { AP_WaterQuality::PARAM_CODE_CONDUCTIVITY, "COND" },
    { AP_WaterQuality::PARAM_CODE_PH,           "PH" },
    { AP_WaterQuality::PARAM_CODE_ODO,          "DO" },
    { AP_WaterQuality::PARAM_CODE_NH4,          "NH4" },
    { AP_WaterQuality::PARAM_CODE_CHLOROPHYLL,  "CHLOR" },
    { AP_WaterQuality::PARAM_CODE_TAL_PC,       "TALPC" },
    { AP_WaterQuality::PARAM_CODE_TURBIDITY,    "TURB" },
};

const char *AP_WaterQuality::name_for_param_code(uint8_t code)
{
    for (const WaterQualityParamName &entry : wq_param_names) {
        if (entry.code == code) {
            return entry.name;
        }
    }
    return nullptr;
}

void AP_WaterQuality::send_to_gcs() const
{
    if (!_have_data || _send_gcs.get() == 0) {
        return;
    }

    // Report every measurement the sonde is actually configured to send,
    // rather than a fixed list, so that changing the sonde's configuration
    // does not require a code change here.
    // Each value goes out as a MAVLink NAMED_VALUE_FLOAT message, which is
    // visible in the QGC MAVLink inspector. send_named_float() also writes
    // an NVF record to the on-board log.
    for (uint8_t i = 0; i < _param_count; i++) {
        const char *name = name_for_param_code(_param_codes[i]);
        if (name == nullptr) {
            // no MAVLink name defined for this parameter code
            continue;
        }
        gcs().send_named_float(name, _param_values[i]);
    }
}

void AP_WaterQuality::send_warning(const char *msg)
{
    const uint32_t now_ms = AP_HAL::millis();
    if (_last_warn_ms != 0 && (now_ms - _last_warn_ms) < WQ_WARN_INTERVAL_MS) {
        return;
    }
    _last_warn_ms = now_ms;
    GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "%s", msg);
}

#endif  // AP_WATERQUALITY_ENABLED
