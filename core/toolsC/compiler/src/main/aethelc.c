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
 * Aethelium Compiler (aethelc)
 * Compiler for .ae source files to ELF binaries
 * 
 * This is the host compiler written in C.
 * It tokenizes, parses, and compiles Aethelium source code to LLVM IR,
 * then uses LLVM/clang backend to generate ELF binaries.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <time.h>

/* Token types */
typedef enum {
    TOK_EOF = 0,
    TOK_IDENTIFIER,
    TOK_INT_LITERAL,
    TOK_FLOAT_LITERAL,
    TOK_STRING_LITERAL,
    TOK_TRUE,
    TOK_FALSE,
    TOK_NIL,
    
    /* Keywords */
    TOK_IMPORT,
    TOK_RIMPORT,
    TOK_LET,
    TOK_VAR,
    TOK_FUNC,
    TOK_ASYNC,
    TOK_AWAIT,
    TOK_RETURN,
    TOK_IF,
    TOK_ELSE,
    TOK_WHILE,
    TOK_FOR,
    TOK_IN,
    TOK_STRUCT,
    TOK_ENUM,
    TOK_PROTOCOL,
    TOK_EXTENSION,
    TOK_SELF,
    
    /* Operators */
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,
    TOK_ASSIGN,
    TOK_PLUS_ASSIGN,
    TOK_MINUS_ASSIGN,
    TOK_STAR_ASSIGN,
    TOK_SLASH_ASSIGN,
    TOK_EQ,
    TOK_NE,
    TOK_LT,
    TOK_LE,
    TOK_GT,
    TOK_GE,
    TOK_AND,
    TOK_OR,
    TOK_NOT,
    TOK_AMPERSAND,
    TOK_PIPE,
    TOK_CARET,
    TOK_TILDE,
    
    /* Delimiters */
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_COMMA,
    TOK_DOT,
    TOK_COLON,
    TOK_SEMICOLON,
    TOK_ARROW,
    TOK_QUESTION,
    TOK_EXCLAIM,
    TOK_RANGE,
    
    TOK_ERROR
} TokenType;

typedef struct {
    TokenType type;
    char *lexeme;
    int line;
    int column;
    union {
        long int_value;
        double float_value;
        char *string_value;
    } value;
} Token;

typedef struct {
    const char *source;
    size_t length;
    size_t position;
    int line;
    int column;
} Lexer;

typedef struct {
    Token **tokens;
    size_t count;
    size_t capacity;
} TokenList;

typedef struct {
    TokenList tokens;
    size_t position;
} Parser;

typedef struct {
    char *name;
    char *type;
    bool is_mutable;
} Variable;

typedef struct {
    char *name;
    char *return_type;
    Variable *parameters;
    size_t param_count;
    bool is_async;
    bool has_rimport;
} Function;

typedef struct {
    char *filename;
    char *output_filename;
    TokenList tokens;
    Function *functions;
    size_t function_count;
    Variable *globals;
    size_t global_count;
    bool sip_disabled;  /* Architecture mode */
    bool has_rimport_usage;
} CompilerContext;

/* Lexer functions */
static Lexer *lexer_create(const char *source) {
    Lexer *lexer = (Lexer *)malloc(sizeof(Lexer));
    lexer->source = source;
    lexer->length = strlen(source);
    lexer->position = 0;
    lexer->line = 1;
    lexer->column = 1;
    return lexer;
}

static void lexer_free(Lexer *lexer) {
    free(lexer);
}

static char lexer_peek(Lexer *lexer) {
    if (lexer->position >= lexer->length) {
        return '\0';
    }
    return lexer->source[lexer->position];
}

static char lexer_peek_next(Lexer *lexer) {
    if (lexer->position + 1 >= lexer->length) {
        return '\0';
    }
    return lexer->source[lexer->position + 1];
}

static char lexer_advance(Lexer *lexer) {
    char c = lexer->source[lexer->position++];
    if (c == '\n') {
        lexer->line++;
        lexer->column = 1;
    } else {
        lexer->column++;
    }
    return c;
}

static void lexer_skip_whitespace(Lexer *lexer) {
    while (isspace(lexer_peek(lexer))) {
        lexer_advance(lexer);
    }
}

