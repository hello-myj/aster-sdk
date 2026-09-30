# Wi-Fi API

Wi-Fi station calls are available in ABI 3.5 when the device reports
`ASTER_FEATURE_WIFI`. The app manifest must declare both:

```toml
requires = ["wifi"]
permissions = ["wifi"]
```

Scan, connect, disconnect, reconnect and forget calls enqueue one operation
and return immediately. An operation already in progress returns
`ASTER_EBUSY`. Poll `aster_wifi_status()` for completion; its `operation`
field is nonzero while work is running, then `last_operation` and
`last_result` report the result.

```c
aster_wifi_ap_info_t aps[ASTER_WIFI_AP_MAX];
aster_wifi_info_t wifi = {
    .size = sizeof(wifi),
    .version = 1,
};

if (aster_system_has_feature(ASTER_FEATURE_WIFI) > 0) {
    (void)aster_wifi_scan_start();
    /* In the app event loop: call later, not in a tight loop. */
    int count = aster_wifi_scan_results(aps, ASTER_WIFI_AP_MAX);
    if (count == ASTER_EAGAIN) {
        /* scan is still running */
    } else if (count >= 0) {
        /* `count` AP records, strongest RSSI first */
    }

    (void)aster_wifi_status(&wifi, sizeof(wifi));
}
```

To join, pass string pointers and their byte lengths. A zero password length
selects an open network; personal-network passwords must be 8 to 64 bytes.
Connection success is reported asynchronously in `last_result`.

```c
const char *ssid = "home-wifi";
const char *password = "example-passphrase";
int err = aster_wifi_connect(ssid, strlen(ssid), password,
                             strlen(password));
```

The device keeps one most recently successful profile, retries it after boot
when the SD card mounts, and exposes `aster_wifi_reconnect()`,
`aster_wifi_get_saved_ssid()` and `aster_wifi_forget()`. The quick-settings
panel can reconnect or disconnect that profile; `hw wifi scan/connect/status`
are available in the firmware shell.

The saved profile lives in the system KV under the SD card's system directory.
Its passphrase is not encrypted, so anyone who can read the card can read it.
The current station implementation supports 2.4 GHz open and personal
networks. Enterprise authentication, WEP, SoftAP, IPv6, and general TCP/UDP
socket imports are outside this API.
