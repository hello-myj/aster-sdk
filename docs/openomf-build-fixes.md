# OpenOMF 移植所需工具修复

2026-10-25 当前状态：本地工作树另含已获授权的ABI4.2内存音效扩展，见continuous-audio.md；下文“未修改ABI4.1”仅指早期工具修复，不代表当前工作树仍是纯4.1。

CRT的memcpy/memmove/memset在编译目标定义__wasm_bulk_memory__时改用编译器内建操作，生成memory.copy/fill；其他目标保留原字节循环。APP需同时传递-mbulk-memory编译/链接参数。WAMR保留源/目标完整范围检查，重叠复制由memmove语义处理；没有增加env libc导入，也不放宽包校验。OpenOMF0.2.13真实模拟器360帧Exit0，并在微雪板实际原版DEMO中运行150秒结束RUNNING。此项独立于内存音效的新公开接口。

基于SDK源码68d14e2，在feature/openomf-audio本地分支同步原安装版已经应用的工具修复。未修改ABI4.1、导入白名单或发布契约，也没有覆盖上游的Windows编译器诊断解码修复。

- build.py将[build].ldflags加入目标文件之后的链接参数；编译和链接以APP目录为工作目录，支持工程相对路径。
- 目标对象文件包含源根编号及相对路径，解决OpenOMF多个子目录中同名C文件相互覆盖；源根外使用绝对路径摘要。
- verify_ast.py正确读取WASM的LEB128导出函数编号、导入limits、字符串和章节边界，避免大型模块编号超过127后的错位；能力、目标、签名与ABI校验保留。

实际构建：OpenOMF0.2.0的177个源文件，WASM524879B、Xtensa AOT1802820B、AST2335280B；31个公开Aster导入，无SDL/WASI Host导入，通过包校验。此验证是构建与包校验，不代表板上运行。

源码构建可复用独立安装SDK的工具链，设置ASTER_WASI_SDK到wasi-sdk目录、ASTER_WAMRC到wamrc.exe，然后使用tools/build.py APP路径 --target xtensa --aot。默认输出在本源码检出的dist/xtensa目录；保留APP原有输出，避免替换正在试玩的包。
