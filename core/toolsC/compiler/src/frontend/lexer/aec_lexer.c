/*
 * AethelOS Aethelium Compiler - Lexer Implementation
 * 修复版：支持位移操作符 << 和 >>
 * 范式清洗版：集成 Unix 特征罢工检测
 */

#include "aec_lexer.h"
#include "unix_strike.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

static char current_char(Lexer *lexer) {
    if (lexer->pos >= lexer->length) return '\0';
    return lexer->input[lexer->pos];
}

static char peek_char(Lexer *lexer, int offset) {
    size_t pos = lexer->pos + offset;
    if (pos >= lexer->length) return '\0';
    return lexer->input[pos];
}

static void advance(Lexer *lexer) {
    if (lexer->pos < lexer->length) {
        if (lexer->input[lexer->pos] == '\n') {
            lexer->line++;
            lexer->column = 0;
        } else {
            lexer->column++;
        }
        lexer->pos++;
    }
}

static void skip_whitespace(Lexer *lexer) {
    while (current_char(lexer) && isspace(current_char(lexer)) &&
           current_char(lexer) != '\n') {
        advance(lexer);
    }
}

static void skip_comment(Lexer *lexer) {
    if (current_char(lexer) == '#') {
        while (current_char(lexer) && current_char(lexer) != '\n') {
            advance(lexer);
        }
    }
    else if (current_char(lexer) == '/' && peek_char(lexer, 1) == '/') {
        while (current_char(lexer) && current_char(lexer) != '\n') {
            advance(lexer);
        }
    }
    else if (current_char(lexer) == '/' && peek_char(lexer, 1) == '*') {
        advance(lexer); advance(lexer);
        while (current_char(lexer)) {
            if (current_char(lexer) == '*' && peek_char(lexer, 1) == '/') {
                advance(lexer); advance(lexer);
                break;
            }
            advance(lexer);
        }
    }
}

typedef struct {
    const char *word;
    TokenType type;
} Keyword;

static const Keyword keywords[] = {
    { "import", TK_IMPORT },
    { "rimport", TK_RIMPORT },
    { "use", TK_USE },      /* 新增 */
    { "let", TK_LET },
    { "var", TK_VAR },
    { "const", TK_CONST },  /* 新增 */
    { "func", TK_FUNC },
    { "fn", TK_FUNC },     /* 新增：支持 fn 关键字 */
    { "extern", TK_EXTERN }, /* 新增：支持 extern 关键字 */
    { "async", TK_ASYNC },
    { "await", TK_AWAIT },
    { "return", TK_RETURN },
    { "if", TK_IF },
    { "else", TK_ELSE },
    { "guard", TK_GUARD },
    { "while", TK_WHILE },
    { "for", TK_FOR },
    { "in", TK_IN },
    { "match", TK_MATCH },
    { "struct", TK_STRUCT },
    { "enum", TK_ENUM },
    { "protocol", TK_PROTOCOL },
    { "class", TK_CLASS },
    { "extension", TK_EXTENSION },
    { "switch", TK_SWITCH },
    { "case", TK_CASE },
    { "default", TK_DEFAULT },
    { "defer", TK_DEFER },
    { "as", TK_AS },
    { "break", TK_BREAK },
    { "continue", TK_CONTINUE },
    { "true", TK_TRUE },
    { "false", TK_FALSE },
    { "nil", TK_NIL },
    { "null", TK_NIL },
    { "self", TK_SELF },
    /* 系统层关键字 */
    { "metal", TK_METAL },
    { "ptr", TK_PTR },
    { "view", TK_VIEW },
    { "reg", TK_REG },
    { "unmanaged", TK_UNMANAGED },
    { "asm", TK_ASM },
    { "bytes", TK_BYTES },
    { "syntax", TK_SYNTAX },
    { "using", TK_USING },
    { "map", TK_MAP },
    { "pattern", TK_PATTERN },
    /* 原初层地址类型 - 仅在 Rimport 后可用（但在词法级别全部允许） */
    { "PhysAddr", TK_PHYSADDR },
    { "VirtAddr", TK_VIRTADDR },
    /* 硅基语义关键字 (Silicon Semantics) */
    { "silicon", TK_SILICON },
    { "pipeline", TK_PIPELINE },
    { "phys", TK_PHYS },
    { NULL, 0 }
};

static TokenType check_keyword(const char *text) {
    for (int i = 0; keywords[i].word != NULL; i++) {
        if (strcmp(keywords[i].word, text) == 0) {
            return keywords[i].type;
        }
    }
    return TK_IDENT;
}

