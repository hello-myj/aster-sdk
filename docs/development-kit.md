# 完整开发套件

从 SDK Release 下载本机平台的包。包内有 APP SDK、手册、示例和 `xtensa` 工具链。当前 ABI 为 4.0.0。

固件镜像和模拟器只在仓库里有 `contract/release-family.json` 时打进同一个包。没有这份清单时，发布包只有 SDK。

## 使用

解压后执行 `aster install`，再激活 `env.sh` 或 `env.ps1`。Linux 需要 Python 3.12。

```text
aster init my-app --template hello --id com.example.hello --target xtensa
aster build my-app --aot
aster run my-app --port <串口>
```

模拟器执行包里的 WASM。立创竖屏用 `--resolution 240x320`，微雪竖屏用 `--resolution 320x480`。

设备固件和 SD 字体从固件仓库取得。两款板的镜像分开烧录：

```text
aster flash <镜像> --port <串口>
```

## 维护

要连同固件和模拟器发布时，先放好 `contract/release-family.json`，ABI 必须和 `contract/lock.json` 一致。打包只校验已有产物，不编译固件。

```bash
bash release.sh --hosts linux-x64,windows-x64 --development-kit-dir /path/to/pinned-payloads
```
