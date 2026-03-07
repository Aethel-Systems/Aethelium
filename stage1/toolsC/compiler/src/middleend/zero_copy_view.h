/*
 * AethelOS Aethelium Compiler - Zero-Copy View Code Generation
 * 零拷贝视图代码生成实现
 * 
 * 版本：1.0 工业级
 * 状态：完整实现
 * 
 * 功能：
 * - view<T>类型的编译时检查
 * - 零拷贝视图的x86-64机器码生成
 * - 内存对齐与边界检查
 * - 类型安全的强转换
 */

#ifndef ZERO_COPY_VIEW_H
#define ZERO_COPY_VIEW_H

#include "../frontend/parser/aec_parser.h"
#include <stdint.h>
#include <stdio.h>

/* =====================================================================
 * 零拷贝视图类型定义
 * ===================================================================== */

typedef enum {
    VIEW_TYPE_STRUCT = 1,      /* view<StructName> - 结构体视图 */
    VIEW_TYPE_ARRAY = 2,       /* view<[T]> - 数组视图 */
    VIEW_TYPE_PRIMITIVE = 3,   /* view<UInt64> - 原始类型视图 */
} ZeroCopyViewType;

typedef struct {
    char *base_type_name;       /* 被视图化的类型名，如 "PacketHeader" */
    uint32_t base_type_size;    /* 基础类型大小（字节） */
    ZeroCopyViewType view_type; /* 视图类型分类 */
    int is_array;               /* 是否为数组视图 [T] */
    char *element_type;         /* 数组元素类型，仅当is_array=1时有效 */
    uint32_t element_size;      /* 元素大小 */
    uint32_t alignment;         /* 对齐要求（字节） */
    int requires_bounds_check;  /* 是否需要边界检查 */
} ZeroCopyViewInfo;

/* =====================================================================
 * 零拷贝视图编译函数声明
 * ===================================================================== */

/**
 * 从AST TYPE节点解析view<T>信息
 * 返回ZeroCopyViewInfo结构，失败返回NULL
 */
ZeroCopyViewInfo* zcv_parse_view_type(ASTNode *type_node);

/**
 * 释放零拷贝视图信息
 */
void zcv_free_view_info(ZeroCopyViewInfo *info);

/**
 * 验证view<T>类型的合法性
 * 返回0表示合法，-1表示非法
 */
int zcv_validate_view_type(const char *view_type_name);

/**
 * 生成零拷贝视图的x86-64机器码
 * 本质：ptr -> rax, type信息 -> rcx, 生成一个"解释"指令序列
 * 
 * 参数：
 *   out: 输出文件
 *   source_ptr_reg: 源指针寄存器(如"rax")
 *   target_type: 目标view类型(如"view<PacketHeader>")
 *   view_var_name: 目标变量名(用于调试符号)
 * 
 * 返回：生成的机器码字节数
 */
size_t zcv_gen_view_cast(FILE *out,
                         const char *source_ptr_reg,
                         const char *target_type,
                         const char *view_var_name);

/**
 * 生成view<T>成员访问的机器码
 * 例如：header\version 生成从rax+offset读取version字段
 * 
 * 参数：
 *   out: 输出文件
 *   view_var_reg: 视图变量所在寄存器
 *   member_name: 成员名称
 *   member_offset: 成员在结构体中的偏移量
 *   member_size: 成员大小(1/2/4/8字节)
 *   target_reg: 目标寄存器(用于存储读取结果)
 * 
 * 返回：生成的机器码字节数
 */
size_t zcv_gen_view_member_access(FILE *out,
                                  const char *view_var_reg,
                                  const char *member_name,
                                  uint32_t member_offset,
                                  uint8_t member_size,
                                  const char *target_reg);

/**
 * 生成view<[T]>数组访问的机器码
 * 例如：descriptors[i] 的索引访问
 */
size_t zcv_gen_view_array_access(FILE *out,
                                 const char *view_array_reg,
                                 const char *index_reg,
                                 uint32_t element_size,
                                 const char *target_reg);

/**
 * 生成view<T>的内存对齐检查
 * 验证基地址是否满足对齐要求
 */
size_t zcv_gen_alignment_check(FILE *out,
                               const char *ptr_reg,
                               uint32_t alignment);

/**
 * 获取view<T>的总大小（用于边界检查）
 * 对于view<[T]>，需要知道数组元素数量
 */
uint64_t zcv_get_view_total_size(const char *view_type, uint64_t array_element_count);

/**
 * 类型检查：两个view类型是否兼容
 * 返回1兼容，0不兼容
 */
int zcv_types_compatible(const char *type1, const char *type2);

#endif /* ZERO_COPY_VIEW_H */
