#include "colunwind/colunwind.h"
#include "internal.h"
#include "os/os.h"

#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#include <dbghelp.h>
#else
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdlib.h>
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

    /* 4. 精确列号定位解析 (结合源码映射与符号位置推算) */
    if (out_frame->line > 0 && out_frame->file_path[0] != '\0') {
        out_frame->column = colunwind_os_extract_column_from_source(out_frame->file_path,
                                                                    out_frame->line,
                                                                    out_frame->symbol_name);
    } else {
        out_frame->column = 0;
    }

    out_frame->file = out_frame->file_path;
    out_frame->function = out_frame->symbol_name;

    return COLUNWIND_SUCCESS;

#else
    /* POSIX: 基于 dladdr 与符号信息 */
    Dl_info info;
    if (dladdr((void*)address, &info) != 0) {
        if (info.dli_sname) {
            colunwind_safe_strncpy(out_frame->symbol_name, info.dli_sname, sizeof(out_frame->symbol_name));
            out_frame->offset = address - (uintptr_t)info.dli_saddr;
        } else {
            colunwind_safe_strncpy(out_frame->symbol_name, "???", sizeof(out_frame->symbol_name));
            out_frame->offset = 0;
        }
    } else {
        colunwind_safe_strncpy(out_frame->symbol_name, "???", sizeof(out_frame->symbol_name));
        out_frame->offset = 0;
    }

    /* 如果有内嵌 DWARF 行号信息，提取行列号 */
    if (out_frame->file_path[0] != '\0' && out_frame->line > 0) {
        out_frame->column = colunwind_os_extract_column_from_source(out_frame->file_path,
                                                                    out_frame->line,
                                                                    out_frame->symbol_name);
    } else {
        out_frame->line = 0;
        out_frame->column = 0;
    }

    out_frame->file = out_frame->file_path;
    out_frame->function = out_frame->symbol_name;

    return COLUNWIND_SUCCESS;
#endif
}

colunwind_status_t colunwind_symbolize(colunwind_backtrace_t* trace) {
    if (!trace) return COLUNWIND_ERROR_INVALID_ARGUMENT;

    for (uint32_t i = 0; i < trace->frame_count; ++i) {
        colunwind_frame_t* f = &trace->frames[i];
        colunwind_resolve_location(f->instruction_pointer, f);
    }

    return COLUNWIND_SUCCESS;
}
