/* rtc.c - Stable CMOS RTC bootstrap and monotonic wall-clock service. */
#include <kernel/rtc.h>
#include <kernel/io.h>
#include <kernel/timer.h>
#include <kernel/serial.h>

#define CMOS_INDEX_PORT 0x70u
#define CMOS_DATA_PORT  0x71u
#define CMOS_SECONDS    0x00u
#define CMOS_MINUTES    0x02u
#define CMOS_HOURS      0x04u
#define CMOS_DAY        0x07u
#define CMOS_MONTH      0x08u
#define CMOS_YEAR       0x09u
#define CMOS_STATUS_A  0x0Au
#define CMOS_STATUS_B  0x0Bu
#define CMOS_STATUS_D  0x0Du
#define CMOS_CENTURY   0x32u
#define CMOS_UIP       0x80u
#define CMOS_VRT       0x80u
#define RTC_BINARY     0x04u
#define RTC_24_HOUR    0x02u

typedef struct {
    u8 second, minute, hour;
    u8 day, month, year, century;
    u8 status_b;
} rtc_sample_t;

static volatile bool wall_clock_valid;
static u64 wall_clock_epoch;
static u64 wall_clock_monotonic_ms;

static u8 cmos_read(u8 reg) {
    /* Keep NMI enabled; only the low seven index bits select a CMOS register. */
    outb(CMOS_INDEX_PORT, (u8)(reg & 0x7Fu));
    return inb(CMOS_DATA_PORT);
}

static bool wait_for_update_end(void) {
    for (u32 spins = 0; spins < 1000000u; spins++) {
        if (!(cmos_read(CMOS_STATUS_A) & CMOS_UIP)) return true;
        asm volatile("pause");
    }
    return false;
}

static bool sample_cmos(rtc_sample_t* sample) {
    if (!sample || !wait_for_update_end()) return false;
    if (!(cmos_read(CMOS_STATUS_D) & CMOS_VRT)) return false;

    sample->second = cmos_read(CMOS_SECONDS);
    sample->minute = cmos_read(CMOS_MINUTES);
    sample->hour = cmos_read(CMOS_HOURS);
    sample->day = cmos_read(CMOS_DAY);
    sample->month = cmos_read(CMOS_MONTH);
    sample->year = cmos_read(CMOS_YEAR);
    sample->century = cmos_read(CMOS_CENTURY);
    sample->status_b = cmos_read(CMOS_STATUS_B);

    /* Reject a sample that straddled the start of the next RTC update. */
    return !(cmos_read(CMOS_STATUS_A) & CMOS_UIP);
}

static bool samples_equal(const rtc_sample_t* a, const rtc_sample_t* b) {
    return a->second == b->second && a->minute == b->minute &&
           a->hour == b->hour && a->day == b->day &&
           a->month == b->month && a->year == b->year &&
           a->century == b->century && a->status_b == b->status_b;
}

static bool decode_value(u8 raw, bool binary, u8* value) {
    if (!value) return false;
    if (binary) {
        *value = raw;
        return true;
    }
    if ((raw & 0x0Fu) > 9u || ((raw >> 4) & 0x0Fu) > 9u) return false;
    *value = (u8)(((raw >> 4) * 10u) + (raw & 0x0Fu));
    return true;
}

static bool is_leap_year(u16 year) {
    return (year % 4u == 0u) && ((year % 100u != 0u) || (year % 400u == 0u));
}

static u8 days_in_month(u16 year, u8 month) {
    static const u8 lengths[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    if (month < 1u || month > 12u) return 0;
    if (month == 2u && is_leap_year(year)) return 29u;
    return lengths[month - 1u];
}

static u64 calendar_to_epoch(const rtc_datetime_t* date) {
    static const u16 days_before_month[12] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
    };
    u64 previous_year = (u64)date->year - 1u;
    u64 base_year = 1969u;
    u64 leap_days = previous_year / 4u - previous_year / 100u +
                    previous_year / 400u -
                    (base_year / 4u - base_year / 100u + base_year / 400u);
    u64 days = ((u64)date->year - 1970u) * 365u + leap_days +
               days_before_month[date->month - 1u] + (date->day - 1u);
    if (date->month > 2u && is_leap_year(date->year)) days++;
    return days * 86400u + (u64)date->hour * 3600u +
           (u64)date->minute * 60u + date->second;
}

