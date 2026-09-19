#include "arch/arch.h"
#include "internal.h"

#if defined(COLUNWIND_ARCH_X86_64) || defined(COLUNWIND_ARCH_X86)

void colunwind_arch_extract_registers(const void* native_context, colunwind_arch_regs_t* regs) {
    if (!regs) return;
    regs->ip = 0;
    regs->sp = 0;
    regs->fp = 0;
    regs->flags = 0;

    if (!native_context) return;

#if defined(_WIN32)
    const CONTEXT* ctx = NULL;
    /* 兼容 EXCEPTION_POINTERS 或直接传入的 CONTEXT */
    const EXCEPTION_POINTERS* ep = (const EXCEPTION_POINTERS*)native_context;
    if (ep->ContextRecord != NULL && ep->ExceptionRecord != NULL) {
        ctx = ep->ContextRecord;
    } else {
        ctx = (const CONTEXT*)native_context;
    }

#if defined(COLUNWIND_ARCH_X86_64)
    regs->ip = (uintptr_t)ctx->Rip;
    regs->sp = (uintptr_t)ctx->Rsp;
    regs->fp = (uintptr_t)ctx->Rbp;
    regs->flags = (uintptr_t)ctx->EFlags;
#else
    regs->ip = (uintptr_t)ctx->Eip;
    regs->sp = (uintptr_t)ctx->Esp;
    regs->fp = (uintptr_t)ctx->Ebp;
    regs->flags = (uintptr_t)ctx->EFlags;
#endif

#elif defined(__linux__) || defined(__APPLE__)
    #include <ucontext.h>
    const ucontext_t* uc = (const ucontext_t*)native_context;
#if defined(__x86_64__)
  #if defined(__APPLE__)
    regs->ip = (uintptr_t)uc->uc_mcontext->__ss.__rip;
    regs->sp = (uintptr_t)uc->uc_mcontext->__ss.__rsp;
    regs->fp = (uintptr_t)uc->uc_mcontext->__ss.__rbp;
  #else
    regs->ip = (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
    regs->sp = (uintptr_t)uc->uc_mcontext.gregs[REG_RSP];
    regs->fp = (uintptr_t)uc->uc_mcontext.gregs[REG_RBP];
  #endif
#elif defined(__i386__)
  #if defined(__APPLE__)
    regs->ip = (uintptr_t)uc->uc_mcontext->__ss.__eip;
    regs->sp = (uintptr_t)uc->uc_mcontext->__ss.__esp;
    regs->fp = (uintptr_t)uc->uc_mcontext->__ss.__ebp;
  #else
    regs->ip = (uintptr_t)uc->uc_mcontext.gregs[REG_EIP];
    regs->sp = (uintptr_t)uc->uc_mcontext.gregs[REG_ESP];
    regs->fp = (uintptr_t)uc->uc_mcontext.gregs[REG_EBP];
  #endif
#endif
#endif
}

#endif /* COLUNWIND_ARCH_X86_64 || COLUNWIND_ARCH_X86 */
