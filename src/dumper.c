#include "colunwind/colunwind.h"
#include "internal.h"
#include "os/os.h"

#if defined(_WIN32)
#include <windows.h>
#include <dbghelp.h>
#endif

/* 紧凑型 POSIX 转储文件魔数 'COLU' */
#define COLUNWIND_COMPACT_DUMP_MAGIC 0x434F4C55
#define COLUNWIND_COMPACT_DUMP_VERSION 1

typedef struct colunwind_compact_dump_header {
    uint32_t magic;
    uint32_t version;
    uint32_t crash_type;
    uint32_t frame_count;
    uint64_t fault_address;
    uint64_t thread_id;
    uint64_t timestamp;
} colunwind_compact_dump_header_t;

colunwind_status_t colunwind_write_dump(const char* dump_path, const colunwind_crash_context_t* crash_ctx, uint32_t dump_flags) {
    char default_path[512];
    const char* target_path = dump_path;

    if (!target_path) {
        char ts[64];
        colunwind_os_get_timestamp(ts, sizeof(ts));
        const char* dir = g_colunwind_runtime.config.dump_directory ? g_colunwind_runtime.config.dump_directory : ".";
        colunwind_safe_snprintf(default_path, sizeof(default_path), "%s/crash_%s.dmp", dir, ts);
        target_path = default_path;
    }

#if defined(_WIN32)
    HANDLE hFile = CreateFileA(target_path,
                               GENERIC_READ | GENERIC_WRITE,
                               0,
                               NULL,
                               CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL,
                               NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return COLUNWIND_ERROR_IO_FAILED;
    }

    MINIDUMP_TYPE mType = MiniDumpNormal;
    if (dump_flags & COLUNWIND_DUMP_WITH_FULL_MEMORY) {
        mType = (MINIDUMP_TYPE)(mType | MiniDumpWithFullMemory);
    }
    if (dump_flags & COLUNWIND_DUMP_WITH_THREAD_INFO) {
        mType = (MINIDUMP_TYPE)(mType | MiniDumpWithThreadInfo);
    }
    if (dump_flags & COLUNWIND_DUMP_WITH_UNWIND_TRACE) {
        mType = (MINIDUMP_TYPE)(mType | MiniDumpWithUnloadedModules);
    }

    MINIDUMP_EXCEPTION_INFORMATION mei;
    MINIDUMP_EXCEPTION_INFORMATION* pMei = NULL;

    if (crash_ctx && crash_ctx->native_context) {
        mei.ThreadId = (DWORD)crash_ctx->thread_id;
        mei.ExceptionPointers = (PEXCEPTION_POINTERS)crash_ctx->native_context;
        mei.ClientPointers = FALSE;
        pMei = &mei;
    }

    BOOL success = MiniDumpWriteDump(GetCurrentProcess(),
                                     GetCurrentProcessId(),
                                     hFile,
                                     mType,
                                     pMei,
                                     NULL,
                                     NULL);
    CloseHandle(hFile);

    return success ? COLUNWIND_SUCCESS : COLUNWIND_ERROR_DUMP_FAILED;

#else
    /* POSIX 紧凑转储格式写入 */
    intptr_t fd = colunwind_raw_open_file_write(target_path);
    if (fd < 0) {
        return COLUNWIND_ERROR_IO_FAILED;
    }

    colunwind_compact_dump_header_t hdr;
    hdr.magic = COLUNWIND_COMPACT_DUMP_MAGIC;
    hdr.version = COLUNWIND_COMPACT_DUMP_VERSION;
    hdr.crash_type = crash_ctx ? (uint32_t)crash_ctx->crash_type : 0;
    hdr.frame_count = crash_ctx ? crash_ctx->backtrace.frame_count : 0;
    hdr.fault_address = crash_ctx ? (uint64_t)crash_ctx->fault_address : 0;
    hdr.thread_id = crash_ctx ? crash_ctx->thread_id : colunwind_os_get_thread_id();
    hdr.timestamp = (uint64_t)time(NULL);

    colunwind_raw_write_file(fd, &hdr, sizeof(hdr));

    if (crash_ctx && hdr.frame_count > 0) {
        colunwind_raw_write_file(fd, crash_ctx->backtrace.frames, sizeof(colunwind_frame_t) * hdr.frame_count);
    }

    colunwind_raw_close_file(fd);
    return COLUNWIND_SUCCESS;
#endif
}
