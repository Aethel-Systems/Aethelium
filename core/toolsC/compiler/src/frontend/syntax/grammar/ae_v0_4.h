/*
 * AethelOS Aethelium Compiler - Syntax Version Definitions
 * 语法版本 v0.4：预研阶段（未启用）
 * 
 * 特点：
 * - PascalCase 成员访问语法优化 (obj->Member vs obj.member)
 * - camelCase 命名约定支持
 * - 新式命名空间 import 语义
 * - 泛型增强
 */

#ifndef AE_V0_4_GRAMMAR_H
#define AE_V0_4_GRAMMAR_H

/* =====================================================================
 * 语法版本标识
 * ===================================================================== */

/* v0.4 使用不同的标识常量来避免冲突 */
#if !defined(AE_SYNTAX_VERSION_MAJOR_V4)
#define AE_SYNTAX_VERSION_MAJOR_V4 0
#endif

#if !defined(AE_SYNTAX_VERSION_MINOR_V4)
#define AE_SYNTAX_VERSION_MINOR_V4 4
#endif

#if !defined(AE_SYNTAX_VERSION_PATCH_V4)
#define AE_SYNTAX_VERSION_PATCH_V4 0
#endif

/* =====================================================================
 * v0.4 语法特性开关（默认全部禁用，需要显式启用）
 * ===================================================================== */

/* 成员访问语法改进 */
#define AE_V0_4_DOT_MEMBER_ACCESS    1   /* a.b 访问（继承自 v0.3） */
#define AE_V0_4_ARROW_PTR_ACCESS     1   /* a->b 指针成员访问（新） */
#define AE_V0_4_CAMEL_CASE_NAMES     1   /* 支持 camelCase */
#define AE_V0_4_PASCAL_CASE_NAMES    1   /* 支持 PascalCase */
#define AE_V0_4_NEW_IMPORT_SEMANTICS 1   /* 新式 use/import with namespaces */

/* 增强的基本特性 */
#define AE_V0_4_BASIC_TYPES          1   /* 继承 v0.3 */
#define AE_V0_4_STRUCT_TYPES         1   /* struct 增强（方法） */
#define AE_V0_4_ENUM_TYPES           1   /* enum 增强（关联值） */
#define AE_V0_4_PROTOCOL_TYPES       1   /* protocol 定义 */

/* 增强的控制流 */
#define AE_V0_4_IF_ELSE              1   /* if/else（继承） */
#define AE_V0_4_WHILE_LOOP           1   /* while（继承） */
#define AE_V0_4_FOR_LOOP             1   /* for（继承） */
#define AE_V0_4_MATCH_STMT           1   /* match（增强） */
#define AE_V0_4_SWITCH_STMT          1   /* switch（新增） */
#define AE_V0_4_LABELED_BREAKS       1   /* 标签化 break/continue */

/* 增强的表达式 */
#define AE_V0_4_BINARY_OPS           1   /* 继承 v0.3 */
#define AE_V0_4_PIPE_OPERATOR        1   /* |> 管道操作符（新） */
#define AE_V0_4_OPTIONAL_CHAINING    1   /* a?.b?.c Optional 链（新） */
#define AE_V0_4_NIL_COALESCING       1   /* a ?? b null 合并（新） */
#define AE_V0_4_FUNCTION_CALLS       1   /* 继承 v0.3 */

/* 高级特性启用 */
#define AE_V0_4_OPTIONAL_TYPE        1   /* T? Optional（继承） */
#define AE_V0_4_GENERIC_TYPES        1   /* 泛型（增强） */
#define AE_V0_4_GENERIC_CONSTRAINTS  1   /* 泛型约束（新） */
#define AE_V0_4_CLOSURES             1   /* 闭包（启用） */
#define AE_V0_4_ASYNC_AWAIT          1   /* async/await（启用） */
#define AE_V0_4_RESULT_TYPE          1   /* Result<T, E> 类型（新） */

/* 系统层特性 */
#define AE_V0_4_METAL_BLOCK          1   /* metal { } 内联机器码（启用） */
#define AE_V0_4_ASM_BLOCK            1   /* asm { } 内联汇编（启用） */
#define AE_V0_4_BYTES_BLOCK          1   /* bytes { } 字节注入（启用） */
#define AE_V0_4_EXTERN_FUNCTION      1   /* extern（继承） */
#define AE_V0_4_PLATFORM_SPECIFIC    1   /* #[platform(...)] 属性（新） */

/* =====================================================================
 * v0.4 BNF 语法增强（注释形式）
 * ===================================================================== */

/*
GRAMMAR ae_v0_4 {
    // 导入增强
    import_stmt    = ("use" | "import") module_path ("as" IDENT)? ";"
    module_path    = IDENT ("::" IDENT)*
    
    // 命名约定灵活性
    identifier     = [a-z][a-zA-Z0-9_]*  // snake_case
                   | [a-z][a-zA-Z0-9]*    // camelCase
                   | [A-Z][a-zA-Z0-9_]*   // PascalCase
    
    // 增强的表达式
    expr           = pipe_expr
    pipe_expr      = optional_chain ("|>" optional_chain)*
    optional_chain = nil_coalesce ("?" ("." IDENT | "[" expr "]" | "(" args ")"))*
    nil_coalesce   = or_expr ("??" or_expr)*
    
    // 指针成员访问
    postfix_expr   = primary_expr (("." IDENT | "->" IDENT | "[" expr "]" | "(" args ")"))*
    
    // 闭包表达式
    closure_expr   = "{" ("|" params "|")? statement* expr? "}"
    
    // 异步函数
    function_decl  = ("async")? "func" IDENT "(" params ")" type_spec block
    
    // Result 类型
    type_spec      = "Result" "<" type_spec "," type_spec ">"
                   | ...（其他类型）
    
    // 泛型约束
    generic_param  = IDENT ("<" IDENT ("+" IDENT)* ">")?
    
    // 系统属性
    attribute      = "#[" IDENT ("(" ... ")")? "]"
}
*/

#endif /* AE_V0_4_GRAMMAR_H */
