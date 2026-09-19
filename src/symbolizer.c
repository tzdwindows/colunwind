#if !defined(_WIN32)
#define _GNU_SOURCE
#endif

#include "colunwind/colunwind.h"
#include "internal.h"
#include "os/os.h"

#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#include <dbghelp.h>
#else
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#endif

colunwind_status_t colunwind_resolve_location(uintptr_t address, colunwind_frame_t* out_frame) {
    if (!out_frame) return COLUNWIND_ERROR_INVALID_ARGUMENT;

    out_frame->instruction_pointer = address;
    out_frame->file = out_frame->file_path;
    out_frame->function = out_frame->symbol_name;

    /* 1. 模块信息解析 */
    colunwind_os_get_module_info(address, out_frame->module_name, sizeof(out_frame->module_name), &out_frame->module_base);

#if defined(_WIN32)
    HANDLE hProcess = GetCurrentProcess();

    /* 确保 DbgHelp 已初始化 */
    if (!g_colunwind_runtime.symbols_initialized) {
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
        SymInitialize(hProcess, search_path, TRUE);
        g_colunwind_runtime.symbols_initialized = true;
    }

    /* 2. 解包增量链接跳转桩 (ILT / JMP thunk) */
    const uint8_t* code_bytes = (const uint8_t*)address;
    if (code_bytes && code_bytes[0] == 0xE9) {
        int32_t rel_offset = *(const int32_t*)(code_bytes + 1);
        address = (uintptr_t)((intptr_t)address + 5 + rel_offset);
        out_frame->instruction_pointer = address;
    }

    /* 3. 符号 (函数名) 与偏移解析 (PDB 符号引擎) */
    char buffer[sizeof(SYMBOL_INFO) + COLUNWIND_MAX_NAME_LEN * sizeof(TCHAR)];
    PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)buffer;
    memset(buffer, 0, sizeof(buffer));
    pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    pSymbol->MaxNameLen = COLUNWIND_MAX_NAME_LEN;

    DWORD64 displacement = 0;
    if (SymFromAddr(hProcess, (DWORD64)address, &displacement, pSymbol)) {
        colunwind_safe_strncpy(out_frame->symbol_name, pSymbol->Name, sizeof(out_frame->symbol_name));
        out_frame->offset = (uintptr_t)displacement;
    } else {
        colunwind_safe_strncpy(out_frame->symbol_name, "???", sizeof(out_frame->symbol_name));
        out_frame->offset = 0;
    }

    /* 4. 源码文件与行号解析 (IMAGEHLP_LINE64) */
    IMAGEHLP_LINE64 line_info;
    memset(&line_info, 0, sizeof(IMAGEHLP_LINE64));
    line_info.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

    DWORD line_displacement = 0;
    BOOL line_found = SymGetLineFromAddr64(hProcess, (DWORD64)address, &line_displacement, &line_info);
    
    /* 若函数入口刚好处于序言 (Prologue) 处导致无行号映射，向后微探寻语句行 */
    if (!line_found) {
        static const uintptr_t probe_offsets[] = { 4, 8, 12, 16, 1, 2 };
        for (size_t i = 0; i < sizeof(probe_offsets) / sizeof(probe_offsets[0]); ++i) {
            line_found = SymGetLineFromAddr64(hProcess, (DWORD64)(address + probe_offsets[i]), &line_displacement, &line_info);
            if (line_found) break;
        }
    }

    if (line_found) {
        colunwind_safe_strncpy(out_frame->file_path, line_info.FileName, sizeof(out_frame->file_path));
        out_frame->line = line_info.LineNumber;
    } else {
        out_frame->file_path[0] = '\0';
        out_frame->line = 0;
    }

    /* 5. 精确列号定位与源码语句/段落提取 (哪一段代码) */
    if (out_frame->line > 0 && out_frame->file_path[0] != '\0') {
        out_frame->column = colunwind_os_extract_column_and_line(out_frame->file_path,
                                                                out_frame->line,
                                                                out_frame->symbol_name,
                                                                out_frame->source_line,
                                                                sizeof(out_frame->source_line));
        /* 提取上下文代码段落 (前后各 2 行，带标记和列指针) */
        colunwind_os_get_source_snippet(out_frame->file_path,
                                        out_frame->line,
                                        out_frame->column,
                                        2,
                                        out_frame->source_snippet,
                                        sizeof(out_frame->source_snippet));
    } else {
        out_frame->column = 0;
        out_frame->source_line[0] = '\0';
        out_frame->source_snippet[0] = '\0';
    }

    out_frame->file = out_frame->file_path;
    out_frame->function = out_frame->symbol_name;

    return COLUNWIND_SUCCESS;

