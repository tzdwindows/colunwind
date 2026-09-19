#ifndef COLUNWIND_TYPES_H
#define COLUNWIND_TYPES_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COLUNWIND_MAX_FRAMES     128
#define COLUNWIND_MAX_NAME_LEN   256
#define COLUNWIND_MAX_PATH_LEN   512

/**
 * @brief colunwind 状态码定义
 */
typedef enum colunwind_status {
    COLUNWIND_SUCCESS                       =  0,
    COLUNWIND_ERROR_INVALID_ARGUMENT       = -1,
    COLUNWIND_ERROR_OUT_OF_MEMORY          = -2,
    COLUNWIND_ERROR_NOT_INITIALIZED        = -3,
    COLUNWIND_ERROR_ALREADY_INITIALIZED    = -4,
    COLUNWIND_ERROR_UNWIND_FAILED          = -5,
    COLUNWIND_ERROR_SYMBOL_NOT_FOUND       = -6,
    COLUNWIND_ERROR_DUMP_FAILED            = -7,
    COLUNWIND_ERROR_PLATFORM_NOT_SUPPORTED = -8,
    COLUNWIND_ERROR_IO_FAILED              = -9
} colunwind_status_t;

/**
 * @brief 崩溃类型分类定义
 */
typedef enum colunwind_crash_type {
    COLUNWIND_CRASH_NONE = 0,
    COLUNWIND_CRASH_ACCESS_VIOLATION,      /* 段错误 / 非法内存访问 (SEGV / 0xC0000005) */
    COLUNWIND_CRASH_ILLEGAL_INSTRUCTION,   /* 非法指令 (ILL / 0xC000001D) */
    COLUNWIND_CRASH_STACK_OVERFLOW,        /* 栈溢出 (0xC00000FD / altstack SEGV) */
    COLUNWIND_CRASH_INTEGER_DIV_BY_ZERO,   /* 算术除零 (FPE / 0xC0000094) */
    COLUNWIND_CRASH_ABORT,                 /* 中止信号 (SIGABRT / abort()) */
    COLUNWIND_CRASH_TRAP,                  /* 陷阱/断点 (SIGTRAP / EXCEPTION_BREAKPOINT) */
    COLUNWIND_CRASH_UNKNOWN                /* 其他未识别异常 */
} colunwind_crash_type_t;

/**
 * @brief 源码行列号精确位置信息 (符合精度规范)
 */
typedef struct colunwind_source_location {
    const char* file;        /**< 源码文件路径/名称 */
    uint32_t    line;        /**< 行号 (1-based, 0 为未知) */
    uint32_t    column;      /**< 列号 (1-based, 0 为未知) */
    const char* function;    /**< 函数符号名称 */
} colunwind_source_location_t;

/**
 * @brief 单个栈帧详细信息 (符合精度规范)
 */
typedef struct colunwind_frame {
    uintptr_t                   instruction_pointer;                   /**< 指令指针 (PC / IP) */
    uintptr_t                   stack_pointer;                         /**< 栈指针 (SP) */
    uintptr_t                   frame_pointer;                         /**< 帧基址指针 (FP / BP) */
    uintptr_t                   module_base;                           /**< 模块基地址 */
    char                        module_name[COLUNWIND_MAX_NAME_LEN];   /**< 所在模块名 */
    char                        symbol_name[COLUNWIND_MAX_NAME_LEN];   /**< 符号名 */
    char                        file_path[COLUNWIND_MAX_PATH_LEN];     /**< 源码绝对路径 */
    const char*                 file;                                  /**< 指向有效文件名缓冲 (显式包含) */
    uint32_t                    line;                                  /**< 1-based 行号 (显式包含) */
    uint32_t                    column;                                /**< 1-based 列号 (显式包含) */
    const char*                 function;                              /**< 指向有效函数名缓冲 (显式包含) */
    uintptr_t                   offset;                                /**< 相对函数符号起始地址偏移 */
} colunwind_frame_t;

/**
 * @brief 栈回溯轨迹
 */
typedef struct colunwind_backtrace {
    colunwind_frame_t frames[COLUNWIND_MAX_FRAMES];
    uint32_t          frame_count;
} colunwind_backtrace_t;

/**
 * @brief 崩溃详细上下文
 */
typedef struct colunwind_crash_context {
    colunwind_crash_type_t crash_type;      /**< 崩溃类型 */
    uintptr_t              fault_address;   /**< 出错指令或故障内存地址 */
    uint64_t               thread_id;       /**< 崩溃发生线程 ID */
    void*                  native_context;  /**< 原生系统上下文 (EXCEPTION_POINTERS* 或 ucontext_t*) */
    colunwind_backtrace_t  backtrace;       /**< 崩溃时的栈回溯 */
} colunwind_crash_context_t;

/**
 * @brief 崩溃回调函数签名 (注意：回调处于崩溃上下文，严禁非 Async-Signal-Safe 操作)
 */
typedef void (*colunwind_crash_callback_t)(const colunwind_crash_context_t* context, void* user_data);

/**
 * @brief 转储生成标志位
 */
typedef enum colunwind_dump_flags {
    COLUNWIND_DUMP_NORMAL            = 0x00,
    COLUNWIND_DUMP_WITH_FULL_MEMORY  = 0x01,
    COLUNWIND_DUMP_WITH_THREAD_INFO  = 0x02,
    COLUNWIND_DUMP_WITH_UNWIND_TRACE = 0x04
} colunwind_dump_flags_t;

/**
 * @brief colunwind 全局配置
 */
typedef struct colunwind_config {
    colunwind_crash_callback_t on_crash;          /**< 崩溃发生时的用户回调 */
    void*                      user_data;         /**< 传递给回调的用户自定义数据 */
    const char*                dump_directory;    /**< 转储文件输出目录 (若 NULL 则为当前路径) */
    bool                       enable_auto_dump;  /**< 崩溃时是否自动生成转储文件 */
    bool                       enable_altstack;   /**< POSIX: 是否为信号处理程序启用备用栈 */
    size_t                     altstack_size;     /**< 备用栈大小 (默认 64KB) */
    size_t                     arena_size;        /**< 预分配固定内存池 Arena 大小 (默认 128KB) */
} colunwind_config_t;

#ifdef __cplusplus
}
#endif

#endif /* COLUNWIND_TYPES_H */