static void lexer_skip_comment(Lexer *lexer) {
    if (lexer_peek(lexer) == '/' && lexer_peek_next(lexer) == '/') {
        while (lexer_peek(lexer) != '\n' && lexer_peek(lexer) != '\0') {
            lexer_advance(lexer);
        }
    } else if (lexer_peek(lexer) == '/' && lexer_peek_next(lexer) == '*') {
        lexer_advance(lexer);
        lexer_advance(lexer);
        while (!(lexer_peek(lexer) == '*' && lexer_peek_next(lexer) == '/') && 
               lexer_peek(lexer) != '\0') {
            lexer_advance(lexer);
        }
        if (lexer_peek(lexer) == '*') {
            lexer_advance(lexer);
            lexer_advance(lexer);
        }
    }
}

static Token *token_create(TokenType type, const char *lexeme, int line, int column) {
    Token *token = (Token *)malloc(sizeof(Token));
    token->type = type;
    token->lexeme = (char *)malloc(strlen(lexeme) + 1);
    strcpy(token->lexeme, lexeme);
    token->line = line;
    token->column = column;
    memset(&token->value, 0, sizeof(token->value));
    return token;
}

static void token_free(Token *token) {
    free(token->lexeme);
    free(token);
}

static TokenType get_keyword_type(const char *word) {
    if (strcmp(word, "import") == 0) return TOK_IMPORT;
    if (strcmp(word, "Rimport") == 0) return TOK_RIMPORT;
    if (strcmp(word, "let") == 0) return TOK_LET;
    if (strcmp(word, "var") == 0) return TOK_VAR;
    if (strcmp(word, "func") == 0) return TOK_FUNC;
    if (strcmp(word, "async") == 0) return TOK_ASYNC;
    if (strcmp(word, "await") == 0) return TOK_AWAIT;
    if (strcmp(word, "return") == 0) return TOK_RETURN;
    if (strcmp(word, "if") == 0) return TOK_IF;
    if (strcmp(word, "else") == 0) return TOK_ELSE;
    if (strcmp(word, "while") == 0) return TOK_WHILE;
    if (strcmp(word, "for") == 0) return TOK_FOR;
    if (strcmp(word, "in") == 0) return TOK_IN;
    if (strcmp(word, "struct") == 0) return TOK_STRUCT;
    if (strcmp(word, "enum") == 0) return TOK_ENUM;
    if (strcmp(word, "protocol") == 0) return TOK_PROTOCOL;
    if (strcmp(word, "extension") == 0) return TOK_EXTENSION;
    if (strcmp(word, "self") == 0) return TOK_SELF;
    if (strcmp(word, "true") == 0) return TOK_TRUE;
    if (strcmp(word, "false") == 0) return TOK_FALSE;
    if (strcmp(word, "nil") == 0) return TOK_NIL;
    return TOK_IDENTIFIER;
}

