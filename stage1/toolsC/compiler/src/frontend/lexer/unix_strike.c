/*
 * AethelOS 范式清洗 - Unix 特征罢工检测实现
 * ================================================================
 */

#include "unix_strike.h"
#include <ctype.h>
#include <stdarg.h>

static int ascii_lower_cmp(const char *a, const char *b) {
    size_t i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i]) {
        char ca = (char)tolower((unsigned char)a[i]);
        char cb = (char)tolower((unsigned char)b[i]);
        if (ca != cb) return 0;
        i++;
    }
    return a[i] == '\0' && b[i] == '\0';
}

/* 禁忌头文件黑名单 */
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
    NULL
};

/* 禁咒符号清单 */
static const char *forbidden_symbols[] = {
    /* 进程分身 */
    "fork", "vfork", "clone", "wait", "waitpid", "wait4", "waitid",
    
    /* 镜像替换 */
    "exec", "execl", "execv", "execle", "execve", "execlp", "execvp", 
    "fexecve", "execution",
    
    /* 挂载逻辑 */
    "mount", "umount", "fstab", "mtab", "autofs", "mountpoint", 
    "vfs_mount",
    
    /* 标准流 */
    "stdin", "stdout", "stderr", "standard_in", "standard_out", 
    "standard_error",
    
    /* 标准 I/O 函数 */
    "printf", "scanf", "putchar", "getchar", "puts", "gets", 
    "fgets", "fputs", "fread", "fwrite", "fopen", "fclose", 
    "fseek", "ftell", "rewind", "fprintf", "fscanf", "vprintf", 
    "vfprintf", "sprintf", "snprintf", "vsprintf", "vsnprintf",
    
    /* 文件操作 */
    "open", "creat", "close", "read", "write", "lseek", "pread", 
    "pwrite", "mmap", "munmap", "msync", "fsync", "fdatasync", 
    "ioctl", "fcntl", "flock",
    
    /* 目录操作 */
    "mkdir", "rmdir", "chdir", "fchdir", "getcwd", "get_current_dir_name",
    "opendir", "readdir", "closedir", "rewinddir", "telldir", "seekdir",
    
    /* 内存管理 */
    "malloc", "calloc", "realloc", "free", "brk", "sbrk", "alloca",
    
    /* 权限与属性 */
    "chmod", "fchmod", "chown", "fchown", "lchown", "stat", "fstat",
    "lstat", "utime", "utimes", "access",
    
    /* 环境/时间 */
    "getenv", "setenv", "putenv", "unsetenv", "time", "gettimeofday",
    "clock_gettime", "settimeofday", "adjtime", "tzset",
    
    /* 错误处理 */
    "errno", "perror", "strerror", "strerror_r",
    
    /* 进程控制 */
    "exit", "_exit", "abort", "atexit", "getpid", "getppid", "getpgrp",
    "getpgid", "setpgid", "setsid",
    
    /* 特权用户 */
    "setuid", "setgid", "seteuid", "setegid", "root", "sudo",
    "superuser", "privilege_escalation",
    
    /* 用户/组 */
    "uid", "gid", "euid", "egid", "passwd", "group", "getuid", "getgid",
    
    /* 匿名管道 */
    "pipe", "pipe2", "mkfifo", "fifo",
    
    /* 异步信号 */
    "signal", "sigaction", "sigprocmask", "sigsuspend", "sigpending",
    "kill", "raise", "alarm", "SIGHUP", "SIGINT", "SIGKILL", "SIGTERM",
    "SIGSTOP",
    
    /* 目录链接 */
    "link", "symlink", "hardlink", "readlink", "unlink",
    
    /* 传统套接字 */
    "socket", "af_inet", "af_unix", "sock_stream", "sock_dgram", "bind",
    "listen", "accept", "connect",
    
    NULL
};

/* 缩写词黑名单 */
static const char *forbidden_abbreviations[] = {
    "src", "inc", "ptr", "str", "cnt", "val", "len", "buf", "tmp",
    "idx", "msg", "err", "init", "dest", "fn", NULL
};

