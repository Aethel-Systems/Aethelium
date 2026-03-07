/* src/compiler/aec_lexer.h */
#ifndef AEC_LEXER_H
#define AEC_LEXER_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    TK_EOF = 0,
    TK_NEWLINE = 1,
    TK_INDENT = 2,
    TK_DEDENT = 3,
    
    TK_IDENT = 10,
    TK_INT = 11,
    TK_FLOAT = 12,
    TK_STRING = 13,
    TK_TRUE = 14,
    TK_FALSE = 15,
    TK_NIL = 16,
    
    TK_IMPORT = 20,
    TK_RIMPORT = 21,
    TK_LET = 22,
    TK_VAR = 23,
    TK_FUNC = 24,
    TK_ASYNC = 25,
    TK_AWAIT = 26,
    TK_RETURN = 27,
    TK_IF = 28,
    TK_ELSE = 29,
    TK_WHILE = 30,
    TK_FOR = 31,
    TK_IN = 32,
    TK_STRUCT = 33,
    TK_ENUM = 34,
    TK_PROTOCOL = 35,
    TK_CLASS = 36,
    TK_EXTENSION = 37,
    TK_SWITCH = 38,
    TK_CASE = 39,
    TK_DEFAULT = 40,
    TK_BREAK = 41,
    TK_CONTINUE = 42,
    TK_SELF = 43,
    TK_EXTERN = 44,    /* 新增 */
    TK_CONST = 150,    /* 新增：const 关键字 */
    TK_USE = 151,      /* 新增：use 关键字 */
    
    TK_METAL = 45,     /* 系统层关键字 */
    TK_PTR = 46,
    TK_VIEW = 47,
    TK_REG = 158,      /* 物理寄存器绑定：reg<"rax", UInt64> */
    TK_UNMANAGED = 48,
    TK_ASM = 49,
    
    /* 硅基语义关键字 */
    TK_SILICON = 154,      /* silicon { ... } 硅基语义块 */
    TK_PIPELINE = 155,     /* pipeline(...) 流水线编排 */
    TK_PHYS = 156,         /* phys<T> 物理硬连线类型 */
    TK_HARDWARE = 157,     /* hardware { ... } 硬件层块 */
    
    TK_MATCH = 50,
    TK_DEFER = 51,
    TK_AS = 52,
    TK_BYTES = 53,
    TK_SYNTAX = 54,
    TK_USING = 55,
    TK_MAP = 56,
    TK_GUARD = 57,
    TK_PATTERN = 58,
    
    /* 原初层地址类型关键字 */
    TK_PHYSADDR = 152,  /* PhysAddr - 物理地址强类型 */
    TK_VIRTADDR = 153,  /* VirtAddr - 虚拟地址强类型 */
    
    TK_PLUS = 60,
    TK_MINUS = 61,
    TK_STAR = 62,
    TK_SLASH = 63,
    TK_BACKSLASH = 64,  /* 反斜杠：成员访问和枚举情况 */
    TK_PERCENT = 65,
    TK_ASSIGN = 66,
    TK_PLUS_ASSIGN = 67,
    TK_MINUS_ASSIGN = 68,
    TK_STAR_ASSIGN = 69,
    TK_SLASH_ASSIGN = 70,
    TK_EQ = 71,
    TK_NE = 72,
    TK_LT = 73,
    TK_LE = 74,
    TK_GT = 75,
    TK_GE = 76,
    TK_AND = 77,
    TK_OR = 78,
    TK_NOT = 79,
    TK_AMPERSAND = 80,
    TK_PIPE = 81,
    TK_CARET = 82,
    TK_TILDE = 83,
    TK_ARROW = 84,
    TK_RANGE = 85,
    TK_RANGE3 = 86,
    TK_DOT_STAR = 87,
    TK_LSHIFT = 88,
    TK_RSHIFT = 89,
    TK_RANGE_EXCLUSIVE = 90,
    TK_PIPE_ASSIGN = 91,
    TK_AMPERSAND_ASSIGN = 92,
    TK_CARET_ASSIGN = 93,
    TK_DIVIDE = 94,      /* Unicode division operator: ÷ */
    
    TK_LPAREN = 120,
    TK_RPAREN = 121,
    TK_LBRACE = 122,
    TK_RBRACE = 123,
    TK_LBRACKET = 124,
    TK_RBRACKET = 125,
    TK_COMMA = 126,
    TK_DOT = 127,
    TK_COLON = 128,
    TK_SCOPE_RESOLUTION = 136,  // :: (新增：用于enum成员访问)
    TK_SEMICOLON = 129,
    TK_QUESTION = 130,
    TK_AT = 131,
    TK_HASH = 132,
    TK_DOLLAR = 133,
    TK_EXCLAIM = 134,
    TK_NULL_COALESCE = 135,
    
    TK_ERROR = 255
} TokenType;

typedef struct {
    TokenType type;
    char *lexeme;
    int line;
    int column;
    union {
        int64_t int_value;
        double float_value;
    } value;
} Token;

typedef struct {
    const char *input;
    size_t length;
    size_t pos;
    int line;
    int column;
    Token *tokens;
    size_t token_count;
    size_t token_capacity;
    char error[1024];       /* 工业级别：更大的错误缓冲区 */
    int error_count;        /* 错误计数 */
    char errors[32][256];   /* 错误历史 */
} Lexer;

Lexer* lexer_create(const char *input);
void lexer_destroy(Lexer *lexer);
Token* lexer_tokenize(Lexer *lexer);
const char* lexer_get_error(Lexer *lexer);
void lexer_print_tokens(Token *tokens);

#endif /* AEC_LEXER_H */
