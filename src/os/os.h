#ifndef COLUNWIND_OS_H
#define COLUNWIND_OS_H

#include "colunwind/types.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief 分配底层内存页 (不经过标准 malloc)
 */
void* colunwind_os_alloc_pages(size_t size);

/**
 * @brief 释放底层内存页
 */
void colunwind_os_free_pages(void* ptr, size_t size);

/**
 * @brief 异步信号安全地向标准错误输出字节流
 */
void colunwind_os_write_stderr(const char* data, size_t len);

/**
 * @brief 打开/创建写入文件 (用于转储或日志)
 */
intptr_t colunwind_os_open_write(const char* path);

/**
 * @brief 写入文件数据
 */
bool colunwind_os_write(intptr_t fd, const void* data, size_t len);

/**
 * @brief 关闭文件句柄
 */
void colunwind_os_close(intptr_t fd);

/**
 * @brief 获取当前线程 ID
 */
uint64_t colunwind_os_get_thread_id(void);

/**
 * @brief 获取当前时间戳格式化字符串 (如 "20260919_081500")
 */
void colunwind_os_get_timestamp(char* buf, size_t buf_len);

/**
 * @brief 根据内存指令地址获取包含该地址的模块基址与模块名称
 */
bool colunwind_os_get_module_info(uintptr_t addr, char* out_mod_name, size_t mod_name_len, uintptr_t* out_base);

/**
 * @brief 从源文件中安全读取指定行号的文本，推断符号的起始列号并拷贝该行代码
 * @param file_path 源码文件路径
 * @param line_target 目标行号 (1-based)
 * @param symbol_name 目标符号名 (可为 NULL)
 * @param out_line 接收该行修剪后代码文本 (可为 NULL)
 * @param line_max_len 接收缓冲长度
 * @return 推导出的列号 (1-based)，若未找到或读取失败返回 1
 */
uint32_t colunwind_os_extract_column_from_source(const char* file_path, uint32_t line_target, const char* symbol_name);

uint32_t colunwind_os_extract_column_and_line(const char* file_path,
                                              uint32_t line_target,
                                              const char* symbol_name,
                                              char* out_line,
                                              size_t line_max_len);

/**
 * @brief 从源文件中提取包含目标行及前后上下文的源码段落文本 (含哪一段代码、指示符与列指针)
 */
bool colunwind_os_get_source_snippet(const char* file_path,
                                     uint32_t line_target,
                                     uint32_t column_target,
                                     uint32_t context_lines,
                                     char* out_buf,
                                     size_t out_buf_len);

#endif /* COLUNWIND_OS_H */