/* Unix 路径片段 */
static const char *forbidden_path_segments[] = {
    "bin", "etc", "usr", "var", "tmp", "dev", "proc", "sys", "lib",
    "sbin", "mnt", "opt", "root", "home", "boot", NULL
};

/* ===================================================================
 * 内部罢工信号处理
 * =================================================================== */

static void strike_print_header(const char *violation_type) {
    fprintf(stderr, "\n");
    fprintf(stderr, "================================================================================\n");
    fprintf(stderr, "[COMPILER STRIKE] 编译器拒绝工作\n");
    fprintf(stderr, "================================================================================\n");
    fprintf(stderr, "违规类型: %s\n", violation_type);
}

static void strike_print_footer(void) {
    fprintf(stderr, "================================================================================\n");
    fprintf(stderr, "返回码: %d\n", COMPILER_STRIKE_CODE);
    fprintf(stderr, "范式壁垒，从编译器开始。\n");
    fprintf(stderr, "================================================================================\n\n");
}

/* ===================================================================
 * [TODO-01] 点号罢工检测
 * =================================================================== */
int strike_detect_dot_access(const char *lexeme, int line, int column,
                              int is_in_float_literal) {
    if (!lexeme || strcmp(lexeme, ".") != 0) {
        return 0;
    }
    
    /* 浮点数中的点号是允许的 */
    if (is_in_float_literal) {
        return 0;
    }
    
    strike_print_header("点号成员访问");
    fprintf(stderr, "检测到: '%s' (在第 %d 行，第 %d 列)\n", lexeme, line, column);
    fprintf(stderr, "理由: 点号用于结束句子，不用于访问对象\n");
    fprintf(stderr, "在 AethelOS，点号 '.' 被禁止用于结构体成员访问\n");
    fprintf(stderr, "纠正: 请使用 '\\' (反斜杠) 来深入对象的灵魂\n");
    fprintf(stderr, "示例: struct\\member 而非 struct.member\n");
    strike_print_footer();
    
    exit(COMPILER_STRIKE_CODE);
}

/* ===================================================================
 * [TODO-02] 箭头罢工检测
 * =================================================================== */
int strike_detect_arrow(const char *lexeme, int line, int column) {
    if (!lexeme || strcmp(lexeme, "->") != 0) {
        return 0;
    }
    
    strike_print_header("指针箭头操作符");
    fprintf(stderr, "检测到: '%s' (在第 %d 行，第 %d 列)\n", lexeme, line, column);
    fprintf(stderr, "理由: 这是 C 语言手动内存管理的丑陋伤疤\n");
    fprintf(stderr, "唯一的成员访问操作符是 '\\'，唯一返回类型声明用':'\n");
    fprintf(stderr, "纠正: Aethelium 的引用是透明的，请同样使用 '\\'\n");
    fprintf(stderr, "示例: pointer\\member 而非 pointer->member\n");
    strike_print_footer();
    
    exit(COMPILER_STRIKE_CODE);
}

/* ===================================================================
 * [TODO-04] 下划线在标识符中的罢工
 * =================================================================== */
int strike_detect_underscore_identifier(const char *identifier, int line, 
                                         int column) {
    if (!identifier) return 0;
    
    if (strchr(identifier, '_') != NULL) {
        strike_print_header("下划线标识符");
        fprintf(stderr, "检测到: '%s' (在第 %d 行，第 %d 列)\n", 
                identifier, line, column);
        fprintf(stderr, "理由: 下划线是打字机时代的遗物\n");
        fprintf(stderr, "AethelOS 推行现代命名约定，而不是 Unix 的蛇形命名法\n");
        fprintf(stderr, "纠正: 请使用 '/' 作为逻辑分隔符\n");
        fprintf(stderr, "示例: calculate/sum 或 Calculate/Sum 而非 calculate_sum\n");
        strike_print_footer();
        
        return 1;
    }
    
    return 0;
}


/* ===================================================================
 * [TODO-03] 禁忌头文件罢工
 * =================================================================== */
