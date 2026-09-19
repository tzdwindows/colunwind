# test/CMakeLists.txt
cmake_minimum_required(VERSION 3.20)

set(TEST_SOURCES
    test_crash_handler
    test_unwind
    test_line_column
    test_dump_writer
)

foreach(TEST_TARGET ${TEST_SOURCES})
    add_executable(${TEST_TARGET} ${TEST_TARGET}.c)
    
    # 严格链接已构建的 colunwind 库，严禁杂糅源码
    target_link_libraries(${TEST_TARGET} PRIVATE colunwind)

    # 启用完整的调试信息生成以支持符号及行列号精准映射
    if(MSVC)
        target_compile_options(${TEST_TARGET} PRIVATE /Zi /Od /utf-8)
        target_link_options(${TEST_TARGET} PRIVATE /DEBUG /INCREMENTAL:NO)
    else()
        target_compile_options(${TEST_TARGET} PRIVATE -g -O0 -fno-inline)
        if(CYGWIN OR MSYS)
            target_link_options(${TEST_TARGET} PRIVATE -Wl,--export-all-symbols)
        else()
            target_link_options(${TEST_TARGET} PRIVATE -Wl,--export-dynamic)
        endif()
    endif()

    add_test(NAME ${TEST_TARGET} COMMAND ${TEST_TARGET})
endforeach()