/* 工业级别的错误处理函数 */
static void lexer_error(Lexer *lexer, const char *fmt, ...) {
    if (!lexer) return;
    
    va_list args;
    va_start(args, fmt);
    
    /* 立即输出到 stderr */
    fprintf(stderr, "[FATAL LEXER] Line %d, Col %d: ", lexer->line, lexer->column);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    
    /* 存储错误信息 */
    if (lexer->error_count < 32) {
        vsnprintf(lexer->errors[lexer->error_count], 256, fmt, args);
    }
    vsnprintf(lexer->error, sizeof(lexer->error), fmt, args);
    
    va_end(args);
    
    lexer->error_count++;
}

static void add_token(Lexer *lexer, TokenType type, const char *lexeme) {
    if (lexer->token_count >= lexer->token_capacity) {
        lexer->token_capacity *= 2;
        lexer->tokens = realloc(lexer->tokens,
                                lexer->token_capacity * sizeof(Token));
    }
    
    Token *tok = &lexer->tokens[lexer->token_count++];
    tok->type = type;
    tok->lexeme = malloc(strlen(lexeme) + 1);
    strcpy(tok->lexeme, lexeme);
    tok->line = lexer->line;
    tok->column = lexer->column;
    tok->value.int_value = 0;
}

static Token* lexer_scan_tokens(Lexer *lexer) {
    while (current_char(lexer) != '\0') {
        skip_whitespace(lexer);
        
        if (current_char(lexer) == '#' ||
            (current_char(lexer) == '/' &&
            (peek_char(lexer, 1) == '/' || peek_char(lexer, 1) == '*'))) {
            skip_comment(lexer);
            continue;
        }
        
        if (current_char(lexer) == '\0') break;
        
        char c = current_char(lexer);
        char next = peek_char(lexer, 1);
        
        /* 数字 (支持十进制、浮点、十六进制) */
        if (isdigit(c)) {
            int start = lexer->pos;
            int is_float = 0;
            
            // 检查十六进制 0x 前缀
            if (c == '0' && (peek_char(lexer, 1) == 'x' || peek_char(lexer, 1) == 'X')) {
                advance(lexer); // 消耗 '0'
                advance(lexer); // 消耗 'x'
                // 消耗后续的十六进制数字
                while (isxdigit(current_char(lexer))) {
                    advance(lexer);
                }
            } else {
                // 标准十进制/浮点处理
                while (isdigit(current_char(lexer))) {
                    advance(lexer);
                }
                
                if (current_char(lexer) == '.' && isdigit(peek_char(lexer, 1))) {
                    is_float = 1;
                    advance(lexer);
                    while (isdigit(current_char(lexer))) {
                        advance(lexer);
                    }
                }
            }
            
            int len = lexer->pos - start;
            char *num_str = malloc(len + 1);
            strncpy(num_str, &lexer->input[start], len);
            num_str[len] = '\0';
            
            // 只有浮点数标记为 FLOAT，整数(包括Hex)都标记为 INT
            add_token(lexer, is_float ? TK_FLOAT : TK_INT, num_str);
            free(num_str);
        }
        
        else if (c == '"') {
            advance(lexer);
            int start = lexer->pos;
            
            while (current_char(lexer) && current_char(lexer) != '"') {
                if (current_char(lexer) == '\\') {
                    advance(lexer);
                }
                advance(lexer);
            }
            
            int len = lexer->pos - start;
            char *str = malloc(len + 1);
            strncpy(str, &lexer->input[start], len);
            str[len] = '\0';
            
            add_token(lexer, TK_STRING, str);
            free(str);
            
            if (current_char(lexer) == '"') advance(lexer);
        }
        else if (c == '\'') {
            /* 单引号字符串 'char' */
            advance(lexer);
            int start = lexer->pos;
            
            while (current_char(lexer) && current_char(lexer) != '\'') {
                if (current_char(lexer) == '\\') {
                    advance(lexer);
                }
                advance(lexer);
            }
            
            int len = lexer->pos - start;
            char *str = malloc(len + 1);
            strncpy(str, &lexer->input[start], len);
            str[len] = '\0';
            
            add_token(lexer, TK_STRING, str);
            free(str);
            
            if (current_char(lexer) == '\'') advance(lexer);
        }
        else if (isalpha(c) || c == '_') {
            int start = lexer->pos;
            
            while (isalnum(current_char(lexer)) || current_char(lexer) == '_' ||
                   current_char(lexer) == '/' || current_char(lexer) == '-') {
                advance(lexer);
            }
            
            int len = lexer->pos - start;
            char *ident = malloc(len + 1);
            strncpy(ident, &lexer->input[start], len);
            ident[len] = '\0';
            
            /* [TODO-04] 检测标识符中的下划线罢工 */
            if (strike_detect_underscore_identifier(ident, lexer->line, 
                                                     lexer->column)) {
                free(ident);
                exit(COMPILER_STRIKE_CODE);
            }
            

            
            TokenType type = check_keyword(ident);
            add_token(lexer, type, ident);
            free(ident);
        }
        else if (c == '<') {
            if (next == '<') {  /* Left Shift */
                advance(lexer); advance(lexer);
                add_token(lexer, TK_LSHIFT, "<<");
            } else if (next == '=') {
                advance(lexer); advance(lexer);
                add_token(lexer, TK_LE, "<=");
            } else {
                advance(lexer);
                add_token(lexer, TK_LT, "<");
            }
        }
        else if (c == '>') {
            if (next == '>') {  /* Right Shift */
                advance(lexer); advance(lexer);
                add_token(lexer, TK_RSHIFT, ">>");
            } else if (next == '=') {
                advance(lexer); advance(lexer);
                add_token(lexer, TK_GE, ">=");
            } else {
                advance(lexer);
                add_token(lexer, TK_GT, ">");
            }
        }
        else if (c == '+') {
            if (next == '=') {
                advance(lexer); advance(lexer);
                add_token(lexer, TK_PLUS_ASSIGN, "+=");
            } else {
                advance(lexer);
                add_token(lexer, TK_PLUS, "+");
            }
        }
        else if (c == '-') {
            if (next == '=') {
                advance(lexer); advance(lexer);
                add_token(lexer, TK_MINUS_ASSIGN, "-=");
            } else if (next == '>') {
                /* [TODO-02] 检测箭头操作符罢工 */
                strike_detect_arrow("->", lexer->line, lexer->column);
                advance(lexer); advance(lexer);
                add_token(lexer, TK_ARROW, "->");
            } else {
                advance(lexer);
                add_token(lexer, TK_MINUS, "-");
            }
        }
        else if (c == '*') {
            if (next == '=') {
                advance(lexer); advance(lexer);
                add_token(lexer, TK_STAR_ASSIGN, "*=");
            } else {
                advance(lexer);
                add_token(lexer, TK_STAR, "*");
            }
        }
        else if (c == '/') {
            if (next == '=') {
                advance(lexer); advance(lexer);
                add_token(lexer, TK_SLASH_ASSIGN, "/=");
            } else {
                advance(lexer);
                add_token(lexer, TK_SLASH, "/");
            }
        }
        else if (((unsigned char)c) == 0xC3 && ((unsigned char)next) == 0xB7) {
            /* UTF-8 '÷' (U+00F7), required arithmetic division operator */
            advance(lexer);
            advance(lexer);
            add_token(lexer, TK_DIVIDE, "÷");
        }
        else if (c == '\\') {
            /* 反斜杠用于成员访问和枚举情况 */
            advance(lexer);
            add_token(lexer, TK_BACKSLASH, "\\");
        }
        else if (c == '%') {
            advance(lexer);
            add_token(lexer, TK_PERCENT, "%");
        }
        else if (c == '=') {
            if (next == '=') {
                advance(lexer); advance(lexer);
                add_token(lexer, TK_EQ, "==");
            } else {
                advance(lexer);
                add_token(lexer, TK_ASSIGN, "=");
            }
        }
        else if (c == '!') {
            if (next == '=') {
                advance(lexer); advance(lexer);
                add_token(lexer, TK_NE, "!=");
            } else {
                advance(lexer);
                add_token(lexer, TK_EXCLAIM, "!");  // 修改为 TK_EXCLAIM 用于强制解包操作
            }
        }
        else if (c == '&') {
            if (next == '&') {
                advance(lexer); advance(lexer);
                add_token(lexer, TK_AND, "&&");
            } else if (next == '=') {
                advance(lexer); advance(lexer);
                add_token(lexer, TK_AMPERSAND_ASSIGN, "&=");
            } else {
                advance(lexer);
                add_token(lexer, TK_AMPERSAND, "&");
            }
        }
        else if (c == '|') {
            if (next == '|') {
                advance(lexer); advance(lexer);
                add_token(lexer, TK_OR, "||");
            } else if (next == '=') {
                advance(lexer); advance(lexer);
                add_token(lexer, TK_PIPE_ASSIGN, "|=");
            } else {
                advance(lexer);
                add_token(lexer, TK_PIPE, "|");
            }
        }
        else if (c == '^') {
            if (next == '=') {
                advance(lexer); advance(lexer);
                add_token(lexer, TK_CARET_ASSIGN, "^=");
            } else {
                advance(lexer);
                add_token(lexer, TK_CARET, "^");
            }
        }
        else if (c == '~') {
            advance(lexer);
            add_token(lexer, TK_TILDE, "~");
        }
        else if (c == '.') {
            if (next == '.' && peek_char(lexer, 2) == '.') {
                advance(lexer); advance(lexer); advance(lexer);
                add_token(lexer, TK_RANGE3, "...");
            } else if (next == '.' && peek_char(lexer, 2) == '<') {
                // ..< exclusive range
                advance(lexer); advance(lexer); advance(lexer);
                add_token(lexer, TK_RANGE_EXCLUSIVE, "..<");
            } else if (next == '.') {
                advance(lexer); advance(lexer);
                add_token(lexer, TK_RANGE, "..");
            } else {
                /* [TODO-01] 检测单独点号罢工 - 点号用于访问成员 */
                strike_detect_dot_access(".", lexer->line, lexer->column, 0);
                advance(lexer);
                add_token(lexer, TK_DOT, ".");
            }
        }
        else if (c == '(') {
            advance(lexer);
            add_token(lexer, TK_LPAREN, "(");
        }
        else if (c == ')') {
            advance(lexer);
            add_token(lexer, TK_RPAREN, ")");
        }
        else if (c == '{') {
            advance(lexer);
            add_token(lexer, TK_LBRACE, "{");
        }
        else if (c == '}') {
            advance(lexer);
            add_token(lexer, TK_RBRACE, "}");
        }
        else if (c == '[') {
            advance(lexer);
            add_token(lexer, TK_LBRACKET, "[");
        }
        else if (c == ']') {
            advance(lexer);
            add_token(lexer, TK_RBRACKET, "]");
        }
        else if (c == ',') {
            advance(lexer);
            add_token(lexer, TK_COMMA, ",");
        }
        else if (c == ':') {
            advance(lexer);
            // 检查 :: 操作符
            if (peek_char(lexer, 0) == ':') {
                advance(lexer);
                add_token(lexer, TK_SCOPE_RESOLUTION, "::");
            } else {
                add_token(lexer, TK_COLON, ":");
            }
        }
        else if (c == ';') {
            advance(lexer);
            add_token(lexer, TK_SEMICOLON, ";");
        }
        else if (c == '?') {
            advance(lexer);
            // 检查 ?? 操作符
            if (peek_char(lexer, 0) == '?') {
                advance(lexer);
                add_token(lexer, TK_NULL_COALESCE, "??");
            } else {
                add_token(lexer, TK_QUESTION, "?");
            }
        }
        else if (c == '@') {
            advance(lexer);
            add_token(lexer, TK_AT, "@");
        }
        else if (c == '$') {
            advance(lexer);
            add_token(lexer, TK_DOLLAR, "$");
        }
        else if (c == '!') {
            advance(lexer);
            add_token(lexer, TK_EXCLAIM, "!");
        }
        else if (c == '\n') {
            advance(lexer);
            add_token(lexer, TK_NEWLINE, "\\n");
        }
        else {
            advance(lexer);
        }
    }
    
    add_token(lexer, TK_EOF, "");
    return lexer->tokens;
}

