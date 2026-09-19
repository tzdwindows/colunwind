# colunwind

`colunwind` 是一个纯 C 语言编写（遵循 C99/C11 标准）、轻量级、跨平台且具备 **Async-Signal-Safe** 保障的统一崩溃捕获、栈回溯、Minidump 转存及精确到**行列号**的符号化解析库。

---

## 核心特性 (Features)

1. **Async-Signal-Safe 保证**：
   - 崩溃处理过程严格禁止调用可能引发死锁的 `malloc`、`free`、`printf` 及 C++ iostreams。
   - 所有运行时临时上下文均运行在启动期预分配的固定内存池（Arena Buffer）上。
   - 终端输出与转储落盘直接通过操作系统底层系统调用（POSIX `write`，Windows `WriteFile`）完成。

2. **跨平台多分层架构**：
   - **Windows**：接入 Vectored Exception Handler (VEH) 与 `SetUnhandledExceptionFilter`，基于 `RtlVirtualUnwind` 实现 64 位无锁栈回溯，基于 `DbgHelp` / PDB 提供源码行列号映射与 Minidump (`MiniDumpWriteDump`) 生成。
   - **POSIX (Linux/macOS)**：接入 `sigaction`，强制挂载 `sigaltstack` 备用信号栈防止栈溢出死锁，提供轻量紧凑型内存转储文件生成。

3. **高精度行列号定位规范**：
   - 暴露结构体显式包含 `const char* file`、`uint32_t line`、`uint32_t column` 以及 `const char* function`。
   - 结合符号表与源码映射引擎，自动推算符号在源码行内的 1-based 精确列号。

4. **规范设计与严谨构建**：
   - 全局函数与暴露结构体一律以 `colunwind_` 前缀命名，宏定义一律使用 `COLUNWIND_` 前缀。
   - 纯模块化构建：核心库支持静态库 (`colunwind.lib` / `libcolunwind.a`) 与动态库 (`colunwind.dll` / `libcolunwind.so`)。
   - 测试用例与示例独立编译，严格链接库文件，严禁源码混淆。

---

## 目录结构 (Directory Structure)

```text
colunwind/
├── .gitignore
├── CMakeLists.txt
├── README.md
├── include/
│   └── colunwind/
│       ├── colunwind.h          # 统一公共暴露 API
│       ├── types.h              # 上下文、帧信息、行列号数据结构定义
│       └── export.h             # 动态库导出宏定义
├── src/
│   ├── arch/                    # 架构相关代码 (x86_64, aarch64 等)
│   ├── os/                      # 操作系统隔离层 (windows, posix)
│   ├── capture.c                # 信号/异常捕获与接管
│   ├── unwind.c                 # 栈展开逻辑
│   ├── symbolizer.c             # 行号/列号符号化映射引擎 (DWARF / PDB)
│   ├── dumper.c                 # Minidump / 紧凑内存转储生成
│   └── internal.h               # 内部工具函数 (内存 Arena, 基础格式化)
├── test/                        # 【重要】所有测试文件必须且只能放在此目录下
│   ├── CMakeLists.txt
│   ├── test_crash_handler.c     # 验证异常/信号拦截
│   ├── test_unwind.c            # 验证无锁安全栈展开
│   ├── test_line_column.c       # 验证行号与列号定位准确性
│   └── test_dump_writer.c       # 验证 Minidump 文件有效性
└── examples/
    └── simple_app.c             # 集成演示示例
```

---

## 构建与运行 (Build & Run)

### 依赖环境
- CMake >= 3.20
- Visual Studio 2026/v18 工具链 (或 GCC 9+ / Clang 11+)

### 1. 配置与构建 (使用 CMake)

```powershell
# 创建构建目录
cmake -B build -G "Visual Studio 18 2026" -A x64 -DCOLUNWIND_BUILD_SHARED=OFF -DBUILD_TESTING=ON

# 编译项目 (Debug 或 Release)
cmake --build build --config Debug
```

若需编译为动态库 (DLL)，请指定 `-DCOLUNWIND_BUILD_SHARED=ON`。

### 2. 运行测试集 (CTest)

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

测试集包括：
- `test_crash_handler`: 验证 VEH 异常拦截及回调触发。
- `test_unwind`: 验证无锁安全栈展开及多层调用链捕获。
- `test_line_column`: 验证文件名、函数名、行号与列号的精确解析。
- `test_dump_writer`: 验证 Minidump 文件的落盘与 `'MDMP'` 头部魔数有效性。

---

## 快速使用范例 (Quick Start)

```c
#include <colunwind/colunwind.h>
#include <stdio.h>

static void my_crash_handler(const colunwind_crash_context_t* ctx, void* user_data) {
    // Async-signal-safe 回调
    colunwind_print_backtrace(&ctx->backtrace);
}

int main(void) {
    colunwind_config_t config;
    colunwind_config_init(&config);
    config.on_crash = my_crash_handler;
    config.enable_auto_dump = true;

    // 1. 初始化运行时
    colunwind_init(&config);

    // 2. 安装崩溃拦截器
    colunwind_install_crash_handler();

    // 3. 手动捕获当前调用栈
    colunwind_backtrace_t trace;
    if (colunwind_backtrace_capture(&trace, 0) == COLUNWIND_SUCCESS) {
        colunwind_symbolize(&trace);
        colunwind_print_backtrace(&trace);
    }

    // 4. 清理与释放
    colunwind_uninstall_crash_handler();
    colunwind_shutdown();
    return 0;
}
```
