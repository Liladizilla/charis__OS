#include <assert.h>
#include <stdio.h>
#include <kernel/rtc.h>

static void expect_time(u8 sec,u8 min,u8 hour,u8 day,u8 month,u8 year,
                        u8 century,u8 mode,u16 ey,u8 em,u8 ed,
                        u8 eh,u8 emin,u8 es) {
    rtc_datetime_t out = {0};
    assert(rtc_decode_registers(sec,min,hour,day,month,year,century,mode,&out));
    assert(out.valid);
    assert(out.year == ey && out.month == em && out.day == ed);
    assert(out.hour == eh && out.minute == emin && out.second == es);
}

int main(void) {
    /* BCD, 24-hour time, explicit century. */
    expect_time(0x58,0x59,0x23,0x31,0x12,0x26,0x20,0x02,
                2026,12,31,23,59,58);

    /* BCD, 12-hour midnight and noon conversion. */
    expect_time(0x00,0x07,0x12,0x01,0x01,0x26,0x20,0x00,
                2026,1,1,0,7,0);
    expect_time(0x01,0x07,0x92,0x01,0x01,0x26,0x20,0x00,
                2026,1,1,12,7,1);

    /* Binary, 24-hour time, no century register. */
    expect_time(59,30,18,29,2,24,0,0x06,
                2024,2,29,18,30,59);

    /* Century fallback follows the documented 1970..2069 pivot. */
    expect_time(0x00,0x00,0x00,0x01,0x01,0x69,0,0x02,
                2069,1,1,0,0,0);
    expect_time(0x00,0x00,0x00,0x01,0x01,0x70,0,0x02,
                1970,1,1,0,0,0);

    rtc_datetime_t invalid = {0};
    assert(!rtc_decode_registers(0,0,0x24,0x01,0x01,0x26,0x20,0x02,&invalid));
    assert(!invalid.valid);
    assert(!rtc_decode_registers(0,0,0,0x29,0x02,0x23,0x20,0x02,&invalid));
    assert(!rtc_decode_registers(0x6A,0,0,1,1,0x26,0x20,0x02,&invalid));
    assert(!rtc_decode_registers(0,0,0,1,1,0x26,0x18,0x02,&invalid));

    puts("rtc_decode_test: PASS");
    return 0;
}
