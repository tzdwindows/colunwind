#include "colunwind/colunwind.h"
#include "internal.h"
#include "arch/arch.h"
#include "os/os.h"

#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__has_include)
  #if __has_include(<execinfo.h>)
    #define COLUNWIND_HAS_EXECINFO 1
    #include <execinfo.h>
  #endif
#endif

/* 初始化空帧结构 */
static void colunwind_frame_init_defaults(colunwind_frame_t* frame, uintptr_t ip, uintptr_t sp, uintptr_t fp) {
    if (!frame) return;
    frame->instruction_pointer = ip;
    frame->stack_pointer = sp;
    frame->frame_pointer = fp;
    frame->module_base = 0;
    frame->module_name[0] = '\0';
    frame->symbol_name[0] = '\0';
    frame->file_path[0] = '\0';
    frame->file = frame->file_path;
    frame->line = 0;
    frame->column = 0;
    frame->function = frame->symbol_name;
    frame->offset = 0;

    /* 提前获取模块信息 */
    colunwind_os_get_module_info(ip, frame->module_name, sizeof(frame->module_name), &frame->module_base);
}

colunwind_status_t colunwind_backtrace_capture_from_context(colunwind_backtrace_t* trace, const void* native_context, uint32_t skip_frames) {
    if (!trace) return COLUNWIND_ERROR_INVALID_ARGUMENT;
    trace->frame_count = 0;
    if (!native_context) {
        return colunwind_backtrace_capture(trace, skip_frames);
    }

#if defined(_WIN32) && defined(COLUNWIND_ARCH_X86_64)
    const CONTEXT* in_ctx = NULL;
    const EXCEPTION_POINTERS* ep = (const EXCEPTION_POINTERS*)native_context;
    if (ep->ContextRecord != NULL && ep->ExceptionRecord != NULL) {
        in_ctx = ep->ContextRecord;
    } else {
        in_ctx = (const CONTEXT*)native_context;
    }

    CONTEXT ctx;
    memcpy(&ctx, in_ctx, sizeof(CONTEXT));

    uint32_t skipped = 0;
    uintptr_t prev_rip = 0;
    uintptr_t prev_rsp = 0;

    PNT_TIB tib = (PNT_TIB)NtCurrentTeb();
    uintptr_t stack_low = tib ? (uintptr_t)tib->StackLimit : 0;
    uintptr_t stack_high = tib ? (uintptr_t)tib->StackBase : (uintptr_t)-1;

    while (trace->frame_count < COLUNWIND_MAX_FRAMES && ctx.Rip != 0) {
        if (prev_rip == (uintptr_t)ctx.Rip && prev_rsp == (uintptr_t)ctx.Rsp) {
            break;
        }
        if (ctx.Rsp != 0 && (ctx.Rsp < stack_low || ctx.Rsp >= stack_high)) {
            break;
        }

        prev_rip = (uintptr_t)ctx.Rip;
        prev_rsp = (uintptr_t)ctx.Rsp;

        if (skipped < skip_frames) {
            skipped++;
        } else {
            colunwind_frame_t* frame = &trace->frames[trace->frame_count++];
            colunwind_frame_init_defaults(frame, (uintptr_t)ctx.Rip, (uintptr_t)ctx.Rsp, (uintptr_t)ctx.Rbp);
        }

        DWORD64 imageBase = 0;
        PRUNTIME_FUNCTION pRuntimeFunction = RtlLookupFunctionEntry(ctx.Rip, &imageBase, NULL);

        if (!pRuntimeFunction) {
            /* 叶子函数或无展开表函数：从栈顶取返回地址 */
            if (ctx.Rsp == 0 || ctx.Rsp + sizeof(DWORD64) > stack_high) {
                break;
            }
            ctx.Rip = *(DWORD64*)ctx.Rsp;
            ctx.Rsp += sizeof(DWORD64);
        } else {
            PVOID handlerData = NULL;
            ULONG64 establisherFrame = 0;

            RtlVirtualUnwind(UNW_FLAG_NHANDLER,
                             imageBase,
                             ctx.Rip,
                             pRuntimeFunction,
                             &ctx,
                             &handlerData,
                             &establisherFrame,
                             NULL);
        }
    }
    return COLUNWIND_SUCCESS;

#elif defined(_WIN32)
    /* 32位 Windows 或 ARM 平台展开 */
    void* addrs[COLUNWIND_MAX_FRAMES];
    USHORT captured = CaptureStackBackTrace(skip_frames, COLUNWIND_MAX_FRAMES, addrs, NULL);
    for (USHORT i = 0; i < captured; ++i) {
        colunwind_frame_t* frame = &trace->frames[trace->frame_count++];
        colunwind_frame_init_defaults(frame, (uintptr_t)addrs[i], 0, 0);
    }
    return COLUNWIND_SUCCESS;

#else
    /* POSIX: 基于 ucontext 与栈帧指针展开 */
    colunwind_arch_regs_t regs;
    colunwind_arch_extract_registers(native_context, &regs);

    uint32_t skipped = 0;
    if (regs.ip != 0) {
        if (skipped < skip_frames) {
            skipped++;
        } else {
            colunwind_frame_t* frame = &trace->frames[trace->frame_count++];
            colunwind_frame_init_defaults(frame, regs.ip, regs.sp, regs.fp);
        }
    }

    /* 帧指针回溯 */
    uintptr_t cur_fp = regs.fp;
    while (cur_fp != 0 && trace->frame_count < COLUNWIND_MAX_FRAMES) {
        uintptr_t* fp_ptr = (uintptr_t*)cur_fp;
        uintptr_t next_fp = fp_ptr[0];
        uintptr_t ret_addr = fp_ptr[1];

        if (ret_addr == 0 || next_fp <= cur_fp) {
            break;
        }

        if (skipped < skip_frames) {
            skipped++;
        } else {
            colunwind_frame_t* frame = &trace->frames[trace->frame_count++];
            colunwind_frame_init_defaults(frame, ret_addr, cur_fp, next_fp);
        }
        cur_fp = next_fp;
    }
    return COLUNWIND_SUCCESS;
#endif
}

