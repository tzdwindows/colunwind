#if !defined(_WIN32)

#define _GNU_SOURCE
#include "os/os.h"
#include "internal.h"
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>
#include <dlfcn.h>

void* colunwind_os_alloc_pages(size_t size) {
    void* ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) {
        return NULL;
    }
    return ptr;
}

void colunwind_os_free_pages(void* ptr, size_t size) {
    if (ptr && size > 0) {
        munmap(ptr, size);
    }
}

void colunwind_os_write_stderr(const char* data, size_t len) {
    if (!data || len == 0) return;
    ssize_t ret = write(STDERR_FILENO, data, len);
    (void)ret;
}

intptr_t colunwind_os_open_write(const char* path) {
    if (!path) return -1;
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    return (intptr_t)fd;
}

bool colunwind_os_write(intptr_t fd, const void* data, size_t len) {
    if (fd < 0 || !data || len == 0) return false;
    ssize_t ret = write((int)fd, data, len);
    return (ret >= 0 && (size_t)ret == len);
}

void colunwind_os_close(intptr_t fd) {
    if (fd >= 0) {
        close((int)fd);
    }
}

uint64_t colunwind_os_get_thread_id(void) {
    return (uint64_t)(uintptr_t)pthread_self();
}

void colunwind_os_get_timestamp(char* buf, size_t buf_len) {
    if (!buf || buf_len == 0) return;
    time_t t = time(NULL);
    struct tm tm_buf;
    struct tm* ptm = localtime_r(&t, &tm_buf);
    if (ptm) {
        colunwind_safe_snprintf(buf, buf_len, "%04d%02d%02d_%02d%02d%02d",
                                ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday,
                                ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
    } else {
        colunwind_safe_strncpy(buf, "unknown_time", buf_len);
    }
}

bool colunwind_os_get_module_info(uintptr_t addr, char* out_mod_name, size_t mod_name_len, uintptr_t* out_base) {
    if (out_mod_name && mod_name_len > 0) {
        out_mod_name[0] = '\0';
    }
    if (out_base) {
        *out_base = 0;
    }

    Dl_info info;
    if (dladdr((void*)addr, &info) != 0) {
        if (out_base) {
            *out_base = (uintptr_t)info.dli_fbase;
        }
        if (out_mod_name && mod_name_len > 0 && info.dli_fname) {
            const char* p = info.dli_fname;
            const char* slash = p;
            while (*p) {
                if (*p == '/') {
                    slash = p + 1;
                }
                p++;
            }
            colunwind_safe_strncpy(out_mod_name, slash, mod_name_len);
        }
        return true;
    }
    return false;
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
    int fd = open(file_path, O_RDONLY);
    if (fd < 0) return 1;

    char buf[1024];
    ssize_t bytes_read = 0;
    uint32_t current_line = 1;
    char line_buf[512];
    size_t line_idx = 0;
    bool found_line = false;

    while ((bytes_read = read(fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < bytes_read; ++i) {
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
    close(fd);

    if (!found_line && current_line == line_target && line_idx > 0) {
        line_buf[line_idx] = '\0';
        found_line = true;
    }

    if (!found_line) return 1;

    if (out_line && line_max_len > 0) {
        const char* p = line_buf;
        while (*p == ' ' || *p == '\t') p++;
        colunwind_safe_strncpy(out_line, p, line_max_len);
    }

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
                    return (uint32_t)(i + 1);
                }
            }
        }
    }

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

    int fd = open(file_path, O_RDONLY);
    if (fd < 0) return false;

    uint32_t start_line = (line_target > context_lines) ? (line_target - context_lines) : 1;
    uint32_t end_line = line_target + context_lines;

    char buf[1024];
    ssize_t bytes_read = 0;
    uint32_t cur_line = 1;
    char current_line_buf[512];
    size_t line_idx = 0;
    char snippet_temp[256];

    while ((bytes_read = read(fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < bytes_read; ++i) {
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
    close(fd);

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

#endif /* !_WIN32 */
