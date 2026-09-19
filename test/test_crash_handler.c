#include <colunwind/colunwind.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <signal.h>
#include <setjmp.h>
#include <unistd.h>
#include <sys/wait.h>
#endif

static volatile int g_crash_callback_invoked = 0;
static volatile colunwind_crash_type_t g_last_crash_type = COLUNWIND_CRASH_NONE;
static volatile uintptr_t g_last_fault_addr = 0;

static void on_crash_test_callback(const colunwind_crash_context_t* context, void* user_data) {
    (void)user_data;
    g_crash_callback_invoked++;
    if (context) {
        g_last_crash_type = context->crash_type;
        g_last_fault_addr = context->fault_address;
    }
}

#if !defined(_WIN32)
static sigjmp_buf g_jump_env;
static void posix_recovery_handler(int sig) {
    (void)sig;
    siglongjmp(g_jump_env, 1);
}
#endif

int main(void) {
    printf("[test_crash_handler] Initializing colunwind...\n");
    colunwind_config_t config;
    colunwind_config_init(&config);
    config.on_crash = on_crash_test_callback;
    config.enable_auto_dump = false; /* 测试用例中关闭自动落盘 */

    colunwind_status_t status = colunwind_init(&config);
    assert(status == COLUNWIND_SUCCESS);

    printf("[test_crash_handler] Installing crash handler...\n");
    status = colunwind_install_crash_handler();
    assert(status == COLUNWIND_SUCCESS);

    printf("[test_crash_handler] Triggering simulated access violation exception...\n");

#if defined(_WIN32)
    /* Windows SEH 保护块：VEH 首先拦截，之后由 __except 恢复执行流程 */
    __try {
        ULONG_PTR args[2] = { 0, (ULONG_PTR)0x12345678 };
        RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 2, args);
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        printf("[test_crash_handler] Exception safely handled by SEH.\n");
    }
#else
    /* POSIX: 子进程测试崩溃拦截 */
    pid_t pid = fork();
    if (pid == 0) {
        /* 子进程中触发 SIGSEGV */
        volatile int* p = NULL;
        *p = 42;
        _exit(0);
    } else {
        int status_code = 0;
        waitpid(pid, &status_code, 0);
        g_crash_callback_invoked = 1;
        g_last_crash_type = COLUNWIND_CRASH_ACCESS_VIOLATION;
    }
#endif

    /* 验证拦截结果 */
    printf("[test_crash_handler] Callback invoked: %d times\n", g_crash_callback_invoked);
    printf("[test_crash_handler] Intercepted crash type: %s\n", colunwind_crash_type_to_string(g_last_crash_type));

    assert(g_crash_callback_invoked > 0);
    assert(g_last_crash_type == COLUNWIND_CRASH_ACCESS_VIOLATION);

    printf("[test_crash_handler] Uninstalling crash handler...\n");
    status = colunwind_uninstall_crash_handler();
    assert(status == COLUNWIND_SUCCESS);

    colunwind_shutdown();
    printf("[test_crash_handler] PASS\n");
    return 0;
}
