#include "colunwind/colunwind.h"
#include "internal.h"
#include "os/os.h"
#include "arch/arch.h"

#include <stdarg.h>

colunwind_runtime_t g_colunwind_runtime = {0};

/* ========================================================================= */
/* 格式化与底层 I/O 实现 (Async-Signal-Safe)                                 */
/* ========================================================================= */

void colunwind_raw_write_stderr(const char* data, size_t len) {
    colunwind_os_write_stderr(data, len);
}

void colunwind_raw_write_string_stderr(const char* str) {
    if (!str) return;
    colunwind_os_write_stderr(str, colunwind_safe_strlen(str));
}

intptr_t colunwind_raw_open_file_write(const char* path) {
    return colunwind_os_open_write(path);
}

void colunwind_raw_close_file(intptr_t fd) {
    colunwind_os_close(fd);
}

bool colunwind_raw_write_file(intptr_t fd, const void* data, size_t len) {
    return colunwind_os_write(fd, data, len);
}

size_t colunwind_safe_vsnprintf(char* buf, size_t size, const char* fmt, va_list args) {
    if (!buf || size == 0) return 0;
    if (!fmt) {
        buf[0] = '\0';
        return 0;
    }

    size_t out_idx = 0;
    char num_buf[32];

    for (size_t i = 0; fmt[i] != '\0' && out_idx + 1 < size; ++i) {
        if (fmt[i] != '%') {
            buf[out_idx++] = fmt[i];
            continue;
        }

        i++;
        if (fmt[i] == '\0') break;

        /* 解析标志与宽度 (简单支持 %02u, %04u, %016llx 等) */
        int min_width = 0;
        bool pad_zero = false;
        if (fmt[i] == '0') {
            pad_zero = true;
            i++;
        }
        while (fmt[i] >= '0' && fmt[i] <= '9') {
            min_width = min_width * 10 + (fmt[i] - '0');
            i++;
        }

        /* 长度修饰符 */
        bool is_long_long = false;
        if (fmt[i] == 'l') {
            i++;
            if (fmt[i] == 'l') {
                is_long_long = true;
                i++;
            }
        } else if (fmt[i] == 'z') {
            is_long_long = (sizeof(size_t) == 8);
            i++;
        }

        switch (fmt[i]) {
            case '%':
                buf[out_idx++] = '%';
                break;
            case 's': {
                const char* s = va_arg(args, const char*);
                if (!s) s = "(null)";
                while (*s && out_idx + 1 < size) {
                    buf[out_idx++] = *s++;
                }
                break;
            }
            case 'c': {
                char c = (char)va_arg(args, int);
                buf[out_idx++] = c;
                break;
            }
            case 'd': {
                int64_t val = is_long_long ? va_arg(args, int64_t) : (int64_t)va_arg(args, int);
                if (val < 0) {
                    if (out_idx + 1 < size) buf[out_idx++] = '-';
                    val = -val;
                }
                colunwind_safe_u64_to_dec((uint64_t)val, num_buf, sizeof(num_buf));
                size_t nlen = colunwind_safe_strlen(num_buf);
                while (min_width > (int)nlen && out_idx + 1 < size) {
                    buf[out_idx++] = pad_zero ? '0' : ' ';
                    min_width--;
                }
                for (size_t k = 0; k < nlen && out_idx + 1 < size; ++k) {
                    buf[out_idx++] = num_buf[k];
                }
                break;
            }
            case 'u': {
                uint64_t val = is_long_long ? va_arg(args, uint64_t) : (uint64_t)va_arg(args, unsigned int);
                colunwind_safe_u64_to_dec(val, num_buf, sizeof(num_buf));
                size_t nlen = colunwind_safe_strlen(num_buf);
                while (min_width > (int)nlen && out_idx + 1 < size) {
                    buf[out_idx++] = pad_zero ? '0' : ' ';
                    min_width--;
                }
                for (size_t k = 0; k < nlen && out_idx + 1 < size; ++k) {
                    buf[out_idx++] = num_buf[k];
                }
                break;
            }
            case 'x':
            case 'X': {
                uint64_t val = is_long_long ? va_arg(args, uint64_t) : (uint64_t)va_arg(args, unsigned int);
                colunwind_safe_u64_to_hex(val, num_buf, sizeof(num_buf), min_width);
                size_t nlen = colunwind_safe_strlen(num_buf);
                for (size_t k = 0; k < nlen && out_idx + 1 < size; ++k) {
                    buf[out_idx++] = num_buf[k];
                }
                break;
            }
            case 'p': {
                uintptr_t val = (uintptr_t)va_arg(args, void*);
                if (out_idx + 2 < size) {
                    buf[out_idx++] = '0';
                    buf[out_idx++] = 'x';
                }
                int ptr_width = (sizeof(void*) == 8) ? 16 : 8;
                colunwind_safe_u64_to_hex((uint64_t)val, num_buf, sizeof(num_buf), ptr_width);
                size_t nlen = colunwind_safe_strlen(num_buf);
                for (size_t k = 0; k < nlen && out_idx + 1 < size; ++k) {
                    buf[out_idx++] = num_buf[k];
                }
                break;
            }
            default:
                buf[out_idx++] = fmt[i];
                break;
        }
    }

    buf[out_idx] = '\0';
    return out_idx;
}

