# Root CMakeLists.txt
cmake_minimum_required(VERSION 3.20)

project(colunwind
    VERSION 1.0.0
    DESCRIPTION "Cross-platform lightweight crash capture and line-column symbolization library"
    LANGUAGES C
)

# 编译与构建选项
option(COLUNWIND_BUILD_SHARED "Build colunwind as a dynamic library (.dll/.so)" OFF)
option(BUILD_TESTING "Build the testing suite" ON)
option(COLUNWIND_BUILD_EXAMPLES "Build example application" ON)

# 库类型判定 (默认 STATIC, 可切换 SHARED)
if(COLUNWIND_BUILD_SHARED)
    set(COLUNWIND_LIB_TYPE SHARED)
else()
    set(COLUNWIND_LIB_TYPE STATIC)
endif()

# 收集源码列表
set(COLUNWIND_SOURCES
    src/capture.c
    src/unwind.c
    src/symbolizer.c
    src/dumper.c
    src/arch/arch_x86_64.c
    src/arch/arch_aarch64.c
)

if(WIN32)
    list(APPEND COLUNWIND_SOURCES src/os/os_windows.c)
else()
    list(APPEND COLUNWIND_SOURCES src/os/os_posix.c)
endif()

# 声明核心库目标
add_library(colunwind ${COLUNWIND_LIB_TYPE} ${COLUNWIND_SOURCES})

# 语言标准
target_compile_features(colunwind PUBLIC c_std_99)

# 包含路径配置 (严格区分构建期与安装期接口)
target_include_directories(colunwind
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 动态库导出符号配置
if(COLUNWIND_BUILD_SHARED)
    target_compile_definitions(colunwind
        PUBLIC
            COLUNWIND_BUILD_SHARED
        PRIVATE
            COLUNWIND_EXPORTS
    )
endif()

# 平台专属依赖与编译标志配置
if(WIN32)
    target_link_libraries(colunwind PRIVATE dbghelp kernel32)
    if(MSVC)
        target_compile_options(colunwind PRIVATE /W4 /Zi /utf-8)
    endif()
else()
    target_link_libraries(colunwind PRIVATE dl pthread)
    target_compile_options(colunwind PRIVATE -Wall -Wextra -g)
endif()

# 单元测试子目录
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(test)
endif()

# 示例程序
if(COLUNWIND_BUILD_EXAMPLES)
    add_executable(simple_app examples/simple_app.c)
    target_link_libraries(simple_app PRIVATE colunwind)
    if(MSVC)
        target_compile_options(simple_app PRIVATE /Zi /utf-8)
        target_link_options(simple_app PRIVATE /DEBUG)
    else()
        target_compile_options(simple_app PRIVATE -g -O0)
    endif()
endif()

# 安装目标定义
include(GNUInstallDirs)
install(TARGETS colunwind
    EXPORT colunwind-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/colunwind
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
