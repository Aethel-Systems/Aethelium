/*
 * Copyright (C) 2024-2026 Aethel-Systems. All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * AethelOS 预处理器实现
 * ================================================================
 */

#include "preprocessor.h"
#include "lexer/unix_strike.h"
#include <ctype.h>

/* 禁忌头文件黑名单 (与unix_strike.c中同步) */
static const char *forbidden_includes[] = {
    "stdio.h",
    "unistd.h", 
    "stdlib.h",
    "pthread.h",
    "errno.h",
    "fcntl.h",
    "sys/types.h",
    "sys/stat.h",
    "sys/time.h",
    "sys/socket.h",
    "sys/wait.h",
    "signal.h",
    "time.h",
    "dirent.h",
    "pwd.h",
    "grp.h",
    "termios.h",
    "termio.h",
    "sgtty.h",
    "pty.h",
    "utmp.h",
    "wtmp.h",
    "syslog.h",
    "arpa/inet.h",
    "netinet/in.h",
    "netdb.h",
    NULL
};

/*
 * 从 #include 指令中提取头文件名
 */
char* preprocessor_extract_include_path(const char *include_directive) {
    if (!include_directive) return NULL;
    
    /* 找到 < 或 " */
    const char *start = strchr(include_directive, '<');
    char delimiter = '>';
    
    if (!start) {
        start = strchr(include_directive, '"');
        delimiter = '"';
    }
    
    if (!start) return NULL;
    
    start++;  /* 跳过开启符 */
    
    /* 找到关闭符 */
    const char *end = strchr(start, delimiter);
    if (!end) return NULL;
    
    int len = end - start;
    char *result = malloc(len + 1);
    strncpy(result, start, len);
    result[len] = '\0';
    
    return result;
}

/*
 * 检查单个 include 行
 */
int preprocessor_check_include_line(const char *line, int line_number) {
    if (!line) return 0;
    
    /* 跳过前导空白 */
    while (*line && isspace(*line)) line++;
    
    /* 检查 #include */
    if (strncmp(line, "#include", 8) != 0) {
        return 0;
    }
    
    char *include_path = preprocessor_extract_include_path(line);
    if (!include_path) {
        return 0;
    }
    
    /* 检查禁忌列表 */
    for (int i = 0; forbidden_includes[i] != NULL; i++) {
        if (strstr(include_path, forbidden_includes[i]) != NULL) {
            fprintf(stderr, 
                "\n================================================================================\n"
                "[COMPILER STRIKE] 预处理器阻截\n"
                "================================================================================\n"
                "文件: %s (第 %d 行)\n"
                "检测到禁卷轴: <%s>\n"
                "================================================================================\n\n",
                "unknown", line_number, include_path);
            
            /* 调用unix_strike的功能 */
            strike_detect_forbidden_include(include_path);
            
            free(include_path);
            return 1;
        }
    }
    
    free(include_path);
    return 0;
}

/*
 * 扫描整个源代码
 */
int preprocessor_scan_for_forbidden_includes(const char *source_code) {
    if (!source_code) return 0;
    
    int violation_count = 0;
    int line_number = 1;
    const char *current = source_code;
    const char *line_start = source_code;
    
    while (*current != '\0') {
        if (*current == '\n') {
            /* 提取本行并检查 */
            int line_len = current - line_start;
            char *line = malloc(line_len + 1);
            strncpy(line, line_start, line_len);
            line[line_len] = '\0';
            
            if (preprocessor_check_include_line(line, line_number)) {
                violation_count++;
            }
            
            free(line);
            current++;
            line_start = current;
            line_number++;
        } else {
            current++;
        }
    }
    
    /* 检查最后一行 */
    if (current != line_start) {
        int line_len = current - line_start;
        char *line = malloc(line_len + 1);
        strncpy(line, line_start, line_len);
        line[line_len] = '\0';
        
        if (preprocessor_check_include_line(line, line_number)) {
            violation_count++;
        }
        
        free(line);
    }
    
    return violation_count;
}