int strike_detect_forbidden_include(const char *include_path) {
    if (!include_path) return 0;
    
    for (int i = 0; forbidden_includes[i] != NULL; i++) {
        if (strstr(include_path, forbidden_includes[i]) != NULL) {
            strike_print_header("禁忌头文件");
            fprintf(stderr, "检测到禁卷轴: <%s>\n", include_path);
            fprintf(stderr, "理由: AethelOS 不使用标准 C 库\n");
            
            if (strcmp(forbidden_includes[i], "stdio.h") == 0) {
                fprintf(stderr, "AethelOS 没有 'Standard IO'，只有 'Aethel Streams'\n");
            } else if (strcmp(forbidden_includes[i], "stdlib.h") == 0) {
                fprintf(stderr, "标准库？谁的标准？AethelOS 有自己的库\n");
            } else if (strcmp(forbidden_includes[i], "unistd.h") == 0) {
                fprintf(stderr, "Unix 标准的巢穴？在 AethelOS 中被禁止\n");
            } else if (strcmp(forbidden_includes[i], "pthread.h") == 0) {
                fprintf(stderr, "Unix 线程模型在 AethelOS 中不存在\n");
            }
            
            fprintf(stderr, "纠正: 请使用 import Aethel/Core\n");
            strike_print_footer();
            
            return 1;
        }
    }
    
    return 0;
}

/* ===================================================================
 * [TODO-05] 禁咒符号检测
 * =================================================================== */
int strike_detect_forbidden_symbol(const char *symbol_name, int line, 
                                    int column) {
    if (!symbol_name) return 0;
    
    for (int i = 0; forbidden_symbols[i] != NULL; i++) {
        if (strcmp(symbol_name, forbidden_symbols[i]) == 0) {
            strike_print_header("禁咒符号调用");
            fprintf(stderr, "检测到禁咒: '%s()' (在第 %d 行，第 %d 列)\n",
                    symbol_name, line, column);
            fprintf(stderr, "理由: 这是 Unix 系统调用或库函数，在 AethelOS 中被禁止\n");
            
            if (strcmp(symbol_name, "fork") == 0) {
                fprintf(stderr, "补充: 细胞才会分裂，软件需要实例化\n");
                fprintf(stderr, "纠正: 请调用 process/spawn 或 iya/instantiate\n");
            } else if (strcmp(symbol_name, "printf") == 0) {
                fprintf(stderr, "补充: 格式化字符串漏洞之王\n");
                fprintf(stderr, "纠正: 请使用 fmt/print\n");
            } else if (strcmp(symbol_name, "malloc") == 0 || 
                       strcmp(symbol_name, "free") == 0) {
                fprintf(stderr, "补充: 手动内存管理在应用层被禁止\n");
                fprintf(stderr, "纠正: 请使用 allocator 或 managed 块\n");
            } else if (strcmp(symbol_name, "open") == 0 ||
                       strcmp(symbol_name, "read") == 0 ||
                       strcmp(symbol_name, "write") == 0) {
                fprintf(stderr, "补充: POSIX I/O 操作\n");
                fprintf(stderr, "纠正: 请使用 VFS 对象方法 file\\read\n");
            } else if (strcmp(symbol_name, "ioctl") == 0) {
                fprintf(stderr, "补充: 这是一个把垃圾塞进内核的垃圾桶函数\n");
                fprintf(stderr, "纠正: 请使用正规的 AethelOS 接口\n");
            }
            
            strike_print_footer();
            return 1;
        }
    }
    
    return 0;
}

int strike_detect_forbidden_identifier(const char *identifier, int line, int column) {
    size_t i;

    if (!identifier || !identifier[0]) return 0;

    for (i = 0; forbidden_symbols[i] != NULL; i++) {
        if (ascii_lower_cmp(identifier, forbidden_symbols[i])) {
            strike_print_header("Unix 命名污染");
            fprintf(stderr, "检测到禁咒命名: '%s' (在第 %d 行，第 %d 列)\n",
                    identifier, line, column);
            fprintf(stderr, "理由: Unix/POSIX 术语在 AethelOS 命名体系中被严格禁止\n");
            fprintf(stderr, "纠正: 使用 AethelOS 原生命名（如 process/spawn, vfs/read, aethel/*）\n");
            strike_print_footer();
            return 1;
        }
    }

    for (i = 0; forbidden_path_segments[i] != NULL; i++) {
        if (ascii_lower_cmp(identifier, forbidden_path_segments[i])) {
            strike_print_header("Unix 路径命名污染");
            fprintf(stderr, "检测到禁忌路径片段: '%s' (在第 %d 行，第 %d 列)\n",
                    identifier, line, column);
            fprintf(stderr, "理由: AethelOS 路径体系与 POSIX 完全不同，禁止复用 Unix 目录语义\n");
            fprintf(stderr, "纠正: 使用 AEFS 卷语义与 AethelOS 原生命名\n");
            strike_print_footer();
            return 1;
        }
    }

    return 0;
}