#else
    /* POSIX: 基于 dladdr 与 addr2line / 符号信息 */
    Dl_info info;
    memset(&info, 0, sizeof(info));
    if (dladdr((void*)address, &info) != 0) {
        if (info.dli_sname && info.dli_sname[0] != '\0') {
            colunwind_safe_strncpy(out_frame->symbol_name, info.dli_sname, sizeof(out_frame->symbol_name));
            out_frame->offset = address - (uintptr_t)info.dli_saddr;
        } else {
            colunwind_safe_strncpy(out_frame->symbol_name, "???", sizeof(out_frame->symbol_name));
            out_frame->offset = 0;
        }
        if (info.dli_fname && info.dli_fname[0] != '\0') {
            const char* slash = strrchr(info.dli_fname, '/');
            const char* mod_name = slash ? (slash + 1) : info.dli_fname;
            colunwind_safe_strncpy(out_frame->module_name, mod_name, sizeof(out_frame->module_name));
        }
    } else {
        colunwind_safe_strncpy(out_frame->symbol_name, "???", sizeof(out_frame->symbol_name));
        out_frame->offset = 0;
    }

    /* 尝试使用 addr2line 获取源码文件与行号 */
    const char* binary_path = (info.dli_fname && info.dli_fname[0] != '\0') ? info.dli_fname : "/proc/self/exe";
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "addr2line -e \"%s\" -f -C 0x%lx 2>/dev/null",
             binary_path, (unsigned long)address);
    FILE* a2l_pipe = popen(cmd, "r");
    if (a2l_pipe) {
        char fn_line[256];
        char loc_line[512];
        if (fgets(fn_line, sizeof(fn_line), a2l_pipe)) {
            size_t flen = colunwind_safe_strlen(fn_line);
            while (flen > 0 && (fn_line[flen - 1] == '\r' || fn_line[flen - 1] == '\n')) {
                fn_line[--flen] = '\0';
            }
            if (flen > 0 && strcmp(fn_line, "??") != 0) {
                colunwind_safe_strncpy(out_frame->symbol_name, fn_line, sizeof(out_frame->symbol_name));
            }
        }
        if (fgets(loc_line, sizeof(loc_line), a2l_pipe)) {
            size_t llen = colunwind_safe_strlen(loc_line);
            while (llen > 0 && (loc_line[llen - 1] == '\r' || loc_line[llen - 1] == '\n')) {
                loc_line[--llen] = '\0';
            }
            char* colon = strrchr(loc_line, ':');
            if (colon) {
                *colon = '\0';
                uint32_t line_no = (uint32_t)atoi(colon + 1);
                if (line_no > 0 && strcmp(loc_line, "??") != 0) {
                    colunwind_safe_strncpy(out_frame->file_path, loc_line, sizeof(out_frame->file_path));
                    out_frame->line = line_no;
                }
            }
        }
        pclose(a2l_pipe);
    }

    /* 如果有内嵌 DWARF 行号信息，提取行列号与源码 */
    if (out_frame->file_path[0] != '\0' && out_frame->line > 0) {
        out_frame->column = colunwind_os_extract_column_and_line(out_frame->file_path,
                                                                out_frame->line,
                                                                out_frame->symbol_name,
                                                                out_frame->source_line,
                                                                sizeof(out_frame->source_line));
        colunwind_os_get_source_snippet(out_frame->file_path,
                                        out_frame->line,
                                        out_frame->column,
                                        2,
                                        out_frame->source_snippet,
                                        sizeof(out_frame->source_snippet));
    } else {
        out_frame->line = 0;
        out_frame->column = 0;
        out_frame->source_line[0] = '\0';
        out_frame->source_snippet[0] = '\0';
    }

    out_frame->file = out_frame->file_path;
    out_frame->function = out_frame->symbol_name;

    return COLUNWIND_SUCCESS;
#endif
}

colunwind_status_t colunwind_get_source_snippet(const char* file_path,
                                                uint32_t line,
                                                uint32_t column,
                                                uint32_t context_lines,
                                                char* out_buf,
                                                size_t out_buf_size) {
    if (!file_path || line == 0 || !out_buf || out_buf_size == 0) {
        return COLUNWIND_ERROR_INVALID_ARGUMENT;
    }
    bool ok = colunwind_os_get_source_snippet(file_path, line, column, context_lines, out_buf, out_buf_size);
    return ok ? COLUNWIND_SUCCESS : COLUNWIND_ERROR_IO_FAILED;
}

colunwind_status_t colunwind_symbolize(colunwind_backtrace_t* trace) {
    if (!trace) return COLUNWIND_ERROR_INVALID_ARGUMENT;

    for (uint32_t i = 0; i < trace->frame_count; ++i) {
        colunwind_frame_t* f = &trace->frames[i];
        colunwind_resolve_location(f->instruction_pointer, f);
    }

    return COLUNWIND_SUCCESS;
}
