# Wi-Fi

设备报告 `ASTER_FEATURE_WIFI` 时可以使用 station 接口。manifest：

```toml
requires = ["wifi"]
permissions = ["wifi"]
```

扫描、连接、断开、重连和忘记网络都是提交一次操作后立即返回。已有操作在进行时返回 `ASTER_EBUSY`。用 `aster_wifi_status()` 看结果：`operation` 非 0 表示还在进行，结束后看 `last_operation` 和 `last_result`。

```c
aster_wifi_ap_info_t aps[ASTER_WIFI_AP_MAX];
aster_wifi_info_t wifi = { .size = sizeof(wifi), .version = 1 };

if (aster_system_has_feature(ASTER_FEATURE_WIFI) > 0) {
    aster_wifi_scan_start();
    int count = aster_wifi_scan_results(aps, ASTER_WIFI_AP_MAX);
    aster_wifi_status(&wifi, sizeof(wifi));
}
```

`count == ASTER_EAGAIN` 时扫描还没结束。连接时传入 SSID、密码和各自的字节长度。密码长度为 0 表示开放网络；个人网络密码为 8 到 64 字节。

```c
const char *ssid = "home-wifi";
const char *password = "example-passphrase";
aster_wifi_connect(ssid, strlen(ssid), password, strlen(password));
```

设备保存最近一次成功的配置，SD 挂载后自动重连。相关函数是 `aster_wifi_reconnect()`、`aster_wifi_get_saved_ssid()` 和 `aster_wifi_forget()`。密码以明文存在系统 KV 里。

当前支持 2.4 GHz 的开放网络和个人网络。TCP/UDP 使用 `net` 权限，见 [network.md](network.md)。
