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
 * AethelOS Aethelium Compiler - Parser Header
 * 语法分析器：构建抽象语法树 (AST)
 */

#ifndef AEC_PARSER_H
#define AEC_PARSER_H

#include "aec_lexer.h"

/* =====================================================================
 * AST 节点类型定义
 * ===================================================================== */

typedef enum {
    AST_PROGRAM,
    AST_FUNC_DECL,
    AST_VAR_DECL,
    AST_STRUCT_DECL,
    AST_ENUM_DECL,
    AST_BLOCK,
    AST_IF_STMT,
    AST_WHILE_STMT,
    AST_FOR_STMT,
    AST_RETURN_STMT,
    AST_EXPR_STMT,
    AST_BREAK_STMT,
    AST_CONTINUE_STMT,
    AST_MATCH_STMT,
    AST_CASE_CLAUSE,
    AST_SWITCH_STMT,    /* 新增：switch 语句 */
    AST_SWITCH_CASE,    /* 新增：switch case 子句 */
    AST_GUARD_STMT,     /* 新增：guard 语句 */
    AST_DEFER_STMT,
    AST_STRUCT_INIT,
    AST_ASSIGNMENT,
    AST_BINARY_OP,
    AST_UNARY_OP,
    AST_CALL,
    AST_ACCESS,
    AST_LITERAL,
    AST_IDENT,
    AST_ARRAY_LITERAL,
    AST_TYPE,
    AST_RANGE,          /* 新增：Range 表达式 (start..end) */
    AST_CLOSURE,        /* 新增：闭包表达式 { ... } */
    AST_IMPLICIT_PARAM, /* 新增：隐式参数 $0, $1 等 */
    AST_FORCE_UNWRAP,   /* 新增：强制解包 expr! */
    AST_TUPLE_LITERAL,  /* 新增：元组字面量 (x, y, z) */
    AST_TUPLE_PATTERN,  /* 新增：元组解构模式 (a, b) */
    AST_REFERENCE,      /* 新增：引用 &value */
    /* 系统层特性 */
    AST_METAL_BLOCK,    /* 新增：metal { ... } 块 */
    AST_ASM_BLOCK,      /* 新增：asm { ... } 内联汇编 */
    AST_BYTES_BLOCK,    /* 新增：bytes { "HEX" ... } 字节注入 */
    AST_IMPORT_STMT,    /* 新增：import/Rimport 语句 */
    AST_TYPECAST,       /* 新增：expr as Type 类型转换 */
    /* 原初层特性 */
    AST_MAP_DEF,        /* 新增：map ASCII内存拓扑定义 */
    AST_SYNTAX_DEF,     /* 新增：syntax自定义语法映射 */
    
    /* 硅基语义特性 */
    AST_SILICON_BLOCK = 100,           /* silicon { ... } 硅基语义块 */
    AST_MICROARCH_CONFIG = 101,        /* 微架构配置 using CPU/Current { ... } */
    AST_MSR_ACCESS = 102,              /* MSR寄存器访问 MSR/EFER\Syscall/Enable */
    AST_PIPELINE_BLOCK = 103,          /* pipeline(...) { ... } 流水线块 */
    AST_PIPELINE_BARRIER = 104,        /* pipeline\barrier(.type) */
    AST_PIPELINE_HINT = 105,           /* pipeline\hint(.type) */
    AST_SYNTAX_OPCODE_DEF = 106,       /* syntax/opcode Op/Name = bytes ... */
    AST_CACHE_OPERATION = 107,         /* silicon\cache\flush/invalidate */
    AST_PREFETCH_OPERATION = 108,      /* silicon\prefetch(line, hint, intent) */
    AST_PHYS_TYPE = 109,               /* phys<T> 物理硬连线类型 */
    
    /* 硬件层（Hardware Layer）节点类型 */
    AST_HW_BLOCK = 110,                /* hardware { ... } 硬件层块 */
    AST_HW_REG_BINDING = 111,          /* reg<"rax", UInt64> 物理寄存器绑定 */
    AST_HW_PORT_IO = 112,              /* port<T> 端口I/O */
    AST_HW_VOLATILE_VIEW = 113,        /* @volatile view<T> 挥发性视图 */
    AST_HW_GATE_FUNC = 114,            /* @gate(type: \interrupt) 门函数 */
    AST_HW_MORPH_BLOCK = 115,          /* morph { ... } 形态置换块 */
    AST_HW_ISA_CALL = 116,             /* hardware\isa\syscall() ISA直通调用 */
    AST_HW_VECTOR_TYPE = 117,          /* vector<T, N> 向量类型 */
    AST_HW_CONTROL_REG = 118,          /* CPU/Current\Control\CR0 控制寄存器 */
    AST_HW_FLAG_ACCESS = 119,          /* CPU/Flags\Carry 标志位访问 */
} ASTNodeType;