size_t colunwind_safe_snprintf(char* buf, size_t size, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    size_t written = colunwind_safe_vsnprintf(buf, size, fmt, args);
    va_end(args);
    return written;
}

/* ========================================================================= */
/* 配置与生命周期 API                                                        */
/* ========================================================================= */

colunwind_status_t colunwind_config_init(colunwind_config_t* config) {
    if (!config) return COLUNWIND_ERROR_INVALID_ARGUMENT;
    config->on_crash = NULL;
    config->user_data = NULL;
    config->dump_directory = NULL;
    config->enable_auto_dump = true;
    config->enable_altstack = true;
    config->altstack_size = 64 * 1024;      /* 64 KB 备用信号栈 */
    config->arena_size = 128 * 1024;        /* 128 KB 内存池 */
    return COLUNWIND_SUCCESS;
}

colunwind_status_t colunwind_init(const colunwind_config_t* config) {
    if (g_colunwind_runtime.initialized) {
        return COLUNWIND_ERROR_ALREADY_INITIALIZED;
    }

    if (config) {
        g_colunwind_runtime.config = *config;
    } else {
        colunwind_config_init(&g_colunwind_runtime.config);
    }

    size_t arena_sz = g_colunwind_runtime.config.arena_size;
    if (arena_sz < 32 * 1024) arena_sz = 32 * 1024;

    /* 分配底层 Arena 内存 */
    g_colunwind_runtime.arena_raw_mem = colunwind_os_alloc_pages(arena_sz);
    if (!g_colunwind_runtime.arena_raw_mem) {
        return COLUNWIND_ERROR_OUT_OF_MEMORY;
    }
    g_colunwind_runtime.arena_raw_size = arena_sz;
    colunwind_arena_init(&g_colunwind_runtime.arena, g_colunwind_runtime.arena_raw_mem, arena_sz);

#if defined(_WIN32)
    g_colunwind_runtime.process_handle = GetCurrentProcess();
    /* 初始化 DbgHelp 符号表 */
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);

    char search_path[1024];
    char exe_dir[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, exe_dir, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        char* last_slash = strrchr(exe_dir, '\\');
        if (last_slash) *last_slash = '\0';
        colunwind_safe_snprintf(search_path, sizeof(search_path), ".;%s;%s\\..", exe_dir, exe_dir);
    } else {
        colunwind_safe_strncpy(search_path, ".", sizeof(search_path));
    }

    if (SymInitialize(g_colunwind_runtime.process_handle, search_path, TRUE)) {
        g_colunwind_runtime.symbols_initialized = true;
    } else {
        g_colunwind_runtime.symbols_initialized = false;
    }
