/*
 * AethelOS 预处理器 - Unix 头文件黑名单拦截器
 * ================================================================
 * [TODO-03] 实现头文件黑名单检查
 * 在任何 #include 指令中检测并罢工禁忌头文件
 * ================================================================
 */

#ifndef AEC_PREPROCESSOR_H
#define AEC_PREPROCESSOR_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 预处理器上下文结构 */
typedef struct {
    const char *source_file;     /* 当前处理的源文件 */
    const char *current_line;    /* 当前行内容 */
    int line_number;
    int column;
} PreprocessorContext;

/* 预处理器错误处理 */
typedef struct {
    char error_message[512];
    int is_fatal;
    int line;
    int column;
} PreprocessorError;

/* 
 * 扫描源代码并检测 #include 指令
 * 返回：0 如果通过检查，>0 如果发现禁忌
 */
int preprocessor_scan_for_forbidden_includes(const char *source_code);

/*
 * 检查单个 include 行
 * 返回：0 如果合法，>0 如果违规
 */
int preprocessor_check_include_line(const char *line, int line_number);

/* 
 * 从 #include 语句中提取头文件路径
 * 支持 #include <file.h> 和 #include "file.h"
 */
char* preprocessor_extract_include_path(const char *include_directive);

#endif /* AEC_PREPROCESSOR_H */