typedef struct ASTNode ASTNode;

struct ASTNode {
    ASTNodeType type;
    int line;
    union {
        /* 程序节点 */
        struct {
            ASTNode **declarations;
            int decl_count;
        } program;
        
        /* 函数声明 */
        struct {
            char *name;
            ASTNode **params;
            int param_count;
            ASTNode *return_type;
            ASTNode *body;
            int is_extern;    /* 新增：标记是否为外部函数 */
            /* [TODO-06] 装饰器支持 */
            char **attributes;    /* 装饰器数组 (@entry, @packed, 等) */
            int attr_count;       /* 装饰器数量 */
        } func_decl;
        
        /* 变量声明 */
        struct {
            char *name;
            ASTNode *type;
            ASTNode *init;
            ASTNode *init_value;  /* 初始化值 */
            int is_const;         /* 是否为常量 */
            int is_mutable;       /* 是否可变 */
        } var_decl;
        
        /* 代码块 */
        struct {
            ASTNode **statements;
            int stmt_count;
        } block;
        
        /* If 语句 */
        struct {
            ASTNode *condition;
            ASTNode *then_branch;
            ASTNode *else_branch;
        } if_stmt;
        
        /* While 循环 */
        struct {
            ASTNode *condition;
            ASTNode *body;
        } while_stmt;
        
        struct {
            /* 兼容 C 风格和 AE 风格 */
            /* C style: for (init; condition; increment) */
            /* AE style: for (variable in iterable) */
            ASTNode *init;         // AE: NULL
            ASTNode *condition;    // AE: NULL
            ASTNode *increment;    // AE: NULL
            char *variable;        // AE: 循环变量名
            ASTNode *iterable;     // AE: 迭代对象 (Range等)
            ASTNode *body;
        } for_stmt;
        
        /* Match 语句 */
        struct {
            ASTNode *expr;
            ASTNode **cases;
            int case_count;
            ASTNode *default_case;
        } match_stmt;
        
        /* Case 子句 */
        struct {
            ASTNode *pattern;  /* 可以是 enum case (.exit) 或值 */
            ASTNode **statements;
            int stmt_count;
        } case_clause;
        
        /* Defer 语句 */
        struct {
            ASTNode *body;
        } defer_stmt;
        
        
        /* Return 语句 */
        struct {
            ASTNode *value;
        } return_stmt;
        
        /* 赋值和表达式 */
        struct {
            ASTNode *left;
            ASTNode *right;
        } assignment;
        
        /* 二元操作 */
        struct {
            char op[4];
            ASTNode *left;
            ASTNode *right;
        } binary_op;
        
        /* 一元操作 */
        struct {
            char op[4];
            ASTNode *operand;
        } unary_op;
        
        /* 函数调用 */
        struct {
            ASTNode *func;
            ASTNode **args;
            int arg_count;
        } call;
        
        /* 结构体初始化 Point(x: 1, y: 2) */
        struct {
            char *type_name;
            char **field_names;
            ASTNode **field_values;
            int field_count;
        } struct_init;
        
        /* 成员访问 */
        struct {
            ASTNode *object;
            char *member;
            ASTNode *index_expr;   /* 可选：用于 obj[index] 索引表达式 */
        } access;
        
        /* 字面量 */
        struct {
            int is_float;
            int is_string;
            union {
                int64_t int_value;
                double float_value;
                char *str_value;    /* 字符串字面量 */
            } value;
        } literal;
        
        /* 标识符 */
        struct {
            char *name;
        } ident;
        
        /* 数组字面量 */
        struct {
            ASTNode **elements;
            int element_count;
        } array_literal;
        
        /* 类型 */
        struct {
            char *name;
            ASTNode **type_params;
            int type_param_count;
        } type;
        
        /* 新增：Range */
        struct {
            ASTNode *start; ASTNode *end;
            int inclusive;
        } range;
        
        /* 新增：闭包表达式 { $0.field } */
        struct {
            ASTNode *body;      /* 闭包体 */
            int param_count;    /* 参数个数（用于验证隐式参数范围） */
        } closure;
        
        /* 新增：隐式参数 $0, $1, $2 等 */
        struct {
            int index;  /* 参数索引：0 for $0, 1 for $1, etc. */
        } implicit_param;
        
