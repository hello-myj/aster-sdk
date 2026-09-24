# Aster APP toolchain

Linux x86_64 自带 wasi-sdk 24 (clang 18)、WAMR 2.4.3 的 wamrc 和 Binaryen。
工具链作为本 SDK 的独立副本提供，不访问固件或原 mjx 目录。

选择顺序：`ASTER_WASI_SDK` / `ASTER_WAMRC` → 本目录 → 系统工具。
其他开发机需要安装对应主机版本并设置上述环境变量；本次仅验证 Linux x86_64。
生成 AOT 必须使用与 `contract/api.json` 匹配的 WAMR 版本及目标后端。
显式请求 AOT 而缺少 wamrc 会失败；使用 `--no-aot` 可构建解释执行包。

wasi-sdk 仅保留 freestanding 编译需要的 clang、链接器和 builtins，未包含完整 WASI sysroot。
工具链与运行时使用各自上游许可证，见本目录 LICENSE 及仓库 NOTICE.md。
