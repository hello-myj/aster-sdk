# 完整开发套件

从 SDK Release 下载本机平台的包。包内有：

- APP SDK、手册、示例和 `xtensa` 工具链
- `firmware/`：立创与微雪的固件镜像和 SD 字体
- `simulator/`：便携模拟器
- `development-kit.json`：这一组 SDK、固件、模拟器的版本和哈希

当前源码 ABI 为 4.0.0。已下载包以包内 JSON 的 ABI 为准，并与设备固件一致。

## 使用

解压后执行 `aster install`，再激活 `env.sh` 或 `env.ps1`。Linux 需要 Python 3.12。

```text
aster init my-app --template hello --id com.example.hello --target xtensa
aster build my-app --aot
aster run my-app --port <串口>
```

模拟器执行包里的 WASM。立创竖屏用 `--resolution 240x320`，微雪竖屏用 `--resolution 320x480`。

把 `firmware/sdcard/` 复制到 FAT SD 卡根目录。两款板的镜像分开烧录：

```text
aster flash firmware/<对应板的镜像> --port <串口>
```

## 维护

固件和模拟器各自打标签。SDK 用 `contract/release-family.json` 固定配套成品。打包只校验已有产物，不编译固件。

```bash
bash release.sh --hosts linux-x64,windows-x64 --development-kit-dir /path/to/pinned-payloads
```