        /* 新增：强制解包 expr! */
        struct {
            ASTNode *operand;
        } force_unwrap;
        
        /* 新增：元组字面量 (x, y, z) */
        struct {
            ASTNode **elements;
            int element_count;
        } tuple_literal;
        
        /* 新增：元组解构模式 (a, b) */
        struct {
            char **names;
            int element_count;
        } tuple_pattern;
        
        /* 新增：引用 &value 或 &device */
        struct {
            ASTNode *operand;
        } reference;
        
        /* 新增：结构体声明（带属性装饰器支持） */
        struct {
            char *name;
            char **field_names;
            ASTNode **field_types;
            int field_count;
            char **attributes;     // @packed, @aligned(N), etc.
            int attr_count;
        } struct_decl;
        
        /* 新增：枚举声明 */
        struct {
            char *name;
            char **case_names;
            int case_count;
        } enum_decl;
        
        /* 新增：metal { ... } 块 */
        struct {
            ASTNode **statements;
            int stmt_count;
        } metal_block;
        
        /* 新增：asm { ... } 内联汇编 */
        struct {
            char *code;            // 汇编代码字符串
        } asm_block;
        
        /* 新增：bytes { "HEX"... } 字节注入 */
        struct {
            char **hex_strings;    // 十六进制字符串数组
            int hex_count;
        } bytes_block;
        
        /* 新增：import/Rimport 语句 */
        struct {
            char *module;
            int is_rimport;        // 0 for import, 1 for Rimport
        } import_stmt;
        
        /* 新增：switch 语句 */
        struct {
            ASTNode *expr;         // 被比较的表达式
            ASTNode **cases;       // case 子句数组
            int case_count;        // case 数量
            ASTNode *default_case; // default 子句
        } switch_stmt;
        
        /* 新增：switch case 子句 */
        struct {
            ASTNode **values;      // case 值数组（支持多值）
            int value_count;       // 值个数
            ASTNode **statements;  // 语句数组
            int stmt_count;        // 语句个数
        } switch_case;
        
        /* 新增：guard 语句 */
        struct {
            ASTNode *condition;    // guard 条件
            char *binding_name;    // 绑定名（if let 时使用）
            ASTNode *binding_expr; // 绑定表达式
            ASTNode *else_branch;  // else 分支（通常是 return 等）
        } guard_stmt;
        
        /* 新增：expr as Type 类型转换 */
        struct {
            ASTNode *expr;
            ASTNode *target_type;
        } typecast;
        
        /* 原初层：map ASCII内存拓扑定义 */
        struct {
            char *name;                    /* Map名称（如 ACPI/Table） */
            uint64_t base_address;         /* 基址（如 0xE0000） */
            char *ascii_definition;        /* ASCII矩形（原始字符串） */
            
            /* 计算出的字段列表 */
            char **field_names;            /* 字段名称 */
            char **field_types;            /* 字段类型 */
            int *field_offsets;            /* 字段偏移（字节） */
            int *field_sizes;              /* 字段大小（字节） */
            int field_count;               /* 字段总数 */
            
            int alignment;                 /* 对齐（如@aligned(8) = 8） */
            char **attributes;             /* 属性列表 */
            int attr_count;                /* 属性数量 */
        } map_def;
        
        /* 原初层：syntax自定义语法映射定义 */
        struct {
            char *name;                    /* Syntax块名称（如 DebugOps） */
            
            /* Pattern-Action 映射 */
            char **patterns;               /* 模式字符串数组 */
            char **actions;                /* 对应的action（asm或bytes） */
            int pattern_count;             /* pattern总数 */
        } syntax_def;
        
        /* ==================== 硅基语义节点数据结构 ==================== */
        
        /* AST_SILICON_BLOCK: silicon { ... } 块 */
        struct {
            ASTNode **statements;          /* 硅基块内的语句 */
            int stmt_count;
            int has_rimport_check;         /* 是否已检查Rimport权限 */
        } silicon_block;
        
        /* AST_MICROARCH_CONFIG: using CPU/Current { ... } */
        struct {
            char *cpu_context;             /* CPU上下文（如 "CPU/Current") */
            char **register_names;         /* 寄存器名称数组 */
            char **property_names;         /* 属性名称数组 */
            ASTNode **values;              /* 赋值表达式数组 */
            int config_count;              /* 配置项数量 */
        } microarch_config;
        
