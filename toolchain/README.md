# Aster APP toolchain

Linux x86_64 自带 wasi-sdk 24 (clang 18)、WAMR 2.4.3 的 wamrc 和 Binaryen。
工具链作为本 SDK 的独立副本提供，不访问固件或原 mjx 目录。

选择顺序：`ASTER_WASI_SDK` / `ASTER_WAMRC` → 本目录 → 系统工具。
Windows x86-64 发行包通过 `tools/package_windows.ps1` 构建：下载官方 wasi-sdk 24
Windows 版本，并从 WAMR 2.4.3 和 Espressif LLVM 18 固定提交构建支持 Xtensa 的 Windows `wamrc.exe`，封装为独立的
`aster-toolchain.exe`。打包程序内的资源从 PyInstaller 解包目录读取，构建产物
写入当前工作目录（或 `ASTER_WORKSPACE` 指定目录）的 `build/`、`dist/`。
Windows 端需要在真实 Windows 主机或 Windows CI 上执行打包与 AOT 冒烟测试；
Linux 的 PyInstaller 不能生成 Windows 可执行文件。
生成 AOT 必须使用与 `contract/api.json` 匹配的 WAMR 版本及目标后端。
显式请求 AOT 而缺少 wamrc 会失败；使用 `--no-aot` 可构建解释执行包。

wasi-sdk 的 Linux 与 Windows 副本都只保留 freestanding 编译需要的 clang、
链接器、归档工具与 Clang 资源目录，未包含完整 WASI sysroot。
工具链与运行时使用各自上游许可证，见本目录 LICENSE 及仓库 NOTICE.md。