Lexer* lexer_create(const char *input) {
    Lexer *lexer = malloc(sizeof(Lexer));
    lexer->input = input;
    lexer->length = strlen(input);
    lexer->pos = 0;
    lexer->line = 1;
    lexer->column = 0;
    lexer->token_capacity = 256;
    lexer->tokens = malloc(lexer->token_capacity * sizeof(Token));
    lexer->token_count = 0;
    strcpy(lexer->error, "");        /* 初始化错误缓冲区 */
    lexer->error_count = 0;          /* 初始化错误计数 */
    memset(lexer->errors, 0, sizeof(lexer->errors));  /* 初始化错误历史 */
    return lexer;
}

void lexer_destroy(Lexer *lexer) {
    for (size_t i = 0; i < lexer->token_count; i++) {
        free(lexer->tokens[i].lexeme);
    }
    free(lexer->tokens);
    free(lexer);
}

Token* lexer_tokenize(Lexer *lexer) {
    return lexer_scan_tokens(lexer);
}

const char* lexer_get_error(Lexer *lexer) {
    if (!lexer) return "";
    return lexer->error;  /* 返回最后的错误信息 */
}

void lexer_print_tokens(Token *tokens) {
    for (int i = 0; tokens[i].type != TK_EOF; i++) {
        printf("[%d:%d] Type=%d Lexeme='%s'\n",
               tokens[i].line, tokens[i].column,
               tokens[i].type, tokens[i].lexeme);
    }
    printf("[EOF]\n");
}
