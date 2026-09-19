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

uint32_t colunwind_os_extract_column_from_source(const char* file_path, uint32_t line_target, const char* symbol_name) {
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

    /* 逐行读取源码 */
    char buf[1024];
    DWORD bytes_read = 0;
    uint32_t current_line = 1;
    char line_buf[512];
    size_t line_idx = 0;
    bool found_line = false;

    while (ReadFile(hFile, buf, sizeof(buf), &bytes_read, NULL) && bytes_read > 0) {
        for (DWORD i = 0; i < bytes_read; ++i) {
            char c = buf[i];
            if (c == '\r') {
                continue;
            }
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
        if (found_line || current_line > line_target) {
            break;
        }
    }
    CloseHandle(hFile);

    if (!found_line && current_line == line_target && line_idx > 0) {
        line_buf[line_idx] = '\0';
        found_line = true;
    }

    if (!found_line) {
        return 1;
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

#endif /* _WIN32 */
