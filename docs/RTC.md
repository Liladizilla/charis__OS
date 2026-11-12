# CharisOS RTC and wall-clock service

## Current behavior

At boot, the kernel samples the CMOS RTC only after the PIT monotonic timer is initialized. It waits for the RTC update-in-progress bit to clear, checks the battery-backed valid-time bit, reads two complete snapshots and accepts the clock only when both snapshots agree.

The decoder supports the RTC binary/BCD mode and both 12-hour and 24-hour formats. It validates month lengths and Gregorian leap years, including century leap-year rules, before converting the sample to Unix epoch seconds.

After initialization, the kernel does not repeatedly access CMOS registers. It stores the accepted epoch and the monotonic PIT millisecond count at boot. `rtc_get_datetime()` advances the epoch using elapsed monotonic time. The desktop taskbar refreshes the clock once per second by repainting the taskbar alone, not the whole desktop.

When RTC data is missing, invalid, or unstable, the clock service remains unavailable. The taskbar reports `RTC UNAVAILABLE`; it does not label uptime as date/time or invent a timestamp.

## Timezone and synchronization limits

The CMOS value is displayed as firmware-provided time. CharisOS does not yet know whether firmware configured the hardware clock as UTC or local time. Timezone configuration, daylight-saving transitions, UTC normalization, NTP/PTP synchronization, drift correction and runtime clock-setting are not implemented.

PIT elapsed time is monotonic only while the current boot remains active. It does not survive reboot and is not a substitute for a hardware RTC or synchronized wall clock.

## Validation

`tests/rtc_decode_test.c` exercises BCD/binary decoding, 12/24-hour conversion, century fallback, malformed register values, invalid calendar dates, leap-day handling and epoch-to-calendar conversion. CI builds this host test with warnings treated as errors.

The host test does not emulate CMOS I/O or prove that a physical motherboard's RTC is configured correctly. QEMU boot capture and real hardware validation remain required.