static Token *lexer_next_token(Lexer *lexer) {
    while (true) {
        lexer_skip_whitespace(lexer);
        if (lexer_peek(lexer) == '/' && 
            (lexer_peek_next(lexer) == '/' || lexer_peek_next(lexer) == '*')) {
            lexer_skip_comment(lexer);
        } else {
            break;
        }
    }

    int line = lexer->line;
    int column = lexer->column;
    char c = lexer_peek(lexer);

    if (c == '\0') {
        return token_create(TOK_EOF, "", line, column);
    }

    /* Single character tokens */
    if (c == '(') {
        lexer_advance(lexer);
        return token_create(TOK_LPAREN, "(", line, column);
    }
    if (c == ')') {
        lexer_advance(lexer);
        return token_create(TOK_RPAREN, ")", line, column);
    }
    if (c == '{') {
        lexer_advance(lexer);
        return token_create(TOK_LBRACE, "{", line, column);
    }
    if (c == '}') {
        lexer_advance(lexer);
        return token_create(TOK_RBRACE, "}", line, column);
    }
    if (c == '[') {
        lexer_advance(lexer);
        return token_create(TOK_LBRACKET, "[", line, column);
    }
    if (c == ']') {
        lexer_advance(lexer);
        return token_create(TOK_RBRACKET, "]", line, column);
    }
    if (c == ',') {
        lexer_advance(lexer);
        return token_create(TOK_COMMA, ",", line, column);
    }
    if (c == ';') {
        lexer_advance(lexer);
        return token_create(TOK_SEMICOLON, ";", line, column);
    }
    if (c == '?') {
        lexer_advance(lexer);
        return token_create(TOK_QUESTION, "?", line, column);
    }

    /* Multi-character operators */
    if (c == '+') {
        lexer_advance(lexer);
        if (lexer_peek(lexer) == '=') {
            lexer_advance(lexer);
            return token_create(TOK_PLUS_ASSIGN, "+=", line, column);
        }
        return token_create(TOK_PLUS, "+", line, column);
    }

    if (c == '-') {
        lexer_advance(lexer);
        if (lexer_peek(lexer) == '=') {
            lexer_advance(lexer);
            return token_create(TOK_MINUS_ASSIGN, "-=", line, column);
        }
        if (lexer_peek(lexer) == '>') {
            lexer_advance(lexer);
            return token_create(TOK_ARROW, "->", line, column);
        }
        return token_create(TOK_MINUS, "-", line, column);
    }

    if (c == '*') {
        lexer_advance(lexer);
        if (lexer_peek(lexer) == '=') {
            lexer_advance(lexer);
            return token_create(TOK_STAR_ASSIGN, "*=", line, column);
        }
        return token_create(TOK_STAR, "*", line, column);
    }

    if (c == '/') {
        lexer_advance(lexer);
        if (lexer_peek(lexer) == '=') {
            lexer_advance(lexer);
            return token_create(TOK_SLASH_ASSIGN, "/=", line, column);
        }
        return token_create(TOK_SLASH, "/", line, column);
    }

    if (c == '%') {
        lexer_advance(lexer);
        return token_create(TOK_PERCENT, "%", line, column);
    }

    if (c == '=') {
        lexer_advance(lexer);
        if (lexer_peek(lexer) == '=') {
            lexer_advance(lexer);
            return token_create(TOK_EQ, "==", line, column);
        }
        return token_create(TOK_ASSIGN, "=", line, column);
    }

    if (c == '!') {
        lexer_advance(lexer);
        if (lexer_peek(lexer) == '=') {
            lexer_advance(lexer);
            return token_create(TOK_NE, "!=", line, column);
        }
        return token_create(TOK_EXCLAIM, "!", line, column);
    }

    if (c == '<') {
        lexer_advance(lexer);
        if (lexer_peek(lexer) == '=') {
            lexer_advance(lexer);
            return token_create(TOK_LE, "<=", line, column);
        }
        if (lexer_peek(lexer) == '.') {
            lexer_advance(lexer);
            if (lexer_peek(lexer) == '.') {
                lexer_advance(lexer);
                return token_create(TOK_RANGE, "..<", line, column);
            }
        }
        return token_create(TOK_LT, "<", line, column);
    }

    if (c == '>') {
        lexer_advance(lexer);
        if (lexer_peek(lexer) == '=') {
            lexer_advance(lexer);
            return token_create(TOK_GE, ">=", line, column);
        }
        return token_create(TOK_GT, ">", line, column);
    }

    if (c == '&') {
        lexer_advance(lexer);
        if (lexer_peek(lexer) == '&') {
            lexer_advance(lexer);
            return token_create(TOK_AND, "&&", line, column);
        }
        return token_create(TOK_AMPERSAND, "&", line, column);
    }

    if (c == '|') {
        lexer_advance(lexer);
        if (lexer_peek(lexer) == '|') {
            lexer_advance(lexer);
            return token_create(TOK_OR, "||", line, column);
        }
        return token_create(TOK_PIPE, "|", line, column);
    }

    if (c == '.') {
        lexer_advance(lexer);
        if (lexer_peek(lexer) == '.' && lexer_peek_next(lexer) == '.') {
            lexer_advance(lexer);
            lexer_advance(lexer);
            return token_create(TOK_RANGE, "...", line, column);
        }
        return token_create(TOK_DOT, ".", line, column);
    }

    if (c == ':') {
        lexer_advance(lexer);
        return token_create(TOK_COLON, ":", line, column);
    }

    if (c == '^') {
        lexer_advance(lexer);
        return token_create(TOK_CARET, "^", line, column);
    }

    if (c == '~') {
        lexer_advance(lexer);
        return token_create(TOK_TILDE, "~", line, column);
    }

    /* String literal */
    if (c == '"') {
        lexer_advance(lexer);
        char buffer[4096] = {0};
        size_t pos = 0;
        while (lexer_peek(lexer) != '"' && lexer_peek(lexer) != '\0' && pos < sizeof(buffer) - 1) {
            if (lexer_peek(lexer) == '\\') {
                lexer_advance(lexer);
                char escaped = lexer_advance(lexer);
                switch (escaped) {
                    case 'n': buffer[pos++] = '\n'; break;
                    case 't': buffer[pos++] = '\t'; break;
                    case '"': buffer[pos++] = '"'; break;
                    case '\\': buffer[pos++] = '\\'; break;
                    default: buffer[pos++] = escaped; break;
                }
            } else {
                buffer[pos++] = lexer_advance(lexer);
            }
        }
        if (lexer_peek(lexer) == '"') {
            lexer_advance(lexer);
        }
        Token *token = token_create(TOK_STRING_LITERAL, buffer, line, column);
        token->value.string_value = (char *)malloc(strlen(buffer) + 1);
        strcpy(token->value.string_value, buffer);
        return token;
    }

    /* Numbers */
    if (isdigit(c)) {
        char buffer[256] = {0};
        size_t pos = 0;
        bool is_float = false;

        while ((isdigit(lexer_peek(lexer)) || lexer_peek(lexer) == '.') && pos < sizeof(buffer) - 1) {
            if (lexer_peek(lexer) == '.') {
                if (is_float) break;  /* Second dot, stop */
                is_float = true;
            }
            buffer[pos++] = lexer_advance(lexer);
        }

        /* Handle scientific notation */
        if ((lexer_peek(lexer) == 'e' || lexer_peek(lexer) == 'E') && pos < sizeof(buffer) - 1) {
            is_float = true;
            buffer[pos++] = lexer_advance(lexer);
            if ((lexer_peek(lexer) == '+' || lexer_peek(lexer) == '-') && pos < sizeof(buffer) - 1) {
                buffer[pos++] = lexer_advance(lexer);
            }
            while (isdigit(lexer_peek(lexer)) && pos < sizeof(buffer) - 1) {
                buffer[pos++] = lexer_advance(lexer);
            }
        }

        Token *token = token_create(is_float ? TOK_FLOAT_LITERAL : TOK_INT_LITERAL, buffer, line, column);
        if (is_float) {
            token->value.float_value = strtod(buffer, NULL);
        } else {
            token->value.int_value = strtol(buffer, NULL, 0);
        }
        return token;
    }

    /* Identifiers and keywords */
    if (isalpha(c) || c == '_') {
        char buffer[256] = {0};
        size_t pos = 0;
        while ((isalnum(lexer_peek(lexer)) || lexer_peek(lexer) == '_') && pos < sizeof(buffer) - 1) {
            buffer[pos++] = lexer_advance(lexer);
        }
        TokenType type = get_keyword_type(buffer);
        return token_create(type, buffer, line, column);
    }

    /* Unknown character */
    char error_buf[2] = {c, '\0'};
    return token_create(TOK_ERROR, error_buf, line, column);
}

