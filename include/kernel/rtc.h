/* rtc.h - CMOS-backed wall-clock service.
 *
 * The RTC is sampled once during boot. Wall time then advances from the
 * monotonic PIT clock, so callers never need to poll the CMOS device.
 */
#pragma once

#include <kernel/types.h>

typedef struct {
    u16 year;
    u8 month;
    u8 day;
    u8 hour;
    u8 minute;
    u8 second;
    bool valid;
} rtc_datetime_t;

/* Decode a coherent CMOS register sample. status_b is RTC register B.
 * A zero century value means that the firmware does not expose a century
 * register; years 70..99 map to 1970..1999 and 00..69 to 2000..2069. */
bool rtc_decode_registers(u8 second, u8 minute, u8 hour,
                          u8 day, u8 month, u8 year, u8 century,
                          u8 status_b, rtc_datetime_t* out);

/* Initialize from a stable, valid CMOS sample. */
bool rtc_init(void);
bool rtc_available(void);

/* Return wall-clock time advanced by monotonic PIT elapsed time since boot. */
bool rtc_get_datetime(rtc_datetime_t* out);
