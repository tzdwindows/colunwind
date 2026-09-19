#ifndef COLUNWIND_INTERNAL_H
#define COLUNWIND_INTERNAL_H

#include "colunwind/colunwind.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
  #include <dbghelp.h>
#else
  #include <unistd.h>
  #include <signal.h>
  #include <sys/types.h>
  #include <sys/stat.h>
  #include <fcntl.h>
#endif

/* ========================================================================= */
/* 1. 预分配内存 Arena (Async-Signal-Safe，杜绝崩溃时动态分配)             */
/* ========================================================================= */
typedef struct colunwind_arena {
    uint8_t* buffer;
    size_t   capacity;
    size_t   offset;
} colunwind_arena_t;

static inline void colunwind_arena_init(colunwind_arena_t* arena, void* buffer, size_t capacity) {
    if (!arena) return;
    arena->buffer = (uint8_t*)buffer;
    arena->capacity = capacity;
    arena->offset = 0;
}

static inline void* colunwind_arena_alloc(colunwind_arena_t* arena, size_t size) {
    if (!arena || !arena->buffer) return NULL;
    /* 8 字节对齐 */
    size_t aligned_size = (size + 7) & ~((size_t)7);
    if (arena->offset + aligned_size > arena->capacity) {
        return NULL;
    }
    void* ptr = &arena->buffer[arena->offset];
    arena->offset += aligned_size;
    return ptr;
}

static inline void colunwind_arena_reset(colunwind_arena_t* arena) {
    if (arena) {
        arena->offset = 0;
    }
}

/* ========================================================================= */
/* 2. Async-Signal-Safe 字符串与整型转换底层实现 (无锁、无堆分配)           */
/* ========================================================================= */
static inline size_t colunwind_safe_strlen(const char* s) {
    if (!s) return 0;
    size_t len = 0;
    while (s[len] != '\0') {
        len++;
    }
    return len;
}

static inline void colunwind_safe_strncpy(char* dst, const char* src, size_t max_len) {
    if (!dst || max_len == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t i = 0;
    while (i + 1 < max_len && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static inline void colunwind_safe_strcat(char* dst, const char* src, size_t max_len) {
    if (!dst || !src || max_len == 0) return;
    size_t dst_len = colunwind_safe_strlen(dst);
    if (dst_len >= max_len - 1) return;
    size_t i = 0;
    while (dst_len + i + 1 < max_len && src[i] != '\0') {
        dst[dst_len + i] = src[i];
        i++;
    }
    dst[dst_len + i] = '\0';
}

static inline int colunwind_safe_strcmp(const char* s1, const char* s2) {
    if (s1 == s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

/* 十六进制转换 */
static inline size_t colunwind_safe_u64_to_hex(uint64_t value, char* buf, size_t buf_size, int min_width) {
    static const char hex_digits[] = "0123456789abcdef";
    char temp[24];
    int len = 0;
    if (value == 0) {
        temp[len++] = '0';
    } else {
        while (value > 0 && len < (int)sizeof(temp)) {
            temp[len++] = hex_digits[value & 0xF];
            value >>= 4;
        }
    }
    while (len < min_width && len < (int)sizeof(temp)) {
        temp[len++] = '0';
    }
    if ((size_t)(len + 1) > buf_size) return 0;
    for (int i = 0; i < len; ++i) {
        buf[i] = temp[len - 1 - i];
    }
    buf[len] = '\0';
    return (size_t)len;
}

/* 十进制无符号转换 */
static inline size_t colunwind_safe_u64_to_dec(uint64_t value, char* buf, size_t buf_size) {
    char temp[24];
    int len = 0;
    if (value == 0) {
        temp[len++] = '0';
    } else {
        while (value > 0 && len < (int)sizeof(temp)) {
            temp[len++] = (char)('0' + (value % 10));
            value /= 10;
        }
    }
    if ((size_t)(len + 1) > buf_size) return 0;
    for (int i = 0; i < len; ++i) {
        buf[i] = temp[len - 1 - i];
    }
    buf[len] = '\0';
    return (size_t)len;
}

/* 简易信号安全 snprintf (支持 %s, %c, %u, %d, %x, %p, %llu, %llx) */
size_t colunwind_safe_vsnprintf(char* buf, size_t size, const char* fmt, va_list args);
size_t colunwind_safe_snprintf(char* buf, size_t size, const char* fmt, ...);

/* ========================================================================= */
/* 3. 系统底层 I/O (Async-Signal-Safe 系统调用)                             */
/* ========================================================================= */
void colunwind_raw_write_stderr(const char* data, size_t len);
void colunwind_raw_write_string_stderr(const char* str);
intptr_t colunwind_raw_open_file_write(const char* path);
void colunwind_raw_close_file(intptr_t fd);
bool colunwind_raw_write_file(intptr_t fd, const void* data, size_t len);

/* ========================================================================= */
/* 4. 全局运行时管理上下文                                                   */
/* ========================================================================= */
typedef struct colunwind_runtime {
    colunwind_config_t config;
    bool               initialized;
    bool               handler_installed;
    colunwind_arena_t  arena;
    void*              arena_raw_mem;
    size_t             arena_raw_size;

#if defined(_WIN32)
    PVOID              veh_handle;
    LPTOP_LEVEL_EXCEPTION_FILTER prev_filter;
    HANDLE             process_handle;
    bool               symbols_initialized;
#else
    void*              altstack_mem;
    size_t             altstack_size;
    struct sigaction   old_sigaction_segv;
    struct sigaction   old_sigaction_fpe;
    struct sigaction   old_sigaction_ill;
    struct sigaction   old_sigaction_abrt;
    struct sigaction   old_sigaction_bus;
    struct sigaction   old_sigaction_trap;
#endif
} colunwind_runtime_t;

extern colunwind_runtime_t g_colunwind_runtime;

#endif /* COLUNWIND_INTERNAL_H */
