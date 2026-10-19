# 发布 Windows 和 Linux 包

在 Ubuntu x86_64 上一次打出两个主机包。Windows 包是便携目录 ZIP，带 `aster.exe`、`aster-toolchain.exe`、`aster-device.exe`。Linux 包是 tar.gz，运行需要 Python 3.12。当前 ABI 为 4.0.0，CPU 目标是 `xtensa`。

## 准备

```bash
python3.12 -m venv .venv-linux
.venv-linux/bin/python -m pip install -r requirements.txt
```

Windows 编译器使用固定 ZIP，不要把 Linux 的 `wamrc` 当作 Windows 程序。已有缓存时可以导出：

```bash
.venv-linux/bin/python tools/toolchain_bundle.py \
  --target xtensa \
  --output dist/toolchains/windows-x64-esp32s3-wamr-2.4.3-v6.zip
```

## 打包

```bash
bash release.sh \
  --hosts linux-x64,windows-x64 \
  --windows-toolchain-bundle /path/to/windows-x64-esp32s3-wamr-2.4.3-v6.zip
```

契约已经在仓库里时省略 `--contract`。本机已有 Windows 编译器缓存时可以省略 `--windows-toolchain-bundle`。输出在 `dist/portable/`，每个主机一个包。

Linux 包生成前会用包内工具编译并检查一个 `xtensa` 应用。Windows 包在 Windows 上检查：

```powershell
.\runtime\python.exe .\tools\verify_windows_release.py .
```

## 检查文档

```bash
python tools/check_docs.py
```

打包前也会检查 README、手册、CLI 文档和示例里的 `--target` 与当前 ABI。固件仓库用同名脚本检查架构文档和硬件清单。

三仓库集成：

```bash
python tools/release_check.py --sdk ../aster-sdk --sim ../aster-sim --require-clean --output build/release-check
```

该命令在固件仓库执行，不刷机，也不创建 Release。

完整套件把 SDK、固件镜像、SD 字体和模拟器打在一起。配套版本写在 `contract/release-family.json`。用法见 [development-kit.md](development-kit.md)。