bool rtc_decode_registers(u8 second_raw, u8 minute_raw, u8 hour_raw,
                          u8 day_raw, u8 month_raw, u8 year_raw,
                          u8 century_raw, u8 status_b,
                          rtc_datetime_t* out) {
    if (!out) return false;
    out->valid = false;

    bool binary = (status_b & RTC_BINARY) != 0;
    bool twenty_four_hour = (status_b & RTC_24_HOUR) != 0;
    bool is_pm = (hour_raw & 0x80u) != 0;
    u8 second, minute, hour, day, month, year, century = 0;

    if (!decode_value(second_raw, binary, &second) ||
        !decode_value(minute_raw, binary, &minute) ||
        !decode_value((u8)(hour_raw & 0x7Fu), binary, &hour) ||
        !decode_value(day_raw, binary, &day) ||
        !decode_value(month_raw, binary, &month) ||
        !decode_value(year_raw, binary, &year))
        return false;

    if (!twenty_four_hour) {
        if (hour < 1u || hour > 12u) return false;
        hour = (u8)((hour % 12u) + (is_pm ? 12u : 0u));
    }

    u16 full_year;
    if (century_raw && decode_value(century_raw, binary, &century) &&
        century >= 19u && century <= 23u) {
        full_year = (u16)((u16)century * 100u + year);
    } else {
        /* Some firmware leaves the unused century byte at 0xFF or arbitrary
         * CMOS contents. Fall back to the documented two-digit pivot. */
        full_year = (u16)(year >= 70u ? 1900u + year : 2000u + year);
    }

    if (full_year < 1970u || full_year > 2399u ||
        month < 1u || month > 12u || day < 1u ||
        day > days_in_month(full_year, month) ||
        hour > 23u || minute > 59u || second > 59u)
        return false;

    out->year = full_year;
    out->month = month;
    out->day = day;
    out->hour = hour;
    out->minute = minute;
    out->second = second;
    out->valid = true;
    return true;
}

static void epoch_to_calendar(u64 epoch, rtc_datetime_t* out) {
    u64 days = epoch / 86400u;
    u32 day_seconds = (u32)(epoch % 86400u);
    u64 z = days + 719468u;
    u64 era = z / 146097u;
    u32 day_of_era = (u32)(z - era * 146097u);
    u32 year_of_era = (day_of_era - day_of_era / 1460u +
                       day_of_era / 36524u - day_of_era / 146096u) / 365u;
    u32 year = year_of_era + (u32)(era * 400u);
    u32 day_of_year = day_of_era -
                      (365u * year_of_era + year_of_era / 4u -
                       year_of_era / 100u);
    u32 month_prime = (5u * day_of_year + 2u) / 153u;
    u32 day = day_of_year - (153u * month_prime + 2u) / 5u + 1u;
    u32 month = month_prime < 10u ? month_prime + 3u : month_prime - 9u;
    year += month <= 2u;

    out->year = (u16)year;
    out->month = (u8)month;
    out->day = (u8)day;
    out->hour = (u8)(day_seconds / 3600u);
    out->minute = (u8)((day_seconds / 60u) % 60u);
    out->second = (u8)(day_seconds % 60u);
    out->valid = true;
}

bool rtc_epoch_to_datetime(u64 epoch, rtc_datetime_t* out) {
    if (!out) return false;
    epoch_to_calendar(epoch, out);
    if (out->year < 1970u || out->year > 2399u) {
        out->valid = false;
        return false;
    }
    return true;
}

bool rtc_init(void) {
    wall_clock_valid = false;
    rtc_sample_t previous, current;
    bool stable = false;

    for (u32 attempt = 0; attempt < 8u; attempt++) {
        if (!sample_cmos(&previous) || !sample_cmos(&current)) continue;
        if (samples_equal(&previous, &current)) {
            stable = true;
            break;
        }
    }

    rtc_datetime_t decoded;
    if (!stable ||
        !rtc_decode_registers(current.second, current.minute, current.hour,
                              current.day, current.month, current.year,
                              current.century, current.status_b, &decoded)) {
        serial_puts("RTC: unavailable or invalid CMOS clock; wall clock disabled");
        return false;
    }

    wall_clock_epoch = calendar_to_epoch(&decoded);
    wall_clock_monotonic_ms = timer_get_ms();
    wall_clock_valid = true;
    serial_puts("RTC: stable CMOS clock accepted; PIT maintains monotonic wall time");
    return true;
}

bool rtc_available(void) {
    return wall_clock_valid;
}

bool rtc_get_datetime(rtc_datetime_t* out) {
    if (!out || !wall_clock_valid) return false;
    u64 monotonic_now = timer_get_ms();
    if (monotonic_now < wall_clock_monotonic_ms) return false;
    u64 elapsed_seconds = (monotonic_now - wall_clock_monotonic_ms) / 1000u;
    return rtc_epoch_to_datetime(wall_clock_epoch + elapsed_seconds, out);
}
