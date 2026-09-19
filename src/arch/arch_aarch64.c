#include "arch/arch.h"
#include "internal.h"

#if defined(COLUNWIND_ARCH_AARCH64) || defined(COLUNWIND_ARCH_ARM)

void colunwind_arch_extract_registers(const void* native_context, colunwind_arch_regs_t* regs) {
    if (!regs) return;
    regs->ip = 0;
    regs->sp = 0;
    regs->fp = 0;
    regs->flags = 0;

    if (!native_context) return;

#if defined(_WIN32)
    const CONTEXT* ctx = NULL;
    const EXCEPTION_POINTERS* ep = (const EXCEPTION_POINTERS*)native_context;
    if (ep->ContextRecord != NULL && ep->ExceptionRecord != NULL) {
        ctx = ep->ContextRecord;
    } else {
        ctx = (const CONTEXT*)native_context;
    }

#if defined(COLUNWIND_ARCH_AARCH64)
    regs->ip = (uintptr_t)ctx->Pc;
    regs->sp = (uintptr_t)ctx->Sp;
    regs->fp = (uintptr_t)ctx->Fp;
#elif defined(COLUNWIND_ARCH_ARM)
    regs->ip = (uintptr_t)ctx->Pc;
    regs->sp = (uintptr_t)ctx->Sp;
    regs->fp = (uintptr_t)ctx->R11;
#endif

#elif defined(__linux__) || defined(__APPLE__)
    #include <ucontext.h>
    const ucontext_t* uc = (const ucontext_t*)native_context;
#if defined(__aarch64__)
  #if defined(__APPLE__)
    regs->ip = (uintptr_t)uc->uc_mcontext->__ss.__pc;
    regs->sp = (uintptr_t)uc->uc_mcontext->__ss.__sp;
    regs->fp = (uintptr_t)uc->uc_mcontext->__ss.__fp;
  #else
    regs->ip = (uintptr_t)uc->uc_mcontext.pc;
    regs->sp = (uintptr_t)uc->uc_mcontext.sp;
    regs->fp = (uintptr_t)uc->uc_mcontext.regs[29];
  #endif
#endif
#endif
}

#endif /* COLUNWIND_ARCH_AARCH64 || COLUNWIND_ARCH_ARM */