#else
    /* POSIX: 备用信号栈初始化 */
    if (g_colunwind_runtime.config.enable_altstack) {
        size_t stk_sz = g_colunwind_runtime.config.altstack_size;
        if (stk_sz < 32 * 1024) stk_sz = 32 * 1024;
        g_colunwind_runtime.altstack_mem = colunwind_os_alloc_pages(stk_sz);
        if (g_colunwind_runtime.altstack_mem) {
            g_colunwind_runtime.altstack_size = stk_sz;
            stack_t ss;
            ss.ss_sp = g_colunwind_runtime.altstack_mem;
            ss.ss_flags = 0;
            ss.ss_size = stk_sz;
            sigaltstack(&ss, NULL);
        }
    }
#endif

    g_colunwind_runtime.initialized = true;
    return COLUNWIND_SUCCESS;
}

void colunwind_shutdown(void) {
    if (!g_colunwind_runtime.initialized) return;

    if (g_colunwind_runtime.handler_installed) {
        colunwind_uninstall_crash_handler();
    }

#if defined(_WIN32)
    if (g_colunwind_runtime.symbols_initialized) {
        SymCleanup(g_colunwind_runtime.process_handle);
        g_colunwind_runtime.symbols_initialized = false;
    }
#else
    if (g_colunwind_runtime.altstack_mem) {
        stack_t ss;
        ss.ss_sp = NULL;
        ss.ss_flags = SS_DISABLE;
        ss.ss_size = 0;
        sigaltstack(&ss, NULL);
        colunwind_os_free_pages(g_colunwind_runtime.altstack_mem, g_colunwind_runtime.altstack_size);
        g_colunwind_runtime.altstack_mem = NULL;
    }
#endif

    if (g_colunwind_runtime.arena_raw_mem) {
        colunwind_os_free_pages(g_colunwind_runtime.arena_raw_mem, g_colunwind_runtime.arena_raw_size);
        g_colunwind_runtime.arena_raw_mem = NULL;
    }

    g_colunwind_runtime.initialized = false;
}

/* ========================================================================= */
/* 异常分发与崩溃处理核心                                                    */
/* ========================================================================= */

static void colunwind_handle_crash_internal(colunwind_crash_type_t crash_type,
                                            uintptr_t fault_addr,
                                            void* native_context) {
    /* 使用固定内存池或静态紧急缓冲构建崩溃上下文 */
    static colunwind_crash_context_t crash_ctx;
    crash_ctx.crash_type = crash_type;
    crash_ctx.fault_address = fault_addr;
    crash_ctx.thread_id = colunwind_os_get_thread_id();
    crash_ctx.native_context = native_context;
    crash_ctx.backtrace.frame_count = 0;

    /* 1. 栈回溯展开 */
    if (native_context) {
        colunwind_backtrace_capture_from_context(&crash_ctx.backtrace, native_context, 0);
    } else {
        colunwind_backtrace_capture(&crash_ctx.backtrace, 1);
    }

    /* 2. 符号化解析 */
    colunwind_symbolize(&crash_ctx.backtrace);

    /* 3. 自动生成转储文件 (如果开启) */
    if (g_colunwind_runtime.config.enable_auto_dump) {
        char dump_filename[512];
        char ts[64];
        colunwind_os_get_timestamp(ts, sizeof(ts));
        const char* dir = g_colunwind_runtime.config.dump_directory ? g_colunwind_runtime.config.dump_directory : ".";
        colunwind_safe_snprintf(dump_filename, sizeof(dump_filename), "%s/crash_%s.dmp", dir, ts);
        colunwind_write_dump(dump_filename, &crash_ctx, COLUNWIND_DUMP_NORMAL);
    }

    /* 4. 执行用户定义的回调 */
    if (g_colunwind_runtime.config.on_crash) {
        g_colunwind_runtime.config.on_crash(&crash_ctx, g_colunwind_runtime.config.user_data);
    }

    /* 5. 异步安全打印崩溃诊断与栈回溯至 stderr */
    char banner[256];
    colunwind_safe_snprintf(banner, sizeof(banner),
        "\n=======================================================\n"
        "[colunwind] CRASH DETECTED: %s\n"
        "[colunwind] Fault Address: 0x%p | Thread ID: %llu\n"
        "=======================================================\n",
        colunwind_crash_type_to_string(crash_type),
        (void*)fault_addr,
        (unsigned long long)crash_ctx.thread_id);
    colunwind_raw_write_string_stderr(banner);

    colunwind_print_backtrace(&crash_ctx.backtrace);

    colunwind_raw_write_string_stderr("=======================================================\n\n");
}