/* ===================================================================
 * [TODO-06] Main 函数检测
 * =================================================================== */
int strike_detect_main_function(const char *function_name) {
    if (!function_name) return 0;
    
    if (strcmp(function_name, "main") == 0) {
        strike_print_header("Main 函数");
        fprintf(stderr, "检测到: 'main' 函数定义\n");
        fprintf(stderr, "理由: 这是 Unix 进程的入口，不是 AethelOS 实例的入口\n");
        fprintf(stderr, "AethelOS 的程序没有'主函数'，只有'入口点 (@entry)'\n");
        fprintf(stderr, "纠正: 请使用 @entry 装饰器定义\n");
        fprintf(stderr, "示例: @entry func app/start(ctx: Context) { ... }\n");
        strike_print_footer();
        
        return 1;
    }
    
    return 0;
}

/* ===================================================================
 * [TODO-07] GNU 扩展检测
 * =================================================================== */
int strike_detect_gnu_extension(const char *extension_syntax) {
    if (!extension_syntax) return 0;
    
    if (strstr(extension_syntax, "__attribute__") != NULL ||
        strstr(extension_syntax, "__asm__") != NULL ||
        strstr(extension_syntax, "__builtin_") != NULL) {
        
        fprintf(stderr, 
            "\n================================================================================\n"
            "[FATAL COMPILER CRASH] 编译器自杀模式已激活\n"
            "================================================================================\n"
            "错误: 试图将 GNU 寄生虫注入 AethelOS\n"
            "编译器已自毁以保全系统纯洁性\n"
            "具体规范: %s\n"
            "================================================================================\n\n",
            extension_syntax);
        
        /* 编译器自杀 */
        exit(COMPILER_STRIKE_CODE);
    }
    
    return 0;
}

/* ===================================================================
 * 路径字面量检测
 * =================================================================== */
int strike_detect_path_literals(const char *string_literal) {
    if (!string_literal) return 0;
    
    /* 检查 Unix 风格路径 */
    if (strstr(string_literal, "/") != NULL) {
        /* 排除 // 注释符 */
        if (!(strstr(string_literal, "//") == string_literal && 
              string_literal[2] != '/')) {
            
            /* 检查其他路径特征 */
            if (strstr(string_literal, "../") != NULL ||
                strstr(string_literal, "./") != NULL ||
                strstr(string_literal, "~") != NULL) {
                
                fprintf(stderr, "[PATH CONTAMINATION] String: '%s'\n",
                        string_literal);
                fprintf(stderr, "警告: 检测到 Unix 风格路径在字符串中\n");
                fprintf(stderr, "AethelOS 不支持相对路径逻辑\n");
                
                return 1;
            }
        }
    }
    
    return 0;
}

/* ===================================================================
 * 通用罢工输出
 * =================================================================== */
void print_strike_message(UnixViolation *violation) {
    if (!violation) return;
    
    strike_print_header(violation->violation_type);
    fprintf(stderr, "检测到: '%s' (第 %d 行，第 %d 列)\n",
            violation->detected_token, violation->line, violation->column);
    fprintf(stderr, "理由: %s\n", violation->reason);
    fprintf(stderr, "纠正: %s\n", violation->correction);
    strike_print_footer();
}

void compiler_strike_and_exit(UnixViolation *violation) {
    print_strike_message(violation);
    exit(COMPILER_STRIKE_CODE);
}