        /* AST_MSR_ACCESS: MSR/EFER\Syscall/Enable = true */
        struct {
            char *msr_name;                /* MSR名称 (如 "MSR/EFER") */
            char *field_path;              /* 字段路径 (如 "Syscall/Enable") */
            uint64_t msr_address;          /* 编译时计算的MSR地址 */
            uint32_t bit_position;         /* 比特位置 */
            uint32_t bit_width;            /* 比特宽度 */
            ASTNode *value;                /* 赋值的值 */
            int is_read;                   /* 0=write, 1=read */
        } msr_access;
        
        /* AST_PIPELINE_BLOCK: pipeline(behavior: .serialize, ...) { ... } */
        struct {
            uint32_t behavior_flags;       /* PIPELINE_SERIALIZE等标志 */
            uint32_t speculation_flags;    /* PIPELINE_BLOCK等标志 */
            ASTNode **statements;          /* 块内的语句 */
            int stmt_count;
        } pipeline_block;
        
        /* AST_PIPELINE_BARRIER: pipeline\barrier(.memory/load/store) */
        struct {
            char *barrier_type;            /* 屏障类型描述 */
            uint32_t barrier_mode;         /* PIPELINE_BARRIER_* */
        } pipeline_barrier;
        
        /* AST_PIPELINE_HINT: pipeline\hint(.branch/taken/strong) */
        struct {
            char *hint_type;               /* 提示类型描述 */
            uint32_t hint_flags;           /* PIPELINE_HINT_* */
        } pipeline_hint;
        
        /* AST_SYNTAX_OPCODE_DEF: syntax/opcode Op/Name = bytes "HEX" */
        struct {
            char *opcode_name;             /* 操作码符号名 */
            char *prefix_hex;              /* 前缀十六进制 */
            char *opcode_hex;              /* 操作码十六进制 */
            char *operand_spec;            /* 参数规范 */
            char **operand_positions;      /* 参数占位符数组 */
            int operand_count;
        } syntax_opcode_def;
        
        /* AST_CACHE_OPERATION: silicon\cache\flush/invalidate/clean */
        struct {
            char *operation;               /* flush/invalidate/clean */
            ASTNode *target;               /* 缓存行引用 */
            uint32_t cache_level;          /* L1/L2/L3 */
        } cache_operation;
        
        /* AST_PREFETCH_OPERATION: silicon\prefetch(line, hint, intent) */
        struct {
            ASTNode *address;              /* 预取地址 */
            char *hint_type;               /* T0/T1/T2/NTA */
            int write_intent;              /* 0=read, 1=write */
            uint32_t spatial_locality;     /* 0=no, 1=low, 2=high */
            uint32_t temporal_locality;    /* 0=no, 1=low, 2=high */
        } prefetch_operation;
        
        /* AST_PHYS_TYPE: phys<T> 物理硬连线类型 */
        struct {
            ASTNode *element_type;         /* T（如 Cache/Line） */
            uint64_t physical_address;     /* 物理地址（编译时可选） */
        } phys_type;
        
        /* ==================== 硬件层节点数据结构 ==================== */
        
        /* AST_HW_BLOCK: hardware { ... } 硬件层块 */
        struct {
            ASTNode **statements;          /* 硬件层块内的语句 */
            int stmt_count;
            char *gate_type;               /* @gate装饰器类型（如 interrupt, syscall） */
        } hw_block;
        
        /* AST_HW_REG_BINDING: var x: reg<"rax", UInt64> 物理寄存器绑定 */
        struct {
            char *reg_name;                /* 物理寄存器名称（如 "rax", "rdi"） */
            ASTNode *reg_type;             /* 寄存器类型（如 UInt64） */
            char *binding_name;            /* 绑定名称（变量名） */
            uint32_t hw_reg_id;            /* 硬件寄存器ID（编译时计算） */
        } hw_reg_binding;
        
        /* AST_HW_PORT_IO: port<T> 端口I/O操作 */
        struct {
            ASTNode *port_type;            /* T 端口类型参数 */
            uint16_t port_number;          /* I/O端口号 */
            int is_read;                   /* 0=write, 1=read */
            ASTNode *operand;              /* 读/写的操作数 */
        } hw_port_io;
        
        /* AST_HW_VOLATILE_VIEW: @volatile view<T> 挥发性视图 */
        struct {
            ASTNode *element_type;         /* T 元素类型 */
            ASTNode *base_address;         /* 基地址表达式 */
            int is_volatile;               /* 1=@volatile装饰 */
        } hw_volatile_view;
        
