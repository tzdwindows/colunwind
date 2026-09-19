#include <colunwind/colunwind.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#if defined(_MSC_VER)
#pragma optimize("", off)
__declspec(noinline)
#elif defined(__GNUC__)
__attribute__((noinline))
#endif
void colunwind_test_target_function(void) {
    volatile int dummy_val = 0xABCD;
    (void)dummy_val;
}

#if defined(_MSC_VER)
#pragma optimize("", on)
#endif

int main(void) {
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);
    colunwind_config_t cfg;
    colunwind_config_init(&cfg);
    colunwind_status_t status = colunwind_init(&cfg);
    assert(status == COLUNWIND_SUCCESS);

    colunwind_frame_t frame;
    memset(&frame, 0, sizeof(frame));

    uintptr_t func_addr = (uintptr_t)&colunwind_test_target_function;
    printf("[test_line_column] Resolving location for address: 0x%p\n", (void*)func_addr);

    status = colunwind_resolve_location(func_addr, &frame);
    assert(status == COLUNWIND_SUCCESS);

    printf("[test_line_column] Module:   %s\n", frame.module_name);
    printf("[test_line_column] Function: %s (ptr: %s)\n", frame.symbol_name, frame.function ? frame.function : "(null)");
    printf("[test_line_column] File:     %s (ptr: %s)\n", frame.file_path, frame.file ? frame.file : "(null)");
    printf("[test_line_column] Line:     %u\n", (unsigned)frame.line);
    printf("[test_line_column] Column:   %u\n", (unsigned)frame.column);

    /* 验证精度规范：显式包含 file, line, column, function */
    assert(frame.file != NULL);
    assert(frame.function != NULL);
    assert(frame.line > 0);
    assert(frame.column > 0);

    /* 验证符号名与文件名匹配 */
    assert(strstr(frame.function, "colunwind_test_target_function") != NULL);
    assert(strstr(frame.file, "test_line_column") != NULL);

    printf("[test_line_column] Source Line: %s\n", frame.source_line);
    printf("[test_line_column] Snippet:\n%s\n", frame.source_snippet);

    assert(frame.source_line[0] != '\0');
    assert(frame.source_snippet[0] != '\0');

    /* 验证显式源码位置结构体 colunwind_source_location_t */
    colunwind_source_location_t loc;
    loc.file = frame.file;
    loc.line = frame.line;
    loc.column = frame.column;
    loc.function = frame.function;
    loc.source_line = frame.source_line;

    assert(loc.line == frame.line);
    assert(loc.column == frame.column);
    assert(strcmp(loc.file, frame.file) == 0);
    assert(strcmp(loc.function, frame.function) == 0);
    assert(strcmp(loc.source_line, frame.source_line) == 0);

    colunwind_shutdown();
    printf("[test_line_column] PASS\n");
    return 0;
}
