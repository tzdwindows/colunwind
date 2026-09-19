#include <colunwind/colunwind.h>
#include <stdio.h>
#include <stdlib.h>

static void custom_crash_reporter(const colunwind_crash_context_t* context, void* user_data) {
    (void)user_data;
    printf("[simple_app] Custom Crash Callback triggered!\n");
    printf("[simple_app] Crash Type: %s\n", colunwind_crash_type_to_string(context->crash_type));
    printf("[simple_app] Fault Address: 0x%p\n", (void*)context->fault_address);
    printf("[simple_app] Total Stack Frames: %u\n", context->backtrace.frame_count);
}

static void inner_computation(void) {
    printf("[simple_app] Capturing manual stack backtrace...\n");

    colunwind_backtrace_t trace;
    colunwind_status_t status = colunwind_backtrace_capture(&trace, 0);
    if (status != COLUNWIND_SUCCESS) {
        printf("[simple_app] Failed to capture backtrace: %s\n", colunwind_status_to_string(status));
        return;
    }

    colunwind_symbolize(&trace);
    printf("[simple_app] Successfully unwound stack backtrace:\n");
    colunwind_print_backtrace(&trace);
}

int main(int argc, char* argv[]) {
    printf("=== colunwind simple_app demo ===\n");

    colunwind_config_t config;
    colunwind_config_init(&config);
    config.on_crash = custom_crash_reporter;
    config.enable_auto_dump = true;

    colunwind_status_t status = colunwind_init(&config);
    if (status != COLUNWIND_SUCCESS) {
        fprintf(stderr, "Failed to initialize colunwind: %d\n", status);
        return 1;
    }

    status = colunwind_install_crash_handler();
    if (status != COLUNWIND_SUCCESS) {
        fprintf(stderr, "Failed to install crash handler: %d\n", status);
        colunwind_shutdown();
        return 1;
    }

    printf("[simple_app] Crash handler successfully installed.\n");

    /* 运行业务逻辑并捕获栈展开 */
    inner_computation();

    if (argc > 1 && strcmp(argv[1], "--simulate-crash") == 0) {
        printf("[simple_app] Simulating crash dereference...\n");
        volatile int* null_ptr = NULL;
        *null_ptr = 123; /* 触发崩溃 */
    }

    colunwind_uninstall_crash_handler();
    colunwind_shutdown();

    printf("[simple_app] Normal exit. Cleaned up resources.\n");
    return 0;
}