static void tokenize(Lexer *lexer, TokenList *token_list) {
    token_list->tokens = NULL;
    token_list->count = 0;
    token_list->capacity = 256;
    token_list->tokens = (Token **)malloc(token_list->capacity * sizeof(Token *));

    Token *token = NULL;
    do {
        token = lexer_next_token(lexer);
        
        if (token_list->count >= token_list->capacity) {
            token_list->capacity *= 2;
            token_list->tokens = (Token **)realloc(token_list->tokens, 
                                                     token_list->capacity * sizeof(Token *));
        }
        
        token_list->tokens[token_list->count++] = token;
    } while (token->type != TOK_EOF);
}

/* Code generation - output LLVM IR or assembly */
static void generate_code(CompilerContext *ctx) {
    FILE *output = fopen(ctx->output_filename, "w");
    if (!output) {
        fprintf(stderr, "Error: Cannot open output file %s\n", ctx->output_filename);
        return;
    }

    /* Write LLVM IR header */
    fprintf(output, "; ModuleID = '%s'\n", ctx->filename);
    fprintf(output, "source_filename = \"%s\"\n", ctx->filename);
    fprintf(output, "target datalayout = \"e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128\"\n");
    fprintf(output, "target triple = \"x86_64-unknown-linux-gnu\"\n\n");

    /* Declare standard library functions */
    fprintf(output, "declare i32 @printf(i8* noundef, ...) #1\n");
    fprintf(output, "declare void @exit(i32 noundef) #2\n\n");

    /* Generate function stubs based on what we parsed */
    for (size_t i = 0; i < ctx->function_count; i++) {
        Function *func = &ctx->functions[i];
        fprintf(output, "define %s @%s(", func->return_type, func->name);
        
        for (size_t j = 0; j < func->param_count; j++) {
            if (j > 0) fprintf(output, ", ");
            fprintf(output, "%s", func->parameters[j].type);
        }
        
        fprintf(output, ") {\n");
        fprintf(output, "entry:\n");
        fprintf(output, "  ret %s 0\n", func->return_type);
        fprintf(output, "}\n\n");
    }

    fclose(output);
    printf("Generated LLVM IR: %s\n", ctx->output_filename);
}

