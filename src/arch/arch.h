#ifndef COLUNWIND_ARCH_H
#define COLUNWIND_ARCH_H

#include "colunwind/types.h"
#include <stdint.h>

#if defined(_M_X64) || defined(__x86_64__)
  #define COLUNWIND_ARCH_X86_64 1
  #define COLUNWIND_ARCH_NAME "x86_64"
#elif defined(_M_ARM64) || defined(__aarch64__)
  #define COLUNWIND_ARCH_AARCH64 1
  #define COLUNWIND_ARCH_NAME "aarch64"
#elif defined(_M_IX86) || defined(__i386__)
  #define COLUNWIND_ARCH_X86 1
  #define COLUNWIND_ARCH_NAME "x86"
#elif defined(_M_ARM) || defined(__arm__)
  #define COLUNWIND_ARCH_ARM 1
  #define COLUNWIND_ARCH_NAME "arm"
#else
  #define COLUNWIND_ARCH_UNKNOWN 1
  #define COLUNWIND_ARCH_NAME "unknown"
#endif

typedef struct colunwind_arch_regs {
    uintptr_t ip;  /* Instruction Pointer (RIP / PC / EIP) */
    uintptr_t sp;  /* Stack Pointer (RSP / SP / ESP) */
    uintptr_t fp;  /* Frame Pointer (RBP / FP / EBP) */
    uintptr_t flags;
} colunwind_arch_regs_t;

/**
 * @brief 从原生系统上下文中抽取 IP, SP, FP 等寄存器
 */
void colunwind_arch_extract_registers(const void* native_context, colunwind_arch_regs_t* regs);

#endif /* COLUNWIND_ARCH_H */
