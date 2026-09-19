#include <colunwind/colunwind.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#if defined(_WIN32)
#include <windows.h>
#endif

int main(void) {
    colunwind_config_t cfg;
    colunwind_config_init(&cfg);
    colunwind_status_t status = colunwind_init(&cfg);
    assert(status == COLUNWIND_SUCCESS);

    const char* dump_filepath = "test_colunwind_dump.dmp";

    /* 清理旧文件 */
    remove(dump_filepath);

    /* 构造模拟崩溃上下文 */
    colunwind_crash_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.crash_type = COLUNWIND_CRASH_ACCESS_VIOLATION;
    ctx.fault_address = 0x00007FFF12345678ULL;
    colunwind_backtrace_capture(&ctx.backtrace, 0);

    printf("[test_dump_writer] Writing dump file to '%s'...\n", dump_filepath);
    status = colunwind_write_dump(dump_filepath, &ctx, COLUNWIND_DUMP_NORMAL);
    assert(status == COLUNWIND_SUCCESS);

    /* 检查文件是否存在且大小大于 0 */
    FILE* fp = fopen(dump_filepath, "rb");
    assert(fp != NULL);

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    assert(sz > 0);
    fseek(fp, 0, SEEK_SET);

    printf("[test_dump_writer] Generated dump file size: %ld bytes\n", sz);

#if defined(_WIN32)
    /* Windows Minidump 标志 'MDMP' (0x504d444d) */
    uint32_t magic = 0;
    size_t read_bytes = fread(&magic, 1, sizeof(magic), fp);
    assert(read_bytes == sizeof(magic));
    printf("[test_dump_writer] Minidump signature magic: 0x%08X (Expected 0x504D444D)\n", magic);
    assert(magic == 0x504D444D);
#else
    /* POSIX 紧凑转储魔数 'COLU' (0x434F4C55) */
    uint32_t magic = 0;
    size_t read_bytes = fread(&magic, 1, sizeof(magic), fp);
    assert(read_bytes == sizeof(magic));
    printf("[test_dump_writer] POSIX compact dump magic: 0x%08X (Expected 0x434F4C55)\n", magic);
    assert(magic == 0x434F4C55);
#endif

    fclose(fp);
    remove(dump_filepath);

    colunwind_shutdown();
    printf("[test_dump_writer] PASS\n");
    return 0;
}