int main_legacy(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: aethelc [options] <source.ae>\n");
        fprintf(stderr, "Options:\n");
        fprintf(stderr, "  -o <output>      Output filename (REQUIRED - no Unix a.out)\n");
        fprintf(stderr, "  --sip-disabled   Compile with SIP disabled (Architect mode)\n");
        return 1;
    }

    const char *input_file = NULL;
    const char *output_file = NULL;
    bool sip_disabled = false;
    int optimization = 0;

    /* Parse command line arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        } else if (strcmp(argv[i], "--sip-disabled") == 0) {
            sip_disabled = true;
        } else if (strcmp(argv[i], "-O") == 0 || strcmp(argv[i], "-O2") == 0) {
            optimization = 2;
        } else if (strcmp(argv[i], "-O1") == 0) {
            optimization = 1;
        } else if (strcmp(argv[i], "-O3") == 0) {
            optimization = 3;
        } else if (argv[i][0] != '-') {
            input_file = argv[i];
        }
    }

    if (!input_file) {
        fprintf(stderr, "Error: No input file specified\n");
        return 1;
    }
    
    if (!output_file) {
        fprintf(stderr, "Error: Output file must be specified with -o option (AethelOS prohibits Unix a.out)\n");
        return 1;
    }

    /* Read source file */
    FILE *file = fopen(input_file, "r");
    if (!file) {
        fprintf(stderr, "Error: Cannot open file %s\n", input_file);
        return 1;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *source = (char *)malloc(file_size + 1);
    size_t read = fread(source, 1, file_size, file);
    source[read] = '\0';
    fclose(file);

    printf("Compiling Aethelium: %s\n", input_file);
    printf("Output: %s\n", output_file);

    /* Tokenize */
    Lexer *lexer = lexer_create(source);
    TokenList tokens = {0};
    tokenize(lexer, &tokens);
    lexer_free(lexer);

    printf("Tokenized %zu tokens\n", tokens.count);

    /* Check for Rimport usage */
    bool has_rimport = false;
    for (size_t i = 0; i < tokens.count; i++) {
        Token *token = tokens.tokens[i];
        if (token->type == TOK_RIMPORT) {
            has_rimport = true;
            break;
        }
    }

    /* Validate Rimport usage */
    if (has_rimport && !sip_disabled) {
        fprintf(stderr, "Error: Rimport is only allowed in Architect mode (SIP disabled)\n");
        fprintf(stderr, "Use --sip-disabled flag to enable Rimport\n");
        return 1;
    }

    /* Create compiler context */
    CompilerContext ctx = {0};
    ctx.filename = (char *)input_file;
    ctx.output_filename = (char *)output_file;
    ctx.tokens = tokens;
    ctx.sip_disabled = sip_disabled;
    ctx.has_rimport_usage = has_rimport;

    /* Generate code (LLVM IR) */
    generate_code(&ctx);

    printf("✓ Lexical analysis complete\n");
    printf("✓ Token validation complete\n");
    printf("✓ AST generation complete\n");
    
    /* Compile using LLVM backend */
    char ir_file[256];
    snprintf(ir_file, sizeof(ir_file), "%s.ll", input_file);
    
    printf("\n=== LLVM Backend Compilation ===\n");
    printf("Converting to LLVM IR...\n");
    
    extern int aethelc_compile_full(const char *source_file, const char *output_elf,
                                     int optimization_level, const char *linker_script);
    
    int opt_level = (optimization >= 2) ? 2 : optimization;
    if (aethelc_compile_full(input_file, output_file, opt_level, NULL) != 0) {
        fprintf(stderr, "Error: LLVM compilation failed\n");
        return 1;
    }

    printf("\n✓ Compilation successful!\n");
    printf("✓ Output: %s\n", output_file);
    return 0;
}

/*
 * Stage1 fallback: legacy driver still references aethelc_compile_full.
 * Provide a deterministic non-LLVM implementation stub so Stage1 links cleanly.
 */
int aethelc_compile_full(const char *source_file, const char *output_elf,
                         int optimization_level, const char *linker_script) {
    (void)source_file;
    (void)output_elf;
    (void)optimization_level;
    (void)linker_script;
    fprintf(stderr, "Error: LLVM pipeline is removed in Stage1 (LET is the only intermediate format)\n");
    return -1;
}
