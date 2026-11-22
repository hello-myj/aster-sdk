# 发布开发包

`contract/lock.json` 标识本次源码使用的公开 ABI，`contract/release-family.json` 固定已发布的开发包及配套组件。源代码当前使用 4.5.0，已发布的 `sdk-v4.3.0` 仍配套 ABI 4.3 的固件与模拟器。打新的发行包前须确定实际发布的组件和摘要。[开发包配套关系](development-kit.md)

发布配置位于 `contract/release-family.json`。组件使用固定 Release URL、提交和 SHA256；本地开发包不需要固件源码。

先发布配套固件镜像和桌面模拟器，再更新组件引用。使用 Python 3.12 在 Linux 构建两种主机包：

```bash
bash release.sh --hosts linux-x64,windows-x64
```

编译器配置由 `toolchains/catalog.json` 固定，包含 Xtensa 和 RISC-V RV32 ILP32F 架构配置。Windows 编译器依赖需提前放入配置所指定的缓存目录。输出在 `dist/portable/`。

Linux 包会使用包内工具创建独立工程，逐一编译并校验各 CPU 的 AOT。Windows 包解压后验证：

```powershell
.\runtime\python.exe .\tools\verify_windows_release.py .
```

发布附件清单包含文件大小、SHA256 与固定下载 URL。发布任务先校验文件，再创建 Release。已有同名 Release 不会覆盖。