colunwind_status_t colunwind_backtrace_capture(colunwind_backtrace_t* trace, uint32_t skip_frames) {
    if (!trace) return COLUNWIND_ERROR_INVALID_ARGUMENT;
    trace->frame_count = 0;

#if defined(_WIN32)
    void* addrs[COLUNWIND_MAX_FRAMES];
    USHORT captured = CaptureStackBackTrace(skip_frames + 1, COLUNWIND_MAX_FRAMES, addrs, NULL);
    for (USHORT i = 0; i < captured; ++i) {
        colunwind_frame_t* frame = &trace->frames[trace->frame_count++];
        colunwind_frame_init_defaults(frame, (uintptr_t)addrs[i], 0, 0);
    }
    return COLUNWIND_SUCCESS;
#else
    /* POSIX fallback: 使用 execinfo (如 Linux/glibc) 或 GCC frame pointer 回溯 */
#if defined(__has_include)
  #if __has_include(<execinfo.h>)
    #define COLUNWIND_HAS_EXECINFO 1
    #include <execinfo.h>
  #endif
#endif
#if defined(COLUNWIND_HAS_EXECINFO)
    void* buffer[COLUNWIND_MAX_FRAMES];
    int nptrs = backtrace(buffer, COLUNWIND_MAX_FRAMES);
    for (int i = (int)skip_frames + 1; i < nptrs; ++i) {
        colunwind_frame_t* frame = &trace->frames[trace->frame_count++];
        colunwind_frame_init_defaults(frame, (uintptr_t)buffer[i], 0, 0);
    }
    return COLUNWIND_SUCCESS;
#else
    uintptr_t cur_fp = (uintptr_t)__builtin_frame_address(0);
    uint32_t skipped = 0;
    while (cur_fp != 0 && trace->frame_count < COLUNWIND_MAX_FRAMES) {
        uintptr_t* fp_ptr = (uintptr_t*)cur_fp;
        uintptr_t next_fp = fp_ptr[0];
        uintptr_t ret_addr = fp_ptr[1];
        if (ret_addr == 0 || next_fp <= cur_fp) {
            break;
        }
        if (skipped < skip_frames) {
            skipped++;
        } else {
            colunwind_frame_t* frame = &trace->frames[trace->frame_count++];
            colunwind_frame_init_defaults(frame, ret_addr, cur_fp, next_fp);
        }
        cur_fp = next_fp;
    }
    return COLUNWIND_SUCCESS;
#endif
#endif
}
