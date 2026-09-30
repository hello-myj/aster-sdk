# Wall clock

`aster_time_get()` reads the device wall clock. It does not start a network
request and does not require an app permission. SNTP runs inside the firmware;
the SDK exposes only the resulting time and whether the device has synchronized.

```c
#include <aster/aster.h>

aster_time_info_t now = { .size = sizeof(now), .version = 1 };
if (aster_time_get(&now, sizeof(now)) == ASTER_OK) {
    /* UTC Unix time in milliseconds. */
    uint64_t timestamp_ms = now.unix_ms;

    /* Calendar fields use the board's configured local UTC offset. */
    if (now.flags & ASTER_TIME_FLAG_SYNCED) {
        /* SNTP has set the clock during this boot. */
    } else {
        /* These fields are the firmware's configured fallback clock. */
    }
}
```

The LCKFB S3 firmware retries SNTP in the background after Wi-Fi receives an
IPv4 address. Its local calendar currently uses UTC+8. Until the first
successful sync, it reports the configured default of 2026-01-01 00:00:00
local time with `ASTER_TIME_FLAG_SYNCED` clear. The Unix timestamp remains UTC
in both states.

`aster_time_now_ms()` remains the monotonic app runtime clock. Use it for
durations, animation, and deadlines; use `aster_time_get()` for calendar time
and timestamps that need to match real-world time.
