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
static void test_unwind_frame_c(void) {
    colunwind_backtrace_t trace;
    memset(&trace, 0, sizeof(trace));

    colunwind_status_t status = colunwind_backtrace_capture(&trace, 0);
    assert(status == COLUNWIND_SUCCESS);
    assert(trace.frame_count > 0);

    status = colunwind_symbolize(&trace);
    assert(status == COLUNWIND_SUCCESS);

    printf("[test_unwind] Total frames unwound: %u\n", trace.frame_count);
    colunwind_print_backtrace(&trace);

    /* 验证展开出的栈中能够识别到当前测试函数或测试可执行文件 */
    bool found_expected = false;
    for (uint32_t i = 0; i < trace.frame_count; ++i) {
        if (strstr(trace.frames[i].symbol_name, "test_unwind_frame_c") != NULL ||
            strstr(trace.frames[i].symbol_name, "test_unwind_frame_b") != NULL ||
            strstr(trace.frames[i].symbol_name, "main") != NULL ||
            strstr(trace.frames[i].module_name, "test_unwind") != NULL) {
            found_expected = true;
            break;
        }
    }
    assert(found_expected);
}

#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__)
__attribute__((noinline))
#endif
static void test_unwind_frame_b(void) {
    test_unwind_frame_c();
}

#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__)
__attribute__((noinline))
#endif
static void test_unwind_frame_a(void) {
    test_unwind_frame_b();
}

#if defined(_MSC_VER)
#pragma optimize("", on)
#endif

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    colunwind_config_t cfg;
    colunwind_config_init(&cfg);
    colunwind_status_t status = colunwind_init(&cfg);
    assert(status == COLUNWIND_SUCCESS);

    printf("[test_unwind] Starting multi-level stack unwind test...\n");
    test_unwind_frame_a();

    colunwind_shutdown();
    printf("[test_unwind] PASS\n");
    return 0;
}
