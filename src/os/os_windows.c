#ifdef _WIN32

#include "os/os.h"
#include "internal.h"
#include <windows.h>

void* colunwind_os_alloc_pages(size_t size) {
    return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}

void colunwind_os_free_pages(void* ptr, size_t size) {
    (void)size;
    if (ptr) {
        VirtualFree(ptr, 0, MEM_RELEASE);
    }
}

void colunwind_os_write_stderr(const char* data, size_t len) {
    if (!data || len == 0) return;
    HANDLE hStdErr = GetStdHandle(STD_ERROR_HANDLE);
    if (hStdErr != NULL && hStdErr != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(hStdErr, data, (DWORD)len, &written, NULL);
    }
}

intptr_t colunwind_os_open_write(const char* path) {
    if (!path) return (intptr_t)INVALID_HANDLE_VALUE;
    HANDLE hFile = CreateFileA(path,
                               GENERIC_WRITE,
                               FILE_SHARE_READ,
                               NULL,
                               CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL,
                               NULL);
    return (intptr_t)hFile;
}

bool colunwind_os_write(intptr_t fd, const void* data, size_t len) {
    HANDLE hFile = (HANDLE)fd;
    if (hFile == INVALID_HANDLE_VALUE || !data || len == 0) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(hFile, data, (DWORD)len, &written, NULL);
    return (ok && written == (DWORD)len);
}

void colunwind_os_close(intptr_t fd) {
    HANDLE hFile = (HANDLE)fd;
    if (hFile != INVALID_HANDLE_VALUE && hFile != NULL) {
        CloseHandle(hFile);
    }
}

uint64_t colunwind_os_get_thread_id(void) {
    return (uint64_t)GetCurrentThreadId();
}

void colunwind_os_get_timestamp(char* buf, size_t buf_len) {
    if (!buf || buf_len == 0) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    colunwind_safe_snprintf(buf, buf_len, "%04u%02u%02u_%02u%02u%02u",
                            (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
                            (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond);
}

bool colunwind_os_get_module_info(uintptr_t addr, char* out_mod_name, size_t mod_name_len, uintptr_t* out_base) {
    if (out_mod_name && mod_name_len > 0) {
        out_mod_name[0] = '\0';
    }
    if (out_base) {
        *out_base = 0;
    }

    HMODULE hModule = NULL;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)addr,
                            &hModule)) {
        return false;
    }

    if (out_base) {
        *out_base = (uintptr_t)hModule;
    }

    if (out_mod_name && mod_name_len > 0) {
        char full_path[MAX_PATH];
        DWORD ret = GetModuleFileNameA(hModule, full_path, MAX_PATH);
        if (ret > 0 && ret < MAX_PATH) {
            /* 截取文件名 */
            const char* p = full_path + ret;
            while (p > full_path && *(p - 1) != '\\' && *(p - 1) != '/') {
                p--;
            }
            colunwind_safe_strncpy(out_mod_name, p, mod_name_len);
        } else {
            colunwind_safe_strncpy(out_mod_name, "unknown", mod_name_len);
        }
    }
    return true;
}

uint32_t colunwind_os_extract_column_and_line(const char* file_path,
                                              uint32_t line_target,
                                              const char* symbol_name,
                                              char* out_line,
                                              size_t line_max_len) {
    if (out_line && line_max_len > 0) {
        out_line[0] = '\0';
    }
    if (!file_path || line_target == 0) return 1;

    HANDLE hFile = CreateFileA(file_path,
                               GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL,
                               OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL,
                               NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return 1;
    }

    char buf[1024];
    DWORD bytes_read = 0;
    uint32_t current_line = 1;
    char line_buf[512];
    size_t line_idx = 0;
    bool found_line = false;

    while (ReadFile(hFile, buf, sizeof(buf), &bytes_read, NULL) && bytes_read > 0) {
        for (DWORD i = 0; i < bytes_read; ++i) {
            char c = buf[i];
            if (c == '\r') continue;
            if (c == '\n') {
                if (current_line == line_target) {
                    line_buf[line_idx] = '\0';
                    found_line = true;
                    break;
                }
                current_line++;
                line_idx = 0;
            } else {
                if (current_line == line_target && line_idx + 1 < sizeof(line_buf)) {
                    line_buf[line_idx++] = c;
                }
            }
        }
        if (found_line || current_line > line_target) break;
    }
    CloseHandle(hFile);

    if (!found_line && current_line == line_target && line_idx > 0) {
        line_buf[line_idx] = '\0';
        found_line = true;
    }

    if (!found_line) return 1;

    /* 拷贝提取到的源码行 */
    if (out_line && line_max_len > 0) {
        /* 修剪前导空格供显示 */
        const char* p = line_buf;
        while (*p == ' ' || *p == '\t') p++;
        colunwind_safe_strncpy(out_line, p, line_max_len);
    }

    /* 查找 symbol_name 在该行中的位置 */
    if (symbol_name && symbol_name[0] != '\0') {
        size_t sym_len = colunwind_safe_strlen(symbol_name);
        size_t line_len = colunwind_safe_strlen(line_buf);
        if (line_len >= sym_len) {
            for (size_t i = 0; i <= line_len - sym_len; ++i) {
                bool match = true;
                for (size_t j = 0; j < sym_len; ++j) {
                    if (line_buf[i + j] != symbol_name[j]) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    return (uint32_t)(i + 1); /* 1-based 列号 */
                }
            }
        }
    }

    /* 若未找到符号，定位到该行第一个非空白字符 */
    for (size_t i = 0; line_buf[i] != '\0'; ++i) {
        if (line_buf[i] != ' ' && line_buf[i] != '\t') {
            return (uint32_t)(i + 1);
        }
    }

    return 1;
}