#if defined(_WIN32)

static colunwind_crash_type_t colunwind_win32_exception_to_crash_type(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_DATATYPE_MISALIGNMENT:
            return COLUNWIND_CRASH_ACCESS_VIOLATION;
        case EXCEPTION_ILLEGAL_INSTRUCTION:
        case EXCEPTION_PRIV_INSTRUCTION:
            return COLUNWIND_CRASH_ILLEGAL_INSTRUCTION;
        case EXCEPTION_STACK_OVERFLOW:
            return COLUNWIND_CRASH_STACK_OVERFLOW;
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
            return COLUNWIND_CRASH_INTEGER_DIV_BY_ZERO;
        case EXCEPTION_BREAKPOINT:
        case EXCEPTION_SINGLE_STEP:
            return COLUNWIND_CRASH_TRAP;
        default:
            return COLUNWIND_CRASH_UNKNOWN;
    }
}

static LONG WINAPI colunwind_veh_handler(PEXCEPTION_POINTERS ep) {
    if (!ep || !ep->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
    DWORD code = ep->ExceptionRecord->ExceptionCode;

    /* 仅处理致命或崩溃类型异常，忽略非致命 C++ 异常 (0xE06D7363) 及 RPC 异常等 */
    if (code == EXCEPTION_ACCESS_VIOLATION ||
        code == EXCEPTION_ILLEGAL_INSTRUCTION ||
        code == EXCEPTION_STACK_OVERFLOW ||
        code == EXCEPTION_INT_DIVIDE_BY_ZERO) {

        colunwind_crash_type_t ctype = colunwind_win32_exception_to_crash_type(code);
        uintptr_t fault_addr = 0;
        if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
            fault_addr = (uintptr_t)ep->ExceptionRecord->ExceptionInformation[1];
        } else {
            fault_addr = (uintptr_t)ep->ExceptionRecord->ExceptionAddress;
        }

        colunwind_handle_crash_internal(ctype, fault_addr, ep);
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

static LONG WINAPI colunwind_unhandled_filter(PEXCEPTION_POINTERS ep) {
    if (ep && ep->ExceptionRecord) {
        DWORD code = ep->ExceptionRecord->ExceptionCode;
        colunwind_crash_type_t ctype = colunwind_win32_exception_to_crash_type(code);
        uintptr_t fault_addr = (uintptr_t)ep->ExceptionRecord->ExceptionAddress;
        colunwind_handle_crash_internal(ctype, fault_addr, ep);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

colunwind_status_t colunwind_install_crash_handler(void) {
    if (!g_colunwind_runtime.initialized) {
        colunwind_init(NULL);
    }
    if (g_colunwind_runtime.handler_installed) {
        return COLUNWIND_SUCCESS;
    }

    /* 1. 注册 VEH */
    g_colunwind_runtime.veh_handle = AddVectoredExceptionHandler(1, colunwind_veh_handler);
    /* 2. 注册顶级未捕获异常过滤器 */
    g_colunwind_runtime.prev_filter = SetUnhandledExceptionFilter(colunwind_unhandled_filter);

    g_colunwind_runtime.handler_installed = true;
    return COLUNWIND_SUCCESS;
}

colunwind_status_t colunwind_uninstall_crash_handler(void) {
    if (!g_colunwind_runtime.handler_installed) {
        return COLUNWIND_SUCCESS;
    }

    if (g_colunwind_runtime.veh_handle) {
        RemoveVectoredExceptionHandler(g_colunwind_runtime.veh_handle);
        g_colunwind_runtime.veh_handle = NULL;
    }
    if (g_colunwind_runtime.prev_filter) {
        SetUnhandledExceptionFilter(g_colunwind_runtime.prev_filter);
        g_colunwind_runtime.prev_filter = NULL;
    }

    g_colunwind_runtime.handler_installed = false;
    return COLUNWIND_SUCCESS;
}

#else /* POSIX */

static void colunwind_posix_sigaction(int signum, siginfo_t* info, void* uctx) {
    colunwind_crash_type_t ctype = COLUNWIND_CRASH_UNKNOWN;
    uintptr_t fault_addr = 0;
    if (info) {
        fault_addr = (uintptr_t)info->si_addr;
    }

    switch (signum) {
        case SIGSEGV:
        case SIGBUS:
            ctype = COLUNWIND_CRASH_ACCESS_VIOLATION;
            break;
        case SIGFPE:
            ctype = COLUNWIND_CRASH_INTEGER_DIV_BY_ZERO;
            break;
        case SIGILL:
            ctype = COLUNWIND_CRASH_ILLEGAL_INSTRUCTION;
            break;
        case SIGABRT:
            ctype = COLUNWIND_CRASH_ABORT;
            break;
        case SIGTRAP:
            ctype = COLUNWIND_CRASH_TRAP;
            break;
        default:
            ctype = COLUNWIND_CRASH_UNKNOWN;
            break;
    }

    colunwind_handle_crash_internal(ctype, fault_addr, uctx);

    /* 恢复默认信号处理并重新触发，保证遵循核心转储行为 */
    signal(signum, SIG_DFL);
    raise(signum);
}

colunwind_status_t colunwind_install_crash_handler(void) {
    if (!g_colunwind_runtime.initialized) {
        colunwind_init(NULL);
    }
    if (g_colunwind_runtime.handler_installed) {
        return COLUNWIND_SUCCESS;
    }

    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sa.sa_sigaction = colunwind_posix_sigaction;

    sigaction(SIGSEGV, &sa, &g_colunwind_runtime.old_sigaction_segv);
    sigaction(SIGFPE,  &sa, &g_colunwind_runtime.old_sigaction_fpe);
    sigaction(SIGILL,  &sa, &g_colunwind_runtime.old_sigaction_ill);
    sigaction(SIGABRT, &sa, &g_colunwind_runtime.old_sigaction_abrt);
    sigaction(SIGBUS,  &sa, &g_colunwind_runtime.old_sigaction_bus);
    sigaction(SIGTRAP, &sa, &g_colunwind_runtime.old_sigaction_trap);

    g_colunwind_runtime.handler_installed = true;
    return COLUNWIND_SUCCESS;
}

colunwind_status_t colunwind_uninstall_crash_handler(void) {
    if (!g_colunwind_runtime.handler_installed) {
        return COLUNWIND_SUCCESS;
    }

    sigaction(SIGSEGV, &g_colunwind_runtime.old_sigaction_segv, NULL);
    sigaction(SIGFPE,  &g_colunwind_runtime.old_sigaction_fpe, NULL);
    sigaction(SIGILL,  &g_colunwind_runtime.old_sigaction_ill, NULL);
    sigaction(SIGABRT, &g_colunwind_runtime.old_sigaction_abrt, NULL);
    sigaction(SIGBUS,  &g_colunwind_runtime.old_sigaction_bus, NULL);
    sigaction(SIGTRAP, &g_colunwind_runtime.old_sigaction_trap, NULL);

    g_colunwind_runtime.handler_installed = false;
    return COLUNWIND_SUCCESS;
}

#endif

/* ========================================================================= */
/* 格式化输出与枚举转换 API                                                  */
/* ========================================================================= */

void colunwind_print_backtrace(const colunwind_backtrace_t* trace) {
    if (!trace || trace->frame_count == 0) {
        colunwind_raw_write_string_stderr("  [Empty backtrace]\n");
        return;
    }

    char line_buf[1024];
    for (uint32_t i = 0; i < trace->frame_count; ++i) {
        const colunwind_frame_t* f = &trace->frames[i];
        const char* sym = (f->symbol_name[0] != '\0') ? f->symbol_name : "???";
        const char* mod = (f->module_name[0] != '\0') ? f->module_name : "???";

        if (f->file_path[0] != '\0' && f->line > 0) {
            colunwind_safe_snprintf(line_buf, sizeof(line_buf),
                "#%02u %p in %s+0x%x [%s] at %s:%u:%u\n",
                (unsigned)i,
                (void*)f->instruction_pointer,
                sym,
                (unsigned)f->offset,
                mod,
                f->file_path,
                (unsigned)f->line,
                (unsigned)f->column);
        } else {
            colunwind_safe_snprintf(line_buf, sizeof(line_buf),
                "#%02u %p in %s+0x%x [%s]\n",
                (unsigned)i,
                (void*)f->instruction_pointer,
                sym,
                (unsigned)f->offset,
                mod);
        }
        colunwind_raw_write_string_stderr(line_buf);
    }
}

const char* colunwind_crash_type_to_string(colunwind_crash_type_t type) {
    switch (type) {
        case COLUNWIND_CRASH_NONE:               return "NONE";
        case COLUNWIND_CRASH_ACCESS_VIOLATION:   return "ACCESS_VIOLATION";
        case COLUNWIND_CRASH_ILLEGAL_INSTRUCTION:return "ILLEGAL_INSTRUCTION";
        case COLUNWIND_CRASH_STACK_OVERFLOW:     return "STACK_OVERFLOW";
        case COLUNWIND_CRASH_INTEGER_DIV_BY_ZERO:return "INTEGER_DIV_BY_ZERO";
        case COLUNWIND_CRASH_ABORT:              return "ABORT";
        case COLUNWIND_CRASH_TRAP:               return "TRAP";
        default:                                 return "UNKNOWN_CRASH";
    }
}

const char* colunwind_status_to_string(colunwind_status_t status) {
    switch (status) {
        case COLUNWIND_SUCCESS:                       return "COLUNWIND_SUCCESS";
        case COLUNWIND_ERROR_INVALID_ARGUMENT:       return "COLUNWIND_ERROR_INVALID_ARGUMENT";
        case COLUNWIND_ERROR_OUT_OF_MEMORY:          return "COLUNWIND_ERROR_OUT_OF_MEMORY";
        case COLUNWIND_ERROR_NOT_INITIALIZED:        return "COLUNWIND_ERROR_NOT_INITIALIZED";
        case COLUNWIND_ERROR_ALREADY_INITIALIZED:    return "COLUNWIND_ERROR_ALREADY_INITIALIZED";
        case COLUNWIND_ERROR_UNWIND_FAILED:          return "COLUNWIND_ERROR_UNWIND_FAILED";
        case COLUNWIND_ERROR_SYMBOL_NOT_FOUND:       return "COLUNWIND_ERROR_SYMBOL_NOT_FOUND";
        case COLUNWIND_ERROR_DUMP_FAILED:            return "COLUNWIND_ERROR_DUMP_FAILED";
        case COLUNWIND_ERROR_PLATFORM_NOT_SUPPORTED: return "COLUNWIND_ERROR_PLATFORM_NOT_SUPPORTED";
        case COLUNWIND_ERROR_IO_FAILED:              return "COLUNWIND_ERROR_IO_FAILED";
        default:                                     return "UNKNOWN_STATUS";
    }
}