        /* AST_HW_GATE_FUNC: @gate(type: \interrupt, ...) 门函数声明 */
        struct {
            char *gate_type_name;          /* Gate类型（如 \interrupt, \syscall） */
            char **param_names;            /* 参数名数组 */
            ASTNode **param_types;         /* 参数类型数组 */
            int param_count;
            ASTNode *return_type;          /* 返回类型 */
            ASTNode *body;                 /* 函数体 */
            struct {
                uint32_t vector;           /* 中断向量号 */
                uint32_t ist;              /* IST (Interrupt Stack Table) */
                int dpl;                   /* 特权级 */
                int present;               /* Present标志 */
            } gate_fields;
        } hw_gate_func;
        
        /* AST_HW_MORPH_BLOCK: morph { ... } 形态置换块 */
        struct {
            ASTNode **statements;          /* morph块内的语句 */
            int stmt_count;
            char *target_cpu_mode;         /* 目标CPU模式（如 "long_mode", "protected") */
            ASTNode **control_regs;        /* CR0, CR3, CR4 等值 */
            int control_reg_count;
            uint64_t target_stack;         /* 目标栈指针 */
            uint64_t target_rip;           /* 目标指令指针 */
        } hw_morph_block;
        
        /* AST_HW_ISA_CALL: hardware\isa\syscall() ISA直通调用 */
        struct {
            char *isa_operation;           /* ISA操作名（如 "syscall", "cpuid", "sgdt"） */
            ASTNode **operands;            /* 操作数数组 */
            int operand_count;
            uint8_t opcode_bytes[16];      /* 预计算的操作码字节 */
            int opcode_length;             /* 操作码长度 */
        } hw_isa_call;
        
        /* AST_HW_VECTOR_TYPE: vector<T, N> 向量类型 */
        struct {
            ASTNode *element_type;         /* T 元素类型 */
            int vector_size;               /* N 向量大小（如128, 256, 512） */
            char *vector_unit;             /* 向量单元类型（SSE, AVX, AVX-512） */
        } hw_vector_type;
        
        /* AST_HW_CONTROL_REG: CPU/Current\Control\CR0 控制寄存器访问 */
        struct {
            char *reg_name;                /* 寄存器名（如 "CR0", "CR3"） */
            char *field_name;              /* 字段名（如 "PG", "PE"） */
            uint32_t bit_position;         /* 比特位置 */
            uint32_t bit_width;            /* 比特宽度 */
            ASTNode *value;                /* 设置的值（可为NULL表示只读） */
        } hw_control_reg;
        
        /* AST_HW_FLAG_ACCESS: CPU/Flags\Carry 标志位访问 */
        struct {
            char *flag_name;               /* 标志名（如 "Carry", "Zero", "Sign"） */
            uint32_t rflags_bit;           /* RFLAGS中的比特位置 */
            int is_read;                   /* 0=write, 1=read */
            ASTNode *value;                /* 设置的值（可为NULL表示只读） */
        } hw_flag_access;
    } data;
};

/* 前置声明 */
typedef struct StringTable StringTable;

typedef struct {
    Token *tokens;
    int pos;
    char error[256];
    int error_count;      /* 新增：错误计数 */
    int max_errors;       /* 新增：最大错误数 */
    int panic_mode;       /* 新增：panic 模式标志 */
    int debug;            /* 新增：调试标志 */
    
    /* 激进方案：嵌套泛型参数解析深度追踪与虚拟token回放 */
    int generic_nesting_depth;     /* 当前嵌套泛型参数的深度 */
    int has_virtual_gt_token;      /* 是否有虚拟的>token需要回放 */
    
    /* 工业级：字符串表集成 */
    StringTable *string_table;      /* 字符串表实例 */
    int owns_string_table;          /* 是否管理字符串表的生命周期 */
} Parser;

/* =====================================================================
 * Parser 接口
 * ===================================================================== */

Parser* parser_create(Token *tokens, int debug);
void parser_destroy(Parser *parser);

/* 字符串表集成接口 */
int parser_initialize_string_table(Parser *parser);
int parser_initialize_string_table_with_config(Parser *parser, void *config);  /* config 是 StringTableConfig* */
StringTable* parser_get_string_table(Parser *parser);
void parser_set_string_table(Parser *parser, StringTable *table, int takes_ownership);

ASTNode* parser_parse_program(Parser *parser);
ASTNode* parser_parse(Parser *parser);  /* Alias for parser_parse_program */
const char* parser_get_error(Parser *parser);
int parser_get_error_count(Parser *parser);
int parser_is_panic(Parser *parser);
void ast_print(ASTNode *node, int depth);

#endif /* AEC_PARSER_H */