uint32_t colunwind_os_extract_column_from_source(const char* file_path, uint32_t line_target, const char* symbol_name) {
    return colunwind_os_extract_column_and_line(file_path, line_target, symbol_name, NULL, 0);
}

bool colunwind_os_get_source_snippet(const char* file_path,
                                     uint32_t line_target,
                                     uint32_t column_target,
                                     uint32_t context_lines,
                                     char* out_buf,
                                     size_t out_buf_len) {
    if (!out_buf || out_buf_len == 0) return false;
    out_buf[0] = '\0';
    if (!file_path || line_target == 0) return false;

    HANDLE hFile = CreateFileA(file_path,
                               GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL,
                               OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL,
                               NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    uint32_t start_line = (line_target > context_lines) ? (line_target - context_lines) : 1;
    uint32_t end_line = line_target + context_lines;

    char buf[1024];
    DWORD bytes_read = 0;
    uint32_t cur_line = 1;
    char current_line_buf[512];
    size_t line_idx = 0;
    char snippet_temp[256];

    while (ReadFile(hFile, buf, sizeof(buf), &bytes_read, NULL) && bytes_read > 0) {
        for (DWORD i = 0; i < bytes_read; ++i) {
            char c = buf[i];
            if (c == '\r') continue;
            if (c == '\n') {
                if (cur_line >= start_line && cur_line <= end_line) {
                    current_line_buf[line_idx] = '\0';
                    bool is_target = (cur_line == line_target);
                    colunwind_safe_snprintf(snippet_temp, sizeof(snippet_temp),
                                            "    %c %4u | %s\n",
                                            is_target ? '>' : ' ',
                                            (unsigned)cur_line,
                                            current_line_buf);
                    colunwind_safe_strcat(out_buf, snippet_temp, out_buf_len);

                    /* 在目标行下输出列号指示符 */
                    if (is_target && column_target > 0) {
                        char caret_line[256];
                        colunwind_safe_strncpy(caret_line, "         | ", sizeof(caret_line));
                        for (uint32_t k = 1; k < column_target && k < 120; ++k) {
                            colunwind_safe_strcat(caret_line, " ", sizeof(caret_line));
                        }
                        colunwind_safe_strcat(caret_line, "^\n", sizeof(caret_line));
                        colunwind_safe_strcat(out_buf, caret_line, out_buf_len);
                    }
                }
                cur_line++;
                line_idx = 0;
                if (cur_line > end_line) break;
            } else {
                if (cur_line >= start_line && cur_line <= end_line && line_idx + 1 < sizeof(current_line_buf)) {
                    current_line_buf[line_idx++] = c;
                }
            }
        }
        if (cur_line > end_line) break;
    }
    CloseHandle(hFile);

    /* 文件末尾最后一行无换行符的情形 */
    if (cur_line >= start_line && cur_line <= end_line && line_idx > 0) {
        current_line_buf[line_idx] = '\0';
        bool is_target = (cur_line == line_target);
        colunwind_safe_snprintf(snippet_temp, sizeof(snippet_temp),
                                "    %c %4u | %s\n",
                                is_target ? '>' : ' ',
                                (unsigned)cur_line,
                                current_line_buf);
        colunwind_safe_strcat(out_buf, snippet_temp, out_buf_len);
    }

    return (out_buf[0] != '\0');
}

#endif /* _WIN32 */
