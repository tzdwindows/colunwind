#ifndef COLUNWIND_H
#define COLUNWIND_H

#include "colunwind/export.h"
#include "colunwind/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化配置对象并填充默认配置
 * @param config 配置结构体指针
 * @return 状态码
 */
COLUNWIND_API colunwind_status_t colunwind_config_init(colunwind_config_t* config);

/**
 * @brief 初始化 colunwind 核心运行时
 * 预分配固定 Arena 内存池、初始化符号引擎及备用栈 (POSIX)
 * @param config 用户配置 (为 NULL 时采用默认配置)
 * @return 状态码
 */
COLUNWIND_API colunwind_status_t colunwind_init(const colunwind_config_t* config);

/**
 * @brief 安装系统崩溃捕获拦截器
 * Windows: 注册 VEH (VectoredExceptionHandler) 与 UnhandledExceptionFilter
 * POSIX: 注册 sigaction 并挂载 sigaltstack
 * @return 状态码
 */
COLUNWIND_API colunwind_status_t colunwind_install_crash_handler(void);

/**
 * @brief 卸载崩溃拦截器并恢复原始系统处理器
 * @return 状态码
 */
COLUNWIND_API colunwind_status_t colunwind_uninstall_crash_handler(void);

/**
 * @brief 释放运行时分配的所有资源并反初始化
 */
COLUNWIND_API void colunwind_shutdown(void);

/**
 * @brief 捕获当前调用点栈回溯 (无锁、安全)
 * @param trace 接收栈回溯结果的结构体
 * @param skip_frames 跳过的帧数 (自身通常设为 0 或 1)
 * @return 状态码
 */
COLUNWIND_API colunwind_status_t colunwind_backtrace_capture(colunwind_backtrace_t* trace, uint32_t skip_frames);

/**
 * @brief 基于指定的原生执行上下文进行栈展开
 * @param trace 接收栈回溯结果的结构体
 * @param native_context 平台原生上下文指针 (Win32 CONTEXT* / POSIX ucontext_t*)
 * @param skip_frames 跳过的帧数
 * @return 状态码
 */
COLUNWIND_API colunwind_status_t colunwind_backtrace_capture_from_context(colunwind_backtrace_t* trace, const void* native_context, uint32_t skip_frames);

/**
 * @brief 对已捕获的栈帧进行批量符号化解析 (填充模块、符号名、文件路径、行号与列号)
 * @param trace 待符号化的栈轨迹
 * @return 状态码
 */
COLUNWIND_API colunwind_status_t colunwind_symbolize(colunwind_backtrace_t* trace);

/**
 * @brief 解析单个指令地址的源码位置与符号信息 (精确到行列号)
 * @param address 目标指令地址
 * @param out_frame 输出解析出的单帧信息
 * @return 状态码
 */
COLUNWIND_API colunwind_status_t colunwind_resolve_location(uintptr_t address, colunwind_frame_t* out_frame);

/**
 * @brief 生成转储文件 (Windows: Minidump (.dmp); POSIX: 紧凑崩溃转储文件)
 * @param dump_path 输出转储文件路径 (若为 NULL，则在配置目录按命名规则自动生成)
 * @param crash_ctx 崩溃上下文 (可为 NULL，则生成当前进程快照)
 * @param dump_flags 转储标志位组合
 * @return 状态码
 */
COLUNWIND_API colunwind_status_t colunwind_write_dump(const char* dump_path, const colunwind_crash_context_t* crash_ctx, uint32_t dump_flags);

/**
 * @brief 格式化输出栈回溯到标准错误 (遵循 Async-Signal-Safe，无锁、无动态内存、底层系统调用输出)
 * @param trace 待输出的栈回溯数据
 */
COLUNWIND_API void colunwind_print_backtrace(const colunwind_backtrace_t* trace);

/**
 * @brief 将崩溃类型枚举转换为可读文本
 * @param type 崩溃类型
 * @return 描述字符串
 */
COLUNWIND_API const char* colunwind_crash_type_to_string(colunwind_crash_type_t type);

/**
 * @brief 将状态码转换为错误描述文本
 * @param status 状态码
 * @return 描述字符串
 */
COLUNWIND_API const char* colunwind_status_to_string(colunwind_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* COLUNWIND_H */
