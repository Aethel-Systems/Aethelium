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
 * AethelOS Aethelium Compiler - Parser Implementation
 * 语法分析器实现：递归下降解析器
 */

#include "aec_parser.h"
#include "ast_string_table.h"
#include "../silicon_semantics.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =====================================================================
 * 辅助函数
 * ===================================================================== */

static Token current_token(Parser *parser) {
    if (!parser || !parser->tokens) {
        Token error_token;
        error_token.type = TK_EOF;
        error_token.lexeme = "";
        error_token.line = 0;
        error_token.column = 0;
        return error_token;
    }
    return parser->tokens[parser->pos];
}

static Token peek_token(Parser *parser, int offset) {
    return parser->tokens[parser->pos + offset];
}

static void advance(Parser *parser) {
    if (current_token(parser).type != TK_EOF) {
        parser->pos++;
    }
}

static int match(Parser *parser, TokenType type) {
    if (current_token(parser).type == type) {
        advance(parser);
        return 1;
    }
    return 0;
}

static int check(Parser *parser, TokenType type) {
    return current_token(parser).type == type;
}

/* 匹配一个闭合 >，支持将 >> 按两个 > 分裂并回放第二个 >。 */
static int match_closing_angle_bracket(Parser *parser) {
    if (parser->has_virtual_gt_token) {
        parser->has_virtual_gt_token = 0;
        return 1;
    }

    if (current_token(parser).type == TK_GT) {
        advance(parser);
        return 1;
    }

    if (current_token(parser).type == TK_RSHIFT) {
        /* 消耗 >> 的第一个 >，将第二个 > 作为虚拟 token 回放给外层。 */
        advance(parser);
        parser->has_virtual_gt_token = 1;
        return 1;
    }

    return 0;
}

/* 跳过换行符后检查token类型 */
static int check_skip_newlines(Parser *parser, TokenType type) {
    int pos = parser->pos;
    while (parser->tokens[pos].type != TK_EOF && parser->tokens[pos].type == TK_NEWLINE) {
        pos++;
    }
    if (parser->tokens[pos].type != TK_EOF) {
        return parser->tokens[pos].type == type;
    }
    return 0;
}

/* 获取跳过换行符后的lookahead token */
static Token peek_token_skip_newlines(Parser *parser) {
    int pos = parser->pos + 1;
    while (parser->tokens[pos].type != TK_EOF && parser->tokens[pos].type == TK_NEWLINE) {
        pos++;
    }
    return parser->tokens[pos];
}

static Token token_at(Parser *parser, int pos) {
    Token empty = {0};
    empty.type = TK_EOF;
    empty.lexeme = "";
    empty.line = 0;
    empty.column = 0;
    if (!parser || !parser->tokens || pos < 0) {
        return empty;
    }
    return parser->tokens[pos];
}

/* 增强的错误报告函数 - 工业级别实现 */
static void error(Parser *parser, const char *msg) {
    Token tok = current_token(parser);
    Token prev = token_at(parser, parser->pos - 1);
    Token next = token_at(parser, parser->pos + 1);
    fprintf(stderr, "[FATAL] Parse error at line %d (col %d): %s. Found token type %d ('%s')\n",
            tok.line, tok.column, msg, tok.type, tok.lexeme);
    fprintf(stderr, "[FATAL] Context: parser_pos=%d prev=(%d,'%s') curr=(%d,'%s') next=(%d,'%s')\n",
            parser->pos,
            prev.type, prev.lexeme ? prev.lexeme : "",
            tok.type, tok.lexeme ? tok.lexeme : "",
            next.type, next.lexeme ? next.lexeme : "");
    
    snprintf(parser->error, sizeof(parser->error),
             "Parse error at line %d (col %d): %s. Found token type %d ('%s')",
             tok.line, tok.column, msg, tok.type, tok.lexeme);
    
    parser->error_count++;
    /* 工业级别：第一个致命错误立即停止编译 */
    if (parser->error_count >= parser->max_errors) {
        fprintf(stderr, "[FATAL] Compilation aborted due to syntax error. No output file generated.\n");
        parser->panic_mode = 1;
    }
}

/* =====================================================================
 * AST 节点创建
 * ===================================================================== */

static void* safe_malloc(size_t size) {
    void *ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "Error: out of memory\n");
        exit(1);
    }
    return ptr;
}

static char* safe_strdup(const char *str) {
    if (!str) return NULL;
    size_t len = strlen(str);
    char *dup = (char*)safe_malloc(len + 1);
    memcpy(dup, str, len);
    dup[len] = '\0';
    return dup;
}

/**
 * 工业级字符串获取函数
 * 首先尝试从字符串表获取字符串，如果不存在则添加
 * 如果Parser没有字符串表，降级为普通的safe_strdup
 */
static const char* parser_intern_string(Parser *parser, const char *str) {
    if (!str) return NULL;
    
    /* 如果Parser有字符串表，使用他进行管理 */
    if (parser && parser->string_table) {
        StringId id = stringtable_add(parser->string_table, str);
        if (STRING_ID_IS_VALID(id)) {
            const char *interned = stringtable_get(parser->string_table, id);
            if (parser->debug) {
                fprintf(stderr, "[TRACE] String '%s' interned with id=%u\n", str, id);
            }
            return interned;
        }
    }
    
    /* 回落到普通分配 */
    return safe_strdup(str);
}

static ASTNode* ast_create_node(ASTNodeType type) {
    ASTNode *node = (ASTNode*)safe_malloc(sizeof(ASTNode));
    memset(node, 0, sizeof(ASTNode));
    node->type = type;
    node->line = 0;
    return node;
}

static char *parse_import_module_spec(Parser *parser) {
    char buffer[1024];
    size_t len = 0;
    buffer[0] = '\0';

    while (!check(parser, TK_NEWLINE) && !check(parser, TK_SEMICOLON) && !check(parser, TK_EOF)) {
        Token tok = current_token(parser);
        switch (tok.type) {
            case TK_IDENT:
            case TK_GT:
            case TK_MINUS:
            case TK_LBRACKET:
            case TK_RBRACKET:
            case TK_COLON:
            case TK_INT:
            case TK_STRING:
                /* 合法的模块标记，继续累积 */ 
                break;
            case TK_SLASH:
                /* 正斜杠属于 Unix 路径分隔符，在 AethelOS 导入中禁止 */
                error(parser, "Forward slash '/' is forbidden in import path");
                return NULL;
            default:
                error(parser, "Invalid token in import module spec");
                return NULL;
        }

        size_t token_len = strlen(tok.lexeme);
        if (len + token_len + 1 >= sizeof(buffer)) {
            error(parser, "Import module spec too long");
            return NULL;
        }
        memcpy(buffer + len, tok.lexeme, token_len);
        len += token_len;
        buffer[len] = '\0';
        advance(parser);
    }

    if (len == 0) {
        error(parser, "Expected module name after import");
        return NULL;
    }
    return safe_strdup(buffer);
}

/* =====================================================================
 * 前向声明
 * ===================================================================== */

static ASTNode* parse_expression(Parser *parser);
static ASTNode* parse_statement(Parser *parser);
static ASTNode* parse_block(Parser *parser);
static ASTNode* parse_type(Parser *parser);

/* =====================================================================
 * 装饰器、Metal/Asm/Bytes 块解析
 * ===================================================================== */

/* 解析属性装饰器 (@packed, @aligned(N), @entry, etc.) */
static void parse_attributes(Parser *parser, char ***attributes, int *attr_count) {
    if (!attributes || !attr_count) return;
    
    *attr_count = 0;
    *attributes = (char**)safe_malloc(sizeof(char*) * 16);
    
    while (check(parser, TK_AT)) {
        Token at_token = current_token(parser);
        if (parser->debug) {
            fprintf(stderr, "[DEBUG] parse_attributes: Found '@' at line %d, col %d\n", 
                    at_token.line, at_token.column);
        }
        
        advance(parser); // 消耗 @
        
        if (!check(parser, TK_IDENT)) {
            Token t = current_token(parser);
            if (parser->debug) {
                fprintf(stderr, "[DEBUG] parse_attributes: Expected IDENT after '@', got token type %d at line %d\n",
                        t.type, t.line);
            }
            error(parser, "Expected attribute name after '@'");
            break;
        }
        
        Token attr_name = current_token(parser);
        if (parser->debug) {
            fprintf(stderr, "[DEBUG] parse_attributes: Parsed attribute name '%s' at line %d\n", 
                    attr_name.lexeme, attr_name.line);
        }
        advance(parser);
        
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "@%s", attr_name.lexeme);
        
        // 检查是否有参数 @aligned(8)
        if (check(parser, TK_LPAREN)) {
            if (parser->debug) {
                fprintf(stderr, "[DEBUG] parse_attributes: Found LPAREN after attribute '%s', parsing parameters\n", 
                        attr_name.lexeme);
            }
            advance(parser);
            // 收集括号内的所有令牌作为参数
            strcat(buffer, "(");
            
            int param_count = 0;
            while (!check(parser, TK_RPAREN) && !check(parser, TK_EOF)) {
                Token param = current_token(parser);
                if (parser->debug) {
                    fprintf(stderr, "[DEBUG]   Attribute param[%d]: token type %d, lexeme='%s'\n",
                            param_count, param.type, param.lexeme);
                }
                param_count++;
                strcat(buffer, param.lexeme);
                advance(parser);
            }
            strcat(buffer, ")");
            
            if (!match(parser, TK_RPAREN)) {
                Token t = current_token(parser);
                if (parser->debug) {
                    fprintf(stderr, "[DEBUG] parse_attributes: Expected RPAREN, got token type %d at line %d\n",
                            t.type, t.line);
                }
                error(parser, "Expected ')' after attribute parameter");
            }
        }
        
        if (*attr_count < 16) {
            (*attributes)[(*attr_count)++] = safe_strdup(buffer);
            if (parser->debug) {
                fprintf(stderr, "[DEBUG] parse_attributes: Stored attribute: %s\n", buffer);
            }
        }
    }
}

/* 解析 metal { ... } 块 */
static ASTNode* parse_metal_block(Parser *parser) {
    if (!match(parser, TK_METAL)) {
        error(parser, "Expected 'metal'");
        return NULL;
    }
    
    if (parser->debug) {
    fprintf(stderr, "[DEBUG] parse_metal_block: Starting metal block parse at position %d\n", parser->pos);
    }

    if (!match(parser, TK_LBRACE)) {
        Token t = current_token(parser);
        if (parser->debug) {
            fprintf(stderr, "[DEBUG] parse_metal_block: Expected LBRACE after 'metal', got token type %d at line %d\n",
                    t.type, t.line);
        }
        error(parser, "Expected '{' after 'metal'");
        return NULL;
    }
    
    if (parser->debug) {
        fprintf(stderr, "[DEBUG] parse_metal_block: Found opening LBRACE, now parsing statements\n");
    }
    
    ASTNode *node = ast_create_node(AST_METAL_BLOCK);
    node->data.metal_block.statements = NULL;
    node->data.metal_block.stmt_count = 0;
    
    int capacity = 16;
    node->data.metal_block.statements = (ASTNode**)safe_malloc(sizeof(ASTNode*) * capacity);
    
    int brace_depth = 1;  // 追踪括号深度
    
    while (!check(parser, TK_RBRACE) && !check(parser, TK_EOF)) {
        while (match(parser, TK_NEWLINE));
        
        if (check(parser, TK_RBRACE)) {
            if (parser->debug) {
                fprintf(stderr, "[DEBUG] parse_metal_block: Found closing RBRACE at position %d\n", parser->pos);
            }
            break;
        }
        
        Token t = current_token(parser);
        if (parser->debug) {
            fprintf(stderr, "[DEBUG] parse_metal_block: Parsing statement at line %d, token type %d ('%s'), brace_depth=%d\n",
                    t.line, t.type, t.lexeme, brace_depth);
        }
        
        int pos_before = parser->pos;
        ASTNode *stmt = parse_statement(parser);
        
        // 注意：跳过括号深度追踪，因为不能访问 parser->tokens
        
        if (stmt) {
            if (node->data.metal_block.stmt_count >= capacity) {
                capacity *= 2;
                node->data.metal_block.statements = 
                    (ASTNode**)realloc(node->data.metal_block.statements, sizeof(ASTNode*) * capacity);
            }
            node->data.metal_block.statements[node->data.metal_block.stmt_count++] = stmt;
            if (parser->debug) {
                fprintf(stderr, "[DEBUG] parse_metal_block: Statement parsed successfully, stmt_count=%d\n",
                        node->data.metal_block.stmt_count);
            }
        } else if (parser->pos == pos_before) {
            /* 如果parse_statement没有推进位置，强制跳过当前token */
            Token t = current_token(parser);
            if (parser->debug) {
                fprintf(stderr, "[DEBUG] parse_metal_block: parse_statement returned NULL and didn't advance, token type %d ('%s')\n",
                        t.type, t.lexeme);
            }
            if (t.type != TK_RBRACE && t.type != TK_EOF) {
                advance(parser);
            }
        }
        
        while (match(parser, TK_NEWLINE) || match(parser, TK_SEMICOLON));
    }
    
    if (parser->debug) {
        fprintf(stderr, "[DEBUG] parse_metal_block: Exited loop, checking for closing RBRACE at position %d\n", parser->pos);
        fprintf(stderr, "[DEBUG] parse_metal_block: Final brace_depth=%d\n", brace_depth);
    }
    
    if (!match(parser, TK_RBRACE)) {
        Token t = current_token(parser);
        if (parser->debug) {
            fprintf(stderr, "[DEBUG] parse_metal_block: ERROR - Expected RBRACE, got token type %d at line %d\n",
                    t.type, t.line);
        }
        error(parser, "Expected '}' after metal block");
    } else if (parser->debug) {
        fprintf(stderr, "[DEBUG] parse_metal_block: Successfully consumed closing RBRACE\n");
    }
    
    return node;
}

/* 解析 asm { ... } 块 */
static ASTNode* parse_asm_block(Parser *parser) {
    if (!match(parser, TK_ASM)) {
        error(parser, "Expected 'asm'");
        return NULL;
    }
    
    if (!match(parser, TK_LBRACE)) {
        error(parser, "Expected '{' after 'asm'");
        return NULL;
    }
    
    ASTNode *node = ast_create_node(AST_ASM_BLOCK);
    char asm_code[8192] = "";
    
    /* 解析asm块内的所有内容直到匹配的}
       支持格式：
       1. asm { "code" }
       2. asm { code }  (raw asm instructions)
       3. asm { "code" : constraints : inputs : clobber }
    */
    int brace_depth = 1;
    int loop_count = 0;
    int max_loop = 10000;
    
    while (brace_depth > 0 && loop_count++ < max_loop && !check(parser, TK_EOF)) {
        Token tok = current_token(parser);
        
        if (tok.type == TK_LBRACE) {
            brace_depth++;
            advance(parser);
        } else if (tok.type == TK_RBRACE) {
            brace_depth--;
            if (brace_depth > 0) {
                advance(parser);
            } else {
                /* 消耗最后的匹配的} */
                advance(parser);
                break;
            }
        } else if (tok.type == TK_STRING) {
            /* 收集字符串作为asm代码，移除引号 */
            if (strlen(asm_code) > 0 && asm_code[strlen(asm_code)-1] != ' ' && 
                asm_code[strlen(asm_code)-1] != '\n') {
                strcat(asm_code, " ");
            }
            const char *str = tok.lexeme;
            if (str[0] == '"' && str[strlen(str)-1] == '"') {
                strncat(asm_code, str + 1, strlen(str) - 2);
            } else {
                strcat(asm_code, str);
            }
            advance(parser);
        } else {
            /* 对于非字符串的token，直接转录其lexeme到asm_code
               这支持old AT&T风格的%0, %%rcx等写法 */
            if (strlen(asm_code) > 0 && asm_code[strlen(asm_code)-1] != ' ' && 
                asm_code[strlen(asm_code)-1] != '\n' && tok.type != TK_NEWLINE) {
                strcat(asm_code, " ");
            }
            if (tok.type != TK_NEWLINE && tok.type != TK_INDENT && 
                tok.type != TK_DEDENT && tok.type != TK_EOF) {
                strcat(asm_code, tok.lexeme);
            }
            advance(parser);
        }
    }
    
    node->data.asm_block.code = safe_strdup(asm_code);
    return node;
}

/* 解析 bytes { ... } 块 */
static ASTNode* parse_bytes_block(Parser *parser) {
    if (!match(parser, TK_BYTES)) {
        error(parser, "Expected 'bytes'");
        return NULL;
    }
    
    if (!match(parser, TK_LBRACE)) {
        error(parser, "Expected '{' after 'bytes'");
        return NULL;
    }
    
    ASTNode *node = ast_create_node(AST_BYTES_BLOCK);
    node->data.bytes_block.hex_strings = (char**)safe_malloc(sizeof(char*) * 32);
    node->data.bytes_block.hex_count = 0;
    
    while (!check(parser, TK_RBRACE) && !check(parser, TK_EOF)) {
        Token tok = current_token(parser);
        
        // 收集十六进制字符串
        if (tok.type == TK_STRING) {
            if (node->data.bytes_block.hex_count < 32) {
                node->data.bytes_block.hex_strings[node->data.bytes_block.hex_count++] = 
                    safe_strdup(tok.lexeme);
            }
            advance(parser);
        } else {
            // 跳过其他令牌（注释、换行等）
            advance(parser);
        }
    }
    
    if (!match(parser, TK_RBRACE)) {
        error(parser, "Expected '}' after bytes block");
    }
    
    return node;
}

/* =====================================================================
 * 解析硅基语义块 (Silicon Semantics)
 * 工业级实现：完整支持微架构配置、流水线编排、暗物质指令、缓存操作
 * ===================================================================== */

/**
 * 解析 silicon { ... } 块
 * 硅基语义允许开发者直接对CPU架构进行声明式控制
 * 
 * 语法示例：
 *   silicon {
 *       using CPU/Current {
 *           MSR/EFER\Syscall/Enable = true
 *           MSR/EFER\Long/Mode = true
 *       }
 *       
 *       pipeline(behavior: .serialize, speculation: .block) {
 *           let secret = Key/Storage\read()
 *           pipeline\barrier(.memory/load/store)
 *       }
 *       
 *       silicon\cache\flush(line)
 *       silicon\prefetch(address, hint: .T0, intent: .write)
 *   }
 */
static ASTNode* parse_silicon_block(Parser *parser) {
    /* 注意：调用者已经advance()到'silicon'关键字之后 */
    /* 如果来自parse_primary，需要进行match；如果来自parse_statement，已经match过了 */
    if (check(parser, TK_SILICON)) {
        advance(parser);  /* 从parse_primary调用时需要此步 */
    }
    
    if (!match(parser, TK_LBRACE)) {
        error(parser, "Expected '{' after 'silicon'");
        return NULL;
    }
    
    ASTNode *node = ast_create_node(AST_SILICON_BLOCK);
    node->data.silicon_block.statements = (ASTNode**)safe_malloc(sizeof(ASTNode*) * 256);
    node->data.silicon_block.stmt_count = 0;
    
    int brace_depth = 1;
    
    while (!check(parser, TK_RBRACE) && !check(parser, TK_EOF)) {
        while (match(parser, TK_NEWLINE));
        
        if (check(parser, TK_RBRACE)) {
            break;
        }
        
        ASTNode *stmt = NULL;
        
        /* 解析微架构配置: using CPU/Current { ... } */
        if (check(parser, TK_USING)) {
            advance(parser);
            
            /* 期望 CPU/Current 或类似的CPU上下文 */
            char cpu_context[256] = "";
            while (check(parser, TK_IDENT) || check(parser, TK_SLASH)) {
                strcat(cpu_context, current_token(parser).lexeme);
                advance(parser);
            }
            
            if (match(parser, TK_LBRACE)) {
                stmt = ast_create_node(AST_MICROARCH_CONFIG);
                stmt->data.microarch_config.cpu_context = safe_strdup(cpu_context);
                stmt->data.microarch_config.register_names = 
                    (char**)safe_malloc(sizeof(char*) * 64);
                stmt->data.microarch_config.property_names = 
                    (char**)safe_malloc(sizeof(char*) * 64);
                stmt->data.microarch_config.values = 
                    (ASTNode**)safe_malloc(sizeof(ASTNode*) * 64);
                stmt->data.microarch_config.config_count = 0;
                
                /* 解析配置项直到} */
                int config_brace = 1;
                while (config_brace > 0 && !check(parser, TK_EOF)) {
                    while (match(parser, TK_NEWLINE));
                    
                    if (check(parser, TK_RBRACE)) {
                        config_brace--;
                        if (config_brace == 0) {
                            advance(parser);
                            break;
                        }
                        advance(parser);
                        continue;
                    }
                    
                    if (check(parser, TK_LBRACE)) {
                        config_brace++;
                        advance(parser);
                        continue;
                    }
                    
                    /* 解析 MSR/EFER\Syscall/Enable = true */
                    if (check(parser, TK_IDENT)) {
                        char msr_name[256] = "";
                        
                        /* 收集MSR名称 (MSR/EFER 等) */
                        while ((check(parser, TK_IDENT) || check(parser, TK_SLASH)) && 
                               !check(parser, TK_BACKSLASH) && !check(parser, TK_ASSIGN)) {
                            strcat(msr_name, current_token(parser).lexeme);
                            advance(parser);
                        }
                        
                        char property_name[256] = "";
                        
                        /* 解析 \Syscall/Enable */
                        if (match(parser, TK_BACKSLASH)) {
                            while ((check(parser, TK_IDENT) || check(parser, TK_SLASH)) && 
                                   !check(parser, TK_ASSIGN)) {
                                strcat(property_name, current_token(parser).lexeme);
                                advance(parser);
                            }
                        }
                        
                        if (match(parser, TK_ASSIGN)) {
                            ASTNode *value = parse_expression(parser);
                            
                            if (stmt->data.microarch_config.config_count < 64) {
                                int idx = stmt->data.microarch_config.config_count;
                                stmt->data.microarch_config.register_names[idx] = 
                                    safe_strdup(msr_name);
                                stmt->data.microarch_config.property_names[idx] = 
                                    safe_strdup(property_name);
                                stmt->data.microarch_config.values[idx] = value;
                                stmt->data.microarch_config.config_count++;
                            }
                        }
                    }
                    
                    while (match(parser, TK_SEMICOLON) || match(parser, TK_NEWLINE));
                }
            }
        }
        
        /* 解析流水线块: pipeline(...) { ... } */
        else if (check(parser, TK_PIPELINE)) {
            advance(parser);
            
            uint32_t behavior_flags = 0;
            uint32_t speculation_flags = 0;
            
            /* 解析参数 (behavior: \serialize 或 behavior: .serialize) */
            if (match(parser, TK_LPAREN)) {
                while (!check(parser, TK_RPAREN) && !check(parser, TK_EOF)) {
                    if (check(parser, TK_IDENT)) {
                        char *param_name = safe_strdup(current_token(parser).lexeme);
                        advance(parser);
                        
                        if (match(parser, TK_COLON)) {
                            /* 解析参数值 (支持 \ 或 . 前缀) */
                            char *param_value = NULL;
                            
                            if (match(parser, TK_BACKSLASH)) {
                                /* 反斜杠前缀: \serialize, \block 等 */
                                if (check(parser, TK_IDENT)) {
                                    param_value = safe_strdup(current_token(parser).lexeme);
                                    advance(parser);
                                }
                            } else if (match(parser, TK_DOT)) {
                                /* 点号前缀: .serialize, .block 等 */
                                if (check(parser, TK_IDENT)) {
                                    param_value = safe_strdup(current_token(parser).lexeme);
                                    advance(parser);
                                }
                            }
                            
                            if (param_value) {
                                if (strcmp(param_name, "behavior") == 0) {
                                    if (strcmp(param_value, "serialize") == 0) {
                                        behavior_flags |= PIPELINE_SERIALIZE;
                                    }
                                } else if (strcmp(param_name, "speculation") == 0) {
                                    if (strcmp(param_value, "block") == 0) {
                                        speculation_flags |= PIPELINE_BLOCK;
                                    }
                                }
                                
                                free(param_value);
                            }
                        }
                        
                        free(param_name);
                    }
                    
                    if (!match(parser, TK_COMMA)) break;
                }
                
                if (!match(parser, TK_RPAREN)) {
                    error(parser, "Expected ')' after pipeline parameters");
                }
            }
            
            /* 解析流水线块体 */
            if (match(parser, TK_LBRACE)) {
                stmt = ast_create_node(AST_PIPELINE_BLOCK);
                stmt->data.pipeline_block.behavior_flags = behavior_flags;
                stmt->data.pipeline_block.speculation_flags = speculation_flags;
                stmt->data.pipeline_block.statements = 
                    (ASTNode**)safe_malloc(sizeof(ASTNode*) * 128);
                stmt->data.pipeline_block.stmt_count = 0;
                
                int pipeline_brace = 1;
                while (pipeline_brace > 0 && !check(parser, TK_EOF)) {
                    while (match(parser, TK_NEWLINE));
                    
                    if (check(parser, TK_RBRACE)) {
                        pipeline_brace--;
                        if (pipeline_brace == 0) {
                            advance(parser);
                            break;
                        }
                        advance(parser);
                        continue;
                    }
                    
                    if (check(parser, TK_LBRACE)) {
                        pipeline_brace++;
                        advance(parser);
                        continue;
                    }
                    
                    /* 解析管线屏障: pipeline\barrier(.memory/load/store) */
                    if (check(parser, TK_IDENT) && strcmp(current_token(parser).lexeme, "pipeline") == 0) {
                        advance(parser);
                        
                        if (match(parser, TK_BACKSLASH)) {
                            if (check(parser, TK_IDENT) && strcmp(current_token(parser).lexeme, "barrier") == 0) {
                                advance(parser);
                                
                                if (match(parser, TK_LPAREN)) {
                                    ASTNode *barrier_node = ast_create_node(AST_PIPELINE_BARRIER);
                                    
                                    /* 收集屏障类型 (支持 \ 或 . 前缀) */
                                    char barrier_type[256] = "";
                                    if (match(parser, TK_BACKSLASH) || match(parser, TK_DOT)) {
                                        while (check(parser, TK_IDENT) || check(parser, TK_SLASH)) {
                                            strcat(barrier_type, current_token(parser).lexeme);
                                            advance(parser);
                                        }
                                    }
                                    
                                    barrier_node->data.pipeline_barrier.barrier_type = 
                                        safe_strdup(barrier_type);
                                    
                                    /* 确定屏障模式 */
                                    if (strstr(barrier_type, "memory") && 
                                        strstr(barrier_type, "load")) {
                                        barrier_node->data.pipeline_barrier.barrier_mode = 
                                            PIPELINE_BARRIER_LOAD;
                                    } else if (strstr(barrier_type, "memory") && 
                                               strstr(barrier_type, "store")) {
                                        barrier_node->data.pipeline_barrier.barrier_mode = 
                                            PIPELINE_BARRIER_STORE;
                                    } else {
                                        barrier_node->data.pipeline_barrier.barrier_mode = 
                                            PIPELINE_BARRIER_FULL;
                                    }
                                    
                                    if (stmt->data.pipeline_block.stmt_count < 128) {
                                        stmt->data.pipeline_block.statements
                                            [stmt->data.pipeline_block.stmt_count++] = 
                                            barrier_node;
                                    }
                                    
                                    if (!match(parser, TK_RPAREN)) {
                                        error(parser, "Expected ')' after barrier");
                                    }
                                }
                            } else if (check(parser, TK_IDENT) && 
                                       strcmp(current_token(parser).lexeme, "hint") == 0) {
                                advance(parser);
                                
                                if (match(parser, TK_LPAREN)) {
                                    ASTNode *hint_node = ast_create_node(AST_PIPELINE_HINT);
                                    
                                    /* 收集提示类型 (支持 \ 或 . 前缀) */
                                    char hint_type[256] = "";
                                    if (match(parser, TK_BACKSLASH) || match(parser, TK_DOT)) {
                                        while (check(parser, TK_IDENT) || check(parser, TK_SLASH)) {
                                            strcat(hint_type, current_token(parser).lexeme);
                                            advance(parser);
                                        }
                                    }
                                    
                                    hint_node->data.pipeline_hint.hint_type = 
                                        safe_strdup(hint_type);
                                    
                                    if (stmt->data.pipeline_block.stmt_count < 128) {
                                        stmt->data.pipeline_block.statements
                                            [stmt->data.pipeline_block.stmt_count++] = 
                                            hint_node;
                                    }
                                    
                                    if (!match(parser, TK_RPAREN)) {
                                        error(parser, "Expected ')' after hint");
                                    }
                                }
                            }
                        }
                    } else {
                        /* 其他语句 */
                        ASTNode *inner_stmt = parse_statement(parser);
                        if (inner_stmt && stmt->data.pipeline_block.stmt_count < 128) {
                            stmt->data.pipeline_block.statements
                                [stmt->data.pipeline_block.stmt_count++] = inner_stmt;
                        }
                    }
                    
                    while (match(parser, TK_SEMICOLON) || match(parser, TK_NEWLINE));
                }
            }
        }
        
        /* 解析缓存和预取操作: silicon\cache\flush, silicon\prefetch 等 */
        else if ((check(parser, TK_SILICON) || (check(parser, TK_IDENT) && strcmp(current_token(parser).lexeme, "silicon") == 0))) {
            int save_pos = parser->pos;  /* 保存位置以便回退 */
            advance(parser);
            
            if (match(parser, TK_BACKSLASH)) {
                if (check(parser, TK_IDENT) && strcmp(current_token(parser).lexeme, "cache") == 0) {
                    advance(parser);
                    
                    if (match(parser, TK_BACKSLASH)) {
                        if (check(parser, TK_IDENT)) {
                            char *cache_op = safe_strdup(current_token(parser).lexeme);
                            advance(parser);
                            
                            if (match(parser, TK_LPAREN)) {
                                stmt = ast_create_node(AST_CACHE_OPERATION);
                                stmt->data.cache_operation.operation = cache_op;
                                stmt->data.cache_operation.target = parse_expression(parser);
                                
                                if (!match(parser, TK_RPAREN)) {
                                    error(parser, "Expected ')' after cache operation");
                                }
                            }
                        }
                    }
                } else if (check(parser, TK_IDENT) && 
                           strcmp(current_token(parser).lexeme, "prefetch") == 0) {
                    advance(parser);
                    
                    if (match(parser, TK_LPAREN)) {
                        stmt = ast_create_node(AST_PREFETCH_OPERATION);
                        stmt->data.prefetch_operation.address = parse_expression(parser);
                        stmt->data.prefetch_operation.hint_type = safe_strdup("NTA");
                        stmt->data.prefetch_operation.write_intent = 0;
                        
                        /* 可选参数 (支持 \ 或 . 前缀) */
                        while (match(parser, TK_COMMA)) {
                            while (match(parser, TK_NEWLINE));
                            if (check(parser, TK_IDENT)) {
                                char *param = safe_strdup(current_token(parser).lexeme);
                                advance(parser);
                                
                                if (match(parser, TK_COLON)) {
                                    if (strcmp(param, "hint") == 0 && (match(parser, TK_BACKSLASH) || match(parser, TK_DOT))) {
                                        if (check(parser, TK_IDENT)) {
                                            stmt->data.prefetch_operation.hint_type = 
                                                safe_strdup(current_token(parser).lexeme);
                                            advance(parser);
                                        }
                                    } else if (strcmp(param, "intent") == 0 && (match(parser, TK_BACKSLASH) || match(parser, TK_DOT))) {
                                        if (check(parser, TK_IDENT)) {
                                            if (strcmp(current_token(parser).lexeme, "write") == 0) {
                                                stmt->data.prefetch_operation.write_intent = 1;
                                            }
                                            advance(parser);
                                        }
                                    }
                                }
                                
                                free(param);
                            }
                        }
                        
                        if (!match(parser, TK_RPAREN)) {
                            error(parser, "Expected ')' after prefetch");
                        }
                    }
                } else {
                    /* silicon\ 但后面不是 cache 或 prefetch，这是一个语法错误 */
                    parser->pos = save_pos;  /* 回退位置 */
                    stmt = parse_statement(parser);
                }
            } else {
                /* silicon 后面不是 \，回退并作为普通语句处理 */
                parser->pos = save_pos;  /* 回退位置 */
                stmt = parse_statement(parser);
            }
        }
        
        /* 通用语句解析 */
        else {
            stmt = parse_statement(parser);
        }
        
        if (stmt && node->data.silicon_block.stmt_count < 256) {
            node->data.silicon_block.statements[node->data.silicon_block.stmt_count++] = stmt;
        }
        
        while (match(parser, TK_SEMICOLON) || match(parser, TK_NEWLINE));
    }
    
    if (!match(parser, TK_RBRACE)) {
        error(parser, "Expected '}' after silicon block");
        return NULL;
    }
    
    return node;
}

/* =====================================================================
 * ASCII 矩形布局解析引擎 - 用于 map 定义中的字段布局
 * 分析 ASCII 艺术定义，提取字段位置、大小和类型信息
 * ===================================================================== */

typedef struct {
    int start_col;      /* 字段开始列 */
    int end_col;        /* 字段结束列（包含） */
    int field_size;     /* 字段的字节大小 */
    char *field_name;   /* 字段名 */
    char *field_type;   /* 字段的Aethelium类型 */
} FieldLayout;

typedef struct {
    FieldLayout *fields;
    int field_count;
    int capacity;
    int base_address;
    int total_size;
} MapLayout;

static void trim_ascii_token(char *text) {
    size_t len;
    size_t start = 0;
    size_t end;
    if (!text) return;
    len = strlen(text);
    if (len == 0) return;
    while (text[start] != '\0' && isspace((unsigned char)text[start])) start++;
    end = strlen(text);
    while (end > start && isspace((unsigned char)text[end - 1])) end--;
    if (start > 0) {
        memmove(text, text + start, end - start);
    }
    text[end - start] = '\0';
}

/* 从ASCII矩形定义中提取字段布局信息
   预期格式示例：
   map ACPI_Table {
     [ AcpiSignature:4 ] [ AcpiLength:u32 ]
     [ AcpiRevision:u8 ] [ Checksum:u8 ]
   }
   
   其中[...]表示字段，字段内容为"名称:类型"或"名称:大小"
*/
static MapLayout* parse_ascii_layout(const char *ascii_def) {
    if (!ascii_def) return NULL;
    
    MapLayout *layout = (MapLayout*)safe_malloc(sizeof(MapLayout));
    layout->fields = (FieldLayout*)safe_malloc(sizeof(FieldLayout) * 32);
    layout->field_count = 0;
    layout->capacity = 32;
    layout->base_address = 0;
    layout->total_size = 0;
    
    char *def_copy = safe_strdup(ascii_def);
    size_t def_len = strlen(def_copy);
    
    int current_offset = 0;
    int in_field = 0;
    int field_start = -1;
    char field_content[256] = "";
    int field_content_idx = 0;
    
    for (size_t i = 0; i < def_len; i++) {
        char c = def_copy[i];
        
        if (c == '[') {
            /* 开始新字段 */
            if (current_offset == 0) {
                current_offset = i;
            }
            in_field = 1;
            field_start = i;
            field_content_idx = 0;
            memset(field_content, 0, sizeof(field_content));
        } else if (c == ']' && in_field) {
            /* 结束当前字段 */
            in_field = 0;
            
            if (layout->field_count >= layout->capacity) {
                layout->capacity *= 2;
                layout->fields = (FieldLayout*)realloc(layout->fields, 
                                                       sizeof(FieldLayout) * layout->capacity);
            }
            
            /* 解析字段内容：格式为 "名称:大小或类型" */
            char *colon_pos = strchr(field_content, ':');
            if (colon_pos) {
                int name_len = colon_pos - field_content;
                char name[128];
                char type_buf[128];
                strncpy(name, field_content, name_len);
                name[name_len] = '\0';
                snprintf(type_buf, sizeof(type_buf), "%s", colon_pos + 1);
                trim_ascii_token(name);
                trim_ascii_token(type_buf);
                if (name[0] == '\0' || type_buf[0] == '\0') {
                    continue;
                }
                
                /* 计算字段大小 */
                int field_size = 0;
                if (strcmp(type_buf, "u8") == 0 || strcmp(type_buf, "i8") == 0) {
                    field_size = 1;
                } else if (strcmp(type_buf, "u16") == 0 || strcmp(type_buf, "i16") == 0) {
                    field_size = 2;
                } else if (strcmp(type_buf, "u32") == 0 || strcmp(type_buf, "i32") == 0) {
                    field_size = 4;
                } else if (strcmp(type_buf, "u64") == 0 || strcmp(type_buf, "i64") == 0) {
                    field_size = 8;
                } else {
                    /* 尝试直接解析为字节数 */
                    field_size = (int)strtol(type_buf, NULL, 10);
                    if (field_size <= 0) field_size = 1;  /* 默认1字节 */
                }
                
                FieldLayout *field = &layout->fields[layout->field_count++];
                field->field_name = safe_strdup(name);
                field->field_type = safe_strdup(type_buf);
                field->start_col = field_start;
                field->end_col = i;
                field->field_size = field_size;
                
                layout->total_size += field_size;
            }
        } else if (in_field && field_content_idx < (int)sizeof(field_content) - 1) {
            /* 累积字段内容 */
            field_content[field_content_idx++] = c;
        }
    }
    
    free(def_copy);
    return layout;
}

/* 解析 map 定义
   语法：
     map NAME at BASE_ADDRESS {
         "ASCII rectangle definition"
     }
   
   示例：
     map ACPI_Table at 0xE0000 {
         "
         [ Signature:4 ] [ Length:u32  ]
         [ Revision:u8 ] [ Checksum:u8 ]
         "
     }
*/
static ASTNode* parse_map_definition(Parser *parser) {
    if (!match(parser, TK_MAP)) {
        error(parser, "Expected 'map'");
        return NULL;
    }
    
    /* 获取 map 名称 */
    if (!check(parser, TK_IDENT)) {
        error(parser, "Expected map name after 'map'");
        return NULL;
    }
    
    Token map_name_token = current_token(parser);
    char *map_name = safe_strdup(map_name_token.lexeme);
    advance(parser);
    
    /* 支持分层名称（如 "ACPI/Table"）*/
    while (check(parser, TK_SLASH) || check(parser, TK_DOT)) {
        advance(parser);
        if (check(parser, TK_IDENT)) {
            char *full_name = (char*)safe_malloc(strlen(map_name) + strlen(current_token(parser).lexeme) + 3);
            sprintf(full_name, "%s/%s", map_name, current_token(parser).lexeme);
            free(map_name);
            map_name = full_name;
            advance(parser);
        }
    }
    
    /* 解析 "at" 关键词和基地址 */
    uint64_t base_address = 0;
    
    /* 检查可选的 "at" 或 "as" 关键词 */
    if (check(parser, TK_IDENT)) {
        const char *ident = current_token(parser).lexeme;
        if (strcmp(ident, "at") == 0 || strcmp(ident, "as") == 0) {
            advance(parser);
        }
    }
    
    /* 读取 base_address - 如果存在 */
    if (check(parser, TK_INT)) {
        base_address = (uint64_t)strtoll(current_token(parser).lexeme, NULL, 0);
        advance(parser);
    }
    
    /* 期望左大括号 */
    if (!match(parser, TK_LBRACE)) {
        error(parser, "Expected '{' after map header");
        free(map_name);
        return NULL;
    }
    
    while (match(parser, TK_NEWLINE));
    
    /* 读取 ASCII 定义字符串 */
    char ascii_def[4096] = "";
    if (check(parser, TK_STRING)) {
        const char *str = current_token(parser).lexeme;
        /* 移除引号 */
        if (str[0] == '"' && str[strlen(str) - 1] == '"') {
            strncpy(ascii_def, str + 1, strlen(str) - 2);
        } else {
            strncpy(ascii_def, str, sizeof(ascii_def) - 1);
        }
        advance(parser);
    }
    
    while (match(parser, TK_NEWLINE));
    
    /* 期望右大括号 */
    if (!match(parser, TK_RBRACE)) {
        error(parser, "Expected '}' after map definition");
        free(map_name);
        return NULL;
    }
    
    /* 创建 AST 节点 */
    ASTNode *node = ast_create_node(AST_MAP_DEF);
    node->data.map_def.name = map_name;
    node->data.map_def.base_address = base_address;
    node->data.map_def.ascii_definition = safe_strdup(ascii_def);
    
    /* 解析 ASCII 布局 */
    MapLayout *layout = parse_ascii_layout(ascii_def);
    if (layout && layout->field_count > 0) {
        node->data.map_def.field_names = (char**)safe_malloc(sizeof(char*) * layout->field_count);
        node->data.map_def.field_types = (char**)safe_malloc(sizeof(char*) * layout->field_count);
        node->data.map_def.field_offsets = (int*)safe_malloc(sizeof(int) * layout->field_count);
        node->data.map_def.field_sizes = (int*)safe_malloc(sizeof(int) * layout->field_count);
        
        node->data.map_def.field_count = layout->field_count;
        
        for (int i = 0; i < layout->field_count; i++) {
            node->data.map_def.field_names[i] = layout->fields[i].field_name;
            node->data.map_def.field_types[i] = layout->fields[i].field_type;
            node->data.map_def.field_offsets[i] = i > 0 ? 
                node->data.map_def.field_offsets[i-1] + node->data.map_def.field_sizes[i-1] : 0;
            node->data.map_def.field_sizes[i] = layout->fields[i].field_size;
        }
        
        free(layout->fields);
        free(layout);
    } else {
        node->data.map_def.field_count = 0;
    }
    
    /* 初始化属性 */
    node->data.map_def.attributes = (char**)safe_malloc(sizeof(char*) * 8);
    node->data.map_def.attr_count = 0;
    node->data.map_def.alignment = 0;
    
    return node;
}

/* 解析 syntax 定义 - 自定义语法扩展
   语法：
     syntax NAME {
         pattern -> action
         pattern -> action
     }
   
   示例：
     syntax DebugOps {
         "dbg::print" -> "call @debug_print"
         "dbg::halt" -> "hlt"
     }
*/
static ASTNode* parse_syntax_definition(Parser *parser) {
    if (!match(parser, TK_SYNTAX)) {
        error(parser, "Expected 'syntax'");
        return NULL;
    }
    
    /* 获取 syntax 块的名称 */
    if (!check(parser, TK_IDENT)) {
        error(parser, "Expected syntax block name");
        return NULL;
    }
    
    Token syntax_name_token = current_token(parser);
    char *syntax_name = safe_strdup(syntax_name_token.lexeme);
    advance(parser);
    
    /* 期望左大括号 */
    if (!match(parser, TK_LBRACE)) {
        error(parser, "Expected '{' after syntax name");
        free(syntax_name);
        return NULL;
    }
    
    /* 创建 AST 节点 */
    ASTNode *node = ast_create_node(AST_SYNTAX_DEF);
    node->data.syntax_def.name = syntax_name;
    node->data.syntax_def.patterns = (char**)safe_malloc(sizeof(char*) * 64);
    node->data.syntax_def.actions = (char**)safe_malloc(sizeof(char*) * 64);
    node->data.syntax_def.pattern_count = 0;
    
    /* 解析模式-动作对 */
    while (!check(parser, TK_RBRACE) && !check(parser, TK_EOF)) {
        while (match(parser, TK_NEWLINE) || match(parser, TK_SEMICOLON));
        
        if (check(parser, TK_RBRACE)) break;
        
        /* 读取模式字符串 */
        if (!check(parser, TK_STRING)) {
            error(parser, "Expected pattern string in syntax block");
            advance(parser);
            continue;
        }
        
        const char *pattern_str = current_token(parser).lexeme;
        /* 移除引号 */
        char pattern[512] = "";
        if (pattern_str[0] == '"' && pattern_str[strlen(pattern_str) - 1] == '"') {
            strncpy(pattern, pattern_str + 1, strlen(pattern_str) - 2);
        } else {
            strncpy(pattern, pattern_str, sizeof(pattern) - 1);
        }
        advance(parser);
        
        /* 期望箭头 */
        if (!match(parser, TK_ARROW)) {
            error(parser, "Expected '->' after pattern");
            continue;
        }
        
        /* 读取动作字符串 */
        if (!check(parser, TK_STRING)) {
            error(parser, "Expected action string after '->'");
            advance(parser);
            continue;
        }
        
        const char *action_str = current_token(parser).lexeme;
        /* 移除引号 */
        char action[512] = "";
        if (action_str[0] == '"' && action_str[strlen(action_str) - 1] == '"') {
            strncpy(action, action_str + 1, strlen(action_str) - 2);
        } else {
            strncpy(action, action_str, sizeof(action) - 1);
        }
        advance(parser);
        
        /* 存储模式-动作对 */
        if (node->data.syntax_def.pattern_count < 64) {
            node->data.syntax_def.patterns[node->data.syntax_def.pattern_count] = safe_strdup(pattern);
            node->data.syntax_def.actions[node->data.syntax_def.pattern_count] = safe_strdup(action);
            node->data.syntax_def.pattern_count++;
        }
        
        while (match(parser, TK_NEWLINE) || match(parser, TK_SEMICOLON));
    }
    
    /* 期望右大括号 */
    if (!match(parser, TK_RBRACE)) {
        error(parser, "Expected '}' after syntax definition");
    }
    
    return node;
}

/* 递归下降解析器 */

static ASTNode* parse_ternary(Parser *parser);  // 新增
static ASTNode* parse_logical_or(Parser *parser);
static ASTNode* parse_logical_and(Parser *parser);
static ASTNode* parse_null_coalesce(Parser *parser);  // 新增
static ASTNode* parse_bitwise_or(Parser *parser);   // 新增
static ASTNode* parse_bitwise_xor(Parser *parser);  // 新增
static ASTNode* parse_bitwise_and(Parser *parser);  // 新增
static ASTNode* parse_equality(Parser *parser);
static ASTNode* parse_relational(Parser *parser);
static ASTNode* parse_range(Parser *parser);        // 新增
static ASTNode* parse_type(Parser *parser);
static ASTNode* parse_type_no_paren(Parser *parser);
/* 新增：专门用于解析泛型参数内部的类型，停止条件是 > 或 , */
static ASTNode* parse_type_in_generic(Parser *parser);
static ASTNode* parse_cast(Parser *parser);         // 新增：as 类型转换
static ASTNode* parse_shift(Parser *parser);        // 新增
static ASTNode* parse_additive(Parser *parser);
static ASTNode* parse_multiplicative(Parser *parser);
static ASTNode* parse_unary(Parser *parser);
static ASTNode* parse_statement(Parser *parser);
/* 硅基语义解析器前向声明 */
static ASTNode* parse_silicon_block(Parser *parser);

static int is_generic_expr_prefix(const char *name) {
    if (!name) return 0;
    return (strcmp(name, "cast") == 0 ||
            strcmp(name, "ptr") == 0 ||
            strcmp(name, "view") == 0 ||
            strcmp(name, "reg") == 0 ||
            strcmp(name, "vector") == 0);
}

static ASTNode* parse_primary(Parser *parser) {
    Token tok = current_token(parser);
    
    /* 系统层块: metal, asm, bytes */
    if (check(parser, TK_METAL)) {
        return parse_metal_block(parser);
    }
    
    if (check(parser, TK_ASM)) {
        return parse_asm_block(parser);
    }
    
    if (check(parser, TK_BYTES)) {
        return parse_bytes_block(parser);
    }
    
    /* 硅基语义块: silicon */
    if (check(parser, TK_SILICON)) {
        return parse_silicon_block(parser);
    }
    
    /* if 表达式（作为三目表达式的替代） */
    if (check(parser, TK_IF)) {
        advance(parser);
        
        ASTNode *node = ast_create_node(AST_IF_STMT);
        
        /* 解析条件 */
        node->data.if_stmt.condition = parse_expression(parser);
        
        /* 在检查LBRACE前跳过换行 */
        while (match(parser, TK_NEWLINE));
        
        /* then 分支必须是块 */
        if (check(parser, TK_LBRACE)) {
            node->data.if_stmt.then_branch = parse_block(parser);
        } else {
            error(parser, "Expected '{' after if condition in if expression");
            node->data.if_stmt.then_branch = NULL;
        }
        
        /* 跳过换行并查看是否有else */
        while (match(parser, TK_NEWLINE));
        
        if (check(parser, TK_ELSE)) {
            advance(parser);
            while (match(parser, TK_NEWLINE));
            
            if (check(parser, TK_LBRACE)) {
                node->data.if_stmt.else_branch = parse_block(parser);
            } else if (check(parser, TK_IF)) {
                /* 链式 if-else if-else */
                node->data.if_stmt.else_branch = parse_primary(parser);
            } else {
                error(parser, "Expected '{' or 'if' after else in if expression");
                node->data.if_stmt.else_branch = NULL;
            }
        }
        
        return node;
    }
    
    /* match 表达式 */
    if (match(parser, TK_MATCH)) {
        ASTNode *node = ast_create_node(AST_MATCH_STMT);
        
        /* 解析 match 表达式 */
        if (check(parser, TK_LPAREN)) {
            advance(parser);
            node->data.match_stmt.expr = parse_expression(parser);
            if (check(parser, TK_RPAREN)) advance(parser);
        } else {
            node->data.match_stmt.expr = parse_expression(parser);
        }
        
        node->data.match_stmt.cases = (ASTNode**)safe_malloc(sizeof(ASTNode*) * 64);
        node->data.match_stmt.case_count = 0;
        node->data.match_stmt.default_case = NULL;
        
        /* 解析 match 块和 case 列表 */
        if (match(parser, TK_LBRACE)) {
            while (!check(parser, TK_RBRACE) && !check(parser, TK_EOF)) {
                while (match(parser, TK_NEWLINE));
                if (check(parser, TK_RBRACE)) break;
                
                /* 解析 case 或 default */
                if (check(parser, TK_CASE)) {
                    advance(parser);
                    
                    /* 创建 case 节点 */
                    ASTNode *case_node = ast_create_node(AST_SWITCH_CASE);
                    case_node->data.switch_case.values = (ASTNode**)safe_malloc(sizeof(ASTNode*) * 16);
                    case_node->data.switch_case.value_count = 0;
                    case_node->data.switch_case.statements = (ASTNode**)safe_malloc(sizeof(ASTNode*) * 10);
                    case_node->data.switch_case.stmt_count = 0;
                    
                    /* 解析 case 模式（可能有多个，用逗号分隔） */
                    do {
                        ASTNode *pattern = parse_expression(parser);
                        if (pattern && case_node->data.switch_case.value_count < 16) {
                            case_node->data.switch_case.values[case_node->data.switch_case.value_count++] = pattern;
                        }
                        while (match(parser, TK_NEWLINE));
                    } while (match(parser, TK_COMMA) && !check(parser, TK_ARROW) && !check(parser, TK_RBRACE));
                    
                    /* 期望 => （箭头操作符） */
                    if (!match(parser, TK_ARROW)) {
                        error(parser, "Expected '=>' after match case pattern");
                        advance(parser);
                    }
                    
                    /* 跳过换行 */
                    while (match(parser, TK_NEWLINE));
                    
                    /* 解析 case 结果表达式或语句 */
                    if (check(parser, TK_LBRACE)) {
                        /* case 体是一个块 */
                        case_node->data.switch_case.statements[0] = parse_block(parser);
                        case_node->data.switch_case.stmt_count = 1;
                    } else {
                        /* case 结果表达式 */
                        ASTNode *result = parse_expression(parser);
                        if (result && case_node->data.switch_case.stmt_count < 10) {
                            case_node->data.switch_case.statements[case_node->data.switch_case.stmt_count++] = result;
                        }
                    }
                    
                    /* 添加 case 到 match */
                    if (node->data.match_stmt.case_count < 64) {
                        node->data.match_stmt.cases[node->data.match_stmt.case_count++] = case_node;
                    }
                    
                    while (match(parser, TK_NEWLINE));
                } else if (check(parser, TK_DEFAULT)) {
                    advance(parser);
                    
                    /* 创建 default 节点 */
                    ASTNode *default_node = ast_create_node(AST_SWITCH_CASE);
                    default_node->data.switch_case.values = NULL;
                    default_node->data.switch_case.value_count = 0;
                    default_node->data.switch_case.statements = (ASTNode**)safe_malloc(sizeof(ASTNode*) * 10);
                    default_node->data.switch_case.stmt_count = 0;
                    
                    /* 期望 => */
                    if (!match(parser, TK_ARROW)) {
                        error(parser, "Expected '=>' after default");
                    }
                    
                    while (match(parser, TK_NEWLINE));
                    
                    /* 解析 default 结果 */
                    if (check(parser, TK_LBRACE)) {
                        default_node->data.switch_case.statements[0] = parse_block(parser);
                        default_node->data.switch_case.stmt_count = 1;
                    } else {
                        ASTNode *result = parse_expression(parser);
                        if (result && default_node->data.switch_case.stmt_count < 10) {
                            default_node->data.switch_case.statements[default_node->data.switch_case.stmt_count++] = result;
                        }
                    }
                    
                    node->data.match_stmt.default_case = default_node;
                    
                    while (match(parser, TK_NEWLINE));
                } else {
                    /* 跳过未知内容 */
                    advance(parser);
                }
            }
            
            /* 期望 } */
            if (!match(parser, TK_RBRACE)) {
                error(parser, "Expected '}' after match cases");
            }
        }
        
        return node;
    }
    
    /* 枚举情况速记 (\CaseName) */
    if (match(parser, TK_BACKSLASH)) {
        while (match(parser, TK_NEWLINE));
        if (!check(parser, TK_IDENT) && !check(parser, TK_PTR) && !check(parser, TK_VIEW) && !check(parser, TK_METAL) && !check(parser, TK_ASM) && !check(parser, TK_BYTES)) {
            error(parser, "Expected identifier after '\\'");
            return NULL;
        }
        Token case_name = current_token(parser);
        advance(parser);
        
        ASTNode *node = ast_create_node(AST_IDENT);
        // 创建一个特殊的标识符表示枚举情况 (\CaseName)
        char buf[256];
        snprintf(buf, sizeof(buf), "\\%s", case_name.lexeme);
        node->data.ident.name = (char *)parser_intern_string(parser, buf);
        return node;
    }
    
    if (tok.type == TK_INT) {
        ASTNode *node = ast_create_node(AST_LITERAL);
        node->data.literal.is_float = 0;
        node->data.literal.is_string = 0;
        // 使用 strtoll(..., NULL, 0) 以支持 0x 前缀自动识别
        node->data.literal.value.int_value = strtoll(tok.lexeme, NULL, 0);
        advance(parser);
        return node;
    }
    
    if (tok.type == TK_FLOAT) {
        ASTNode *node = ast_create_node(AST_LITERAL);
        node->data.literal.is_float = 1;
        node->data.literal.is_string = 0;
        node->data.literal.value.float_value = atof(tok.lexeme);
        advance(parser);
        return node;
    }
    
    if (tok.type == TK_STRING) {
        ASTNode *node = ast_create_node(AST_LITERAL);
        node->data.literal.is_float = 0;
        node->data.literal.is_string = 1;
        /* 保存字符串内容到字符串表并存储指针 */
        node->data.literal.value.str_value = (char *)parser_intern_string(parser, tok.lexeme);
        advance(parser);
        return node;
    }
    
    if (tok.type == TK_TRUE || tok.type == TK_FALSE) {
        ASTNode *node = ast_create_node(AST_LITERAL);
        node->data.literal.is_float = 0;
        node->data.literal.is_string = 0;
        node->data.literal.value.int_value = (tok.type == TK_TRUE) ? 1 : 0;
        advance(parser);
        return node;
    }
    
    if (tok.type == TK_NIL) {
        ASTNode *node = ast_create_node(AST_LITERAL);
        node->data.literal.is_float = 0;
        node->data.literal.is_string = 0;
        node->data.literal.value.int_value = 0;
        advance(parser);
        return node;
    }
    
    /* 隐式参数: $0, $1, $2 等 */
    if (tok.type == TK_DOLLAR) {
        advance(parser); // 消耗 $
        Token next = current_token(parser);
        if (next.type == TK_INT) {
            ASTNode *node = ast_create_node(AST_IMPLICIT_PARAM);
            node->data.implicit_param.index = strtol(next.lexeme, NULL, 10);
            advance(parser);
            return node;
        } else {
            error(parser, "Expected digit after $");
            return NULL;
        }
    }
    
    /* 标识符或关键字作为标识符 (ptr/view/reg 等可用作变量名或泛型前缀) */
    if (tok.type == TK_IDENT || tok.type == TK_PTR || tok.type == TK_VIEW || tok.type == TK_REG) {
        ASTNode *node = ast_create_node(AST_IDENT);
        node->data.ident.name = (char *)parser_intern_string(parser, tok.lexeme);
        advance(parser);
        
        if (parser->debug) {
        fprintf(stderr, "[DEBUG] parse_primary: Parsed identifier '%s' at line %d, pos=%d\n",
                tok.lexeme, tok.line, parser->pos);
        }
        /* 注：不在这里处理泛型参数 <Type>
           let parse_postfix 处理 cast<Type>(expr) 形式
           泛型参数解析应该由 parse_postfix 完成 */
        
        return node;
    }
    
    /* 闭包表达式: { ... } 
     * 注：闭包解析已禁用，因为会与函数体冲突
     * 闭包应该只在特定上下文中解析（如函数参数）
     */
    // if (match(parser, TK_LBRACE)) {
    //     ...
    // }
    
    /* 括号表达式或元组字面量 */
    if (match(parser, TK_LPAREN)) {
        // 可能是空元组 ()、元组 (expr1, expr2) 或分组表达式 (expr)
        
        if (check(parser, TK_RPAREN)) {
            // 空元组 ()
            advance(parser);
            ASTNode *tuple = ast_create_node(AST_TUPLE_LITERAL);
            tuple->data.tuple_literal.elements = NULL;
            tuple->data.tuple_literal.element_count = 0;
            return tuple;
        }
        
        /* 解析第一个表达式 */
        ASTNode *first_expr = parse_expression(parser);
        
        if (check(parser, TK_COMMA)) {
            /* 这是一个元组字面量 (expr1, expr2, ...) */
            ASTNode *tuple = ast_create_node(AST_TUPLE_LITERAL);
            tuple->data.tuple_literal.elements = (ASTNode**)safe_malloc(sizeof(ASTNode*) * 20);
            tuple->data.tuple_literal.elements[0] = first_expr;
            int element_count = 1;
            
            while (match(parser, TK_COMMA) && element_count < 20) {
                while (match(parser, TK_NEWLINE));  /* 跳过换行 */
                if (check(parser, TK_RPAREN)) break;  /* 尾随逗号 */
                tuple->data.tuple_literal.elements[element_count++] = parse_expression(parser);
            }
            
            tuple->data.tuple_literal.element_count = element_count;
            
            if (!match(parser, TK_RPAREN)) {
                error(parser, "Expected ')' after tuple");
            }
            
            return tuple;
        }
        
        /* 这是分组表达式 (expr) */
        if (!match(parser, TK_RPAREN)) {
            error(parser, "Expected ')'");
        }
        return first_expr;
    }
    
    /* 数组和字典字面量: [expr1, expr2, ...] 或 [key: value, ...] 或 [:] */
    if (match(parser, TK_LBRACKET)) {
        ASTNode *array = ast_create_node(AST_LITERAL);
        array->data.literal.is_float = 0;
        array->data.literal.value.int_value = 0;
        
        // 跳过换行
        while (match(parser, TK_NEWLINE));
        
        // 检查是否是空字典 [:]
        if (check(parser, TK_COLON)) {
            advance(parser);  // 消耗 :
            while (match(parser, TK_NEWLINE));
            if (!match(parser, TK_RBRACKET)) {
                error(parser, "Expected ']' after empty dictionary");
            }
            return array;  // 返回空数组（表示空字典）
        }
        
        // 检查是否是空数组 []
        if (!check(parser, TK_RBRACKET)) {
            // 解析第一个元素
            ASTNode *temp_expr = parse_expression(parser);
            (void)temp_expr;  // 表达式结果仅用于验证语法
            
            while (match(parser, TK_NEWLINE));
            
            // 检查是否是字典字面量 [key: value, ...]
            if (check(parser, TK_COLON)) {
                advance(parser);  // 消耗 :
                while (match(parser, TK_NEWLINE));
                
                // 解析值（忽略字典值）
                parse_expression(parser);
                
                while (match(parser, TK_NEWLINE));
                
                // 解析其他键值对
                while (match(parser, TK_COMMA)) {
                    while (match(parser, TK_NEWLINE));
                    if (check(parser, TK_RBRACKET)) break;
                    
                    parse_expression(parser);  // key
                    
                    while (match(parser, TK_NEWLINE));
                    if (match(parser, TK_COLON)) {
                        while (match(parser, TK_NEWLINE));
                        parse_expression(parser);  // value
                    }
                    
                    while (match(parser, TK_NEWLINE));
                }
            } else {
                // 数组字面量 [expr1, expr2, ...]
                // 跳过其他元素
                while (match(parser, TK_COMMA)) {
                    while (match(parser, TK_NEWLINE));
                    if (!check(parser, TK_RBRACKET)) {
                        parse_expression(parser);
                    }
                }
            }
        }
        
        while (match(parser, TK_NEWLINE));
        if (!match(parser, TK_RBRACKET)) {
            error(parser, "Expected ']' to close array/dictionary literal");
        }
        
        return array;
    }
    
    /* 如果在panic mode，则返回NULL以避免进一步解析 */
    if (parser->panic_mode) {
        fprintf(stderr, "Parse error: In panic mode, skipping remaining expressions\n");
        return NULL;
    }
    
    error(parser, "Expected expression");
    return NULL;
}

/*
 * 修复 parse_postfix：
 * 1. 统一处理函数调用和结构体初始化
 * 2. 改进参数列表解析循环，更健壮地处理换行符和逗号
 * 3. 忽略参数标签（identifier + :），将其视为普通值解析
 */
static ASTNode* parse_postfix(Parser *parser) {
    ASTNode *node = parse_primary(parser);
    
    if (!node) return NULL;
    
    while (1) {
        if (match(parser, TK_SCOPE_RESOLUTION)) {
            // 处理 :: 操作符（用于枚举成员访问）
            while (match(parser, TK_NEWLINE));
            if (!check(parser, TK_IDENT) && !check(parser, TK_PTR) && !check(parser, TK_VIEW) && !check(parser, TK_METAL) && !check(parser, TK_ASM) && !check(parser, TK_BYTES)) {
                error(parser, "Expected identifier after '::'");
                return node;
            }
            Token member = current_token(parser);
            advance(parser);
            
            // 创建一个 ACCESS 节点来表示 Type::Member
            ASTNode *access = ast_create_node(AST_ACCESS);
            access->data.access.object = node;
            access->data.access.member = (char *)parser_intern_string(parser, member.lexeme);
            node = access;
        }
        else if (check(parser, TK_LPAREN) && node->type == AST_IDENT && 
                 strcmp(node->data.ident.name, "sizeof") == 0) {
            /* 特殊处理：sizeof(Type) 
               这里 sizeof 后面跟 (Type)，而不是一个表达式
               我们需要跳过括号内的类型而不尝试解析为表达式 */
            Token sizeof_token = current_token(parser);
            if (parser->debug) {
                fprintf(stderr, "[DEBUG] parse_postfix: Found sizeof at line %d, pos=%d\n", 
                        sizeof_token.line, parser->pos);
            }
            
            advance(parser);  // 消耗 (
            
            if (parser->debug) {
                fprintf(stderr, "[DEBUG] parse_postfix: sizeof - consuming opening paren at pos %d\n", parser->pos);
            }
            
            /* 跳过括号内的内容直到匹配的 ) */
            int paren_depth = 1;
            int skip_count = 0;
            while (paren_depth > 0 && !check(parser, TK_EOF)) {
                Token t = current_token(parser);
                if (parser->debug) {
                    fprintf(stderr, "[DEBUG] parse_postfix: sizeof - inside parens, token type %d ('%s'), paren_depth=%d\n",
                            t.type, t.lexeme, paren_depth);
                }
                
                if (check(parser, TK_LPAREN)) {
                    paren_depth++;
                    if (parser->debug) {
                        fprintf(stderr, "[DEBUG] parse_postfix: sizeof - LPAREN found, paren_depth++ = %d\n", paren_depth);
                    }
                    advance(parser);
                } else if (check(parser, TK_RPAREN)) {
                    paren_depth--;
                    if (parser->debug) {
                        fprintf(stderr, "[DEBUG] parse_postfix: sizeof - RPAREN found, paren_depth-- = %d\n", paren_depth);
                    }
                    if (paren_depth > 0) {
                        advance(parser);
                    } else {
                        advance(parser);  // 消耗匹配的 )
                        if (parser->debug) {
                            fprintf(stderr, "[DEBUG] parse_postfix: sizeof - consumed final RPAREN at pos %d\n", parser->pos);
                        }
                        break;
                    }
                } else {
                    advance(parser);
                    skip_count++;
                }
            }
            
            if (parser->debug) {
                fprintf(stderr, "[DEBUG] parse_postfix: sizeof - skipped %d tokens inside parentheses\n", skip_count);
            }
            
            /* 返回一个虚拟节点表示sizeof(Type)的结果
               在代码生成阶段，实际大小会被计算 */
            ASTNode *sizeof_node = ast_create_node(AST_IDENT);
            sizeof_node->data.ident.name = (char *)parser_intern_string(parser, "__sizeof_result__");
            node = sizeof_node;
            if (parser->debug) {
                fprintf(stderr, "[DEBUG] parse_postfix: sizeof - created result node, continuing postfix processing\n");
            }
            /* 继续处理postfix操作（如乘法等） */
        }
        else if (check(parser, TK_LT) && node->type == AST_IDENT &&
                 is_generic_expr_prefix(node->data.ident.name)) {
            /* 泛型表达式处理：identifier<...>
             * 这包括 cast<Type>, ptr<Type>, reg<"rax">, 等等
             * 需要正确处理嵌套的泛型如 cast<ptr<T>> 和 vector<UInt8, 32>
             */
            Token generic_token = current_token(parser);
            if (parser->debug) {
                fprintf(stderr, "[DEBUG] parse_postfix: Found '%s' with < at line %d, pos=%d\n", 
                        node->data.ident.name, generic_token.line, parser->pos);
            }
            
            advance(parser);  // 消耗 <
            
            /* 收集泛型参数直到匹配的 > 
               需要正确处理嵌套的 < > 对和 RSHIFT (>>) */
            int angle_depth = 1;
            int loop_count = 0;
            int max_loop = 1000;
            int generic_token_count = 0;
            char generic_params[1024] = "";
            int generic_len = 0;
            
            if (parser->debug) {
                fprintf(stderr, "[DEBUG] parse_postfix: %s - starting generic parameter collection, angle_depth=1\n",
                        node->data.ident.name);
            }
            
            while (angle_depth > 0 && loop_count++ < max_loop && !check(parser, TK_EOF)) {
                Token t = current_token(parser);
                if (parser->debug) {
                    fprintf(stderr, "[DEBUG] parse_postfix: %s - generic token[%d]: type %d ('%s'), angle_depth=%d\n",
                            node->data.ident.name, generic_token_count, t.type, t.lexeme, angle_depth);
                }
                generic_token_count++;
                
                if (check(parser, TK_LT)) {
                    angle_depth++;
                    if (generic_len < (int)(sizeof(generic_params) - 1)) {
                        generic_params[generic_len++] = '<';
                        generic_params[generic_len] = '\0';
                    }
                    advance(parser);
                } else if (check(parser, TK_GT)) {
                    angle_depth--;
                    if (angle_depth > 0) {
                        if (generic_len < (int)(sizeof(generic_params) - 1)) {
                            generic_params[generic_len++] = '>';
                            generic_params[generic_len] = '\0';
                        }
                        advance(parser);
                    } else {
                        /* 匹配的 >，消耗它并退出 */
                        advance(parser);
                        break;
                    }
                } else if (check(parser, TK_RSHIFT)) {
                    /* >> 在嵌套泛型中表示两个 > */
                    if (angle_depth == 1) {
                        angle_depth = 0;
                        advance(parser);
                        break;
                    } else if (angle_depth > 1) {
                        angle_depth -= 2;
                        if (generic_len < (int)(sizeof(generic_params) - 1)) {
                            generic_params[generic_len++] = '>';
                            generic_params[generic_len] = '\0';
                        }
                        advance(parser);
                    }
                } else if (check(parser, TK_IDENT) || check(parser, TK_INT) || 
                          check(parser, TK_STRING) || check(parser, TK_COMMA) ||
                          check(parser, TK_PTR) || check(parser, TK_VIEW) ||
                          check(parser, TK_STAR)) {
                    /* 添加token到泛型参数 */
                    int token_len = strlen(t.lexeme);
                    if (generic_len + token_len < (int)(sizeof(generic_params) - 1)) {
                        strcat(generic_params, t.lexeme);
                        generic_len += token_len;
                    }
                    advance(parser);
                } else {
                    /* 未识别的token，停止收集 */
                    break;
                }
            }
            
            /* 现在修改node来表示泛型call */
            /* 更新identifier名称以包含泛型参数 */
            char generic_name[1024];
            snprintf(generic_name, sizeof(generic_name), "%s<%s>", 
                     node->data.ident.name, generic_params);
            free(node->data.ident.name);
            node->data.ident.name = safe_strdup(generic_name);
            
            /* 如果后面跟着(...)，这是一个函数调用 */
            if (check(parser, TK_LPAREN)) {
                advance(parser);  // 消耗 (
                
                ASTNode *call = ast_create_node(AST_CALL);
                call->data.call.func = node;
                call->data.call.args = NULL;
                call->data.call.arg_count = 0;
                
                while (match(parser, TK_NEWLINE));
                
                if (!check(parser, TK_RPAREN)) {
                    int capacity = 64;
                    call->data.call.args = malloc(sizeof(ASTNode*) * capacity);
                    int arg_loop_count = 0;
                    int arg_max_loop = 10000;
                    
                    while (1) {
                        if (++arg_loop_count > arg_max_loop) {
                            Token t = current_token(parser);
                            fprintf(stderr, "FATAL: Infinite loop in generic function argument parsing at line %d\n", t.line);
                            error(parser, "Compiler error: Infinite loop in generic function argument parsing");
                            break;
                        }
                        
                        while (match(parser, TK_NEWLINE));
                        if (check(parser, TK_RPAREN)) break;
                        
                        if (call->data.call.arg_count >= capacity) {
                            capacity *= 2;
                            call->data.call.args = realloc(call->data.call.args, sizeof(ASTNode*) * capacity);
                        }
                        
                        int pos_before = parser->pos;
                        ASTNode* arg = parse_expression(parser);
                        if (!arg) {
                            if (parser->pos == pos_before && !check(parser, TK_RPAREN) && !check(parser, TK_EOF)) {
                                advance(parser);
                            }
                            if (check(parser, TK_RPAREN) || check(parser, TK_EOF)) break;
                            continue;
                        }
                        call->data.call.args[call->data.call.arg_count++] = arg;
                        
                        while (match(parser, TK_NEWLINE));
                        if (match(parser, TK_COMMA)) continue;
                        else if (check(parser, TK_RPAREN)) break;
                        else {
                            error(parser, "Expected ',' or ')' after argument");
                            break;
                        }
                    }
                }
                
                while (match(parser, TK_NEWLINE));
                if (!match(parser, TK_RPAREN)) {
                    error(parser, "Expected ')' after generic function arguments");
                }
                node = call;
            }
            /* 否则就是一个泛型表达式（不是函数调用）*/
        }
        else if (check(parser, TK_LT) && node->type == AST_IDENT && 
                 (strcmp(node->data.ident.name, "cast") == 0 || 
                  strcmp(node->data.ident.name, "ptr") == 0)) {
            /* 这段代码现在被上面的通用泛型处理取代，但保留用于向后兼容 */
            /* 特殊处理：cast<Type>(expr) 或 ptr<Type> 形式 */
            
            advance(parser);  // 消耗 <
            
            /* 收集泛型参数直到匹配的 > 
               需要正确处理嵌套的 < > 对 */
            int angle_depth = 1;
            int loop_count = 0;
            int max_loop = 1000;
            int generic_token_count = 0;
            
            if (parser->debug) {
                fprintf(stderr, "[DEBUG] parse_postfix: %s - starting generic parameter collection, angle_depth=1\n",
                        node->data.ident.name);
            }
            
            while (angle_depth > 0 && loop_count++ < max_loop && !check(parser, TK_EOF)) {
                Token t = current_token(parser);
                if (parser->debug) {
                    fprintf(stderr, "[DEBUG] parse_postfix: %s - generic token[%d]: type %d ('%s'), angle_depth=%d\n",
                            node->data.ident.name, generic_token_count, t.type, t.lexeme, angle_depth);
                }
                generic_token_count++;
                
                if (check(parser, TK_LT)) {
                    angle_depth++;
                    if (parser->debug) {
                        fprintf(stderr, "[DEBUG] parse_postfix: %s - LT found, angle_depth++ = %d\n", 
                                node->data.ident.name, angle_depth);
                    }
                    advance(parser);
                } else if (check(parser, TK_GT)) {
                    angle_depth--;
                    if (parser->debug) {
                        fprintf(stderr, "[DEBUG] parse_postfix: %s - GT found, angle_depth-- = %d\n",
                                node->data.ident.name, angle_depth);
                    }
                    if (angle_depth > 0) {
                        advance(parser);
                    } else {
                        /* 匹配的 >，消耗它并退出 */
                        advance(parser);
                        if (parser->debug) {
                            fprintf(stderr, "[DEBUG] parse_postfix: %s - consumed final GT at pos %d\n",
                                    node->data.ident.name, parser->pos);
                        }
                        break;
                    }
                } else if (check(parser, TK_RSHIFT)) {
                    /* >> 可能出现在嵌套泛型中，如 <vector<vector<int>>> 
                       RSHIFT 代表两个 GT，但在泛型处理中我们需要小心处理
                       如果当前 angle_depth == 1，>> 的第一个 > 会关闭外层泛型
                    */
                    if (parser->debug) {
                        fprintf(stderr, "[DEBUG] parse_postfix: %s - RSHIFT found, angle_depth=%d\n",
                                node->data.ident.name, angle_depth);
                    }
                    
                    if (angle_depth == 1) {
                        /* 这个 RSHIFT 的第一个 > 是我们要找的匹配右括号
                           不消耗这个令牌，让调用者处理
                           实际上，我们需要"虚拟"地减少 1 并保持令牌位置
                           但由于我们不能恢复位置，我们必须 advance 并希望正确处理
                        */
                        angle_depth = 0;
                        if (parser->debug) {
                            fprintf(stderr, "[DEBUG] parse_postfix: %s - RSHIFT closes generic at angle_depth=1\\n", 
                                    node->data.ident.name);
                        }
                        break;
                    } else if (angle_depth > 2) {
                        /* 两个 > 都在内部嵌套中 */
                        angle_depth -= 2;
                        advance(parser);
                        if (parser->debug) {
                            fprintf(stderr, "[DEBUG] parse_postfix: %s - consumed RSHIFT, angle_depth now=%d\\n",
                                    node->data.ident.name, angle_depth);
                        }
                    } else {
                        /* angle_depth == 2，RSHIFT 的两个 > 中第一个关闭泛型 */
                        angle_depth = 0;
                        if (parser->debug) {
                            fprintf(stderr, "[DEBUG] parse_postfix: %s - RSHIFT closes generic at angle_depth=2\\n",
                                    node->data.ident.name);
                        }
                        break;
                    }
                } else {
                    advance(parser);
                }
            }
            
            /* 跳过换行 */
            while (match(parser, TK_NEWLINE));
            
            /* 如果是cast<Type>(expr)形式，需要处理后面的(expr) */
            if (strcmp(node->data.ident.name, "cast") == 0 && check(parser, TK_LPAREN)) {
                /* 这是 cast<Type>(expr) - 作为函数调用处理 */
                advance(parser);  // 消耗 (
                
                ASTNode *call = ast_create_node(AST_CALL);
                call->data.call.func = node;
                call->data.call.args = NULL;
                call->data.call.arg_count = 0;
                
                while (match(parser, TK_NEWLINE));
                
                if (!check(parser, TK_RPAREN)) {
                    int capacity = 64;
                    call->data.call.args = malloc(sizeof(ASTNode*) * capacity);
                    int arg_loop_count = 0;
                    int arg_max_loop = 10000;
                    
                    while (1) {
                        if (++arg_loop_count > arg_max_loop) {
                            Token t = current_token(parser);
                            fprintf(stderr, "FATAL: Infinite loop in cast argument parsing at line %d\n", t.line);
                            error(parser, "Compiler error: Infinite loop in cast argument parsing");
                            break;
                        }
                        
                        while (match(parser, TK_NEWLINE));
                        if (check(parser, TK_RPAREN)) break;
                        
                        if (call->data.call.arg_count >= capacity) {
                            capacity *= 2;
                            call->data.call.args = realloc(call->data.call.args, sizeof(ASTNode*) * capacity);
                        }
                        
                        int pos_before = parser->pos;
                        ASTNode* arg = parse_expression(parser);
                        if (!arg) {
                            if (parser->pos == pos_before && !check(parser, TK_RPAREN) && !check(parser, TK_EOF)) {
                                advance(parser);
                            }
                            if (check(parser, TK_RPAREN) || check(parser, TK_EOF)) break;
                            continue;
                        }
                        call->data.call.args[call->data.call.arg_count++] = arg;
                        
                        while (match(parser, TK_NEWLINE));
                        if (match(parser, TK_COMMA)) continue;
                        else if (check(parser, TK_RPAREN)) break;
                        else {
                            error(parser, "Expected ',' or ')' after cast argument");
                            break;
                        }
                    }
                }
                
                while (match(parser, TK_NEWLINE));
                if (!match(parser, TK_RPAREN)) {
                    error(parser, "Expected ')' after cast arguments");
                }
                node = call;
            }
            /* 否则继续处理其他postfix操作 */
        }
        else if (match(parser, TK_LPAREN)) {
            if (parser->debug) {
                fprintf(stderr, "[DEBUG] parse_postfix: Function call detected at pos=%d\n", parser->pos);
            }
            
            ASTNode *call = ast_create_node(AST_CALL);
            call->data.call.func = node;
            call->data.call.args = NULL;
            call->data.call.arg_count = 0;
            
            while (match(parser, TK_NEWLINE));
            
            if (!check(parser, TK_RPAREN)) {
                int capacity = 64;
                call->data.call.args = malloc(sizeof(ASTNode*) * capacity);
                int loop_count = 0;
                int max_loop_iterations = 10000;
                
                while (1) {
                    if (++loop_count > max_loop_iterations) {
                        Token t = current_token(parser);
                        fprintf(stderr, "FATAL: Infinite loop detected in parse_postfix (function call args) at line %d\n", t.line);
                        error(parser, "Compiler error: Infinite loop in argument parsing");
                        break;
                    }
                    
                    while (match(parser, TK_NEWLINE));
                    
                    if (check(parser, TK_RPAREN)) break;
                    
                    /* --- 修复开始：支持参数标签 (label: value) --- */
                    /* 如果是 IDENT + COLON，则跳过标签 */
                    if (check(parser, TK_IDENT)) {
                        Token next = peek_token_skip_newlines(parser);
                        if (next.type == TK_COLON) {
                            advance(parser); // 消耗 label (IDENT)
                            while (match(parser, TK_NEWLINE));
                            advance(parser); // 消耗 : (COLON)
                            while (match(parser, TK_NEWLINE));
                        }
                    }
                    /* --- 修复结束 --- */

                    if (call->data.call.arg_count >= capacity) {
                        capacity *= 2;
                        call->data.call.args = realloc(call->data.call.args, sizeof(ASTNode*) * capacity);
                    }

                    int pos_before = parser->pos;
                    ASTNode* arg = parse_expression(parser);
                    if (!arg) {
                        // 错误恢复：如果位置没有改变，强制推进
                        if (parser->pos == pos_before && !check(parser, TK_RPAREN) && !check(parser, TK_EOF)) {
                            Token t = current_token(parser);
                            fprintf(stderr, "Parse warning: Skipping unparseable argument at line %d token '%s'\n", t.line, t.lexeme);
                            advance(parser);
                        }
                        if (check(parser, TK_RPAREN) || check(parser, TK_EOF)) break;
                        continue;
                    }
                    call->data.call.args[call->data.call.arg_count++] = arg;
                    
                    while (match(parser, TK_NEWLINE));
                    
                    if (match(parser, TK_COMMA)) {
                        continue;
                    } else if (check(parser, TK_RPAREN)) {
                        break;
                    } else {
                        error(parser, "Expected ',' or ')' after argument");
                        break;
                    }
                }
            }
            
            while (match(parser, TK_NEWLINE));
            if (!match(parser, TK_RPAREN)) {
                error(parser, "Expected ')' after arguments");
            }
            node = call;
        }
        else if (match(parser, TK_SLASH)) {
            /* 斜杠路径访问：CPU/Current\Control\CR0 等硬件路径
               支持形式: obj/path1/path2\member\submember
            */
            while (match(parser, TK_NEWLINE));
            
            /* 收集斜杠分隔的路径段 */
            char full_path[512] = "";
            strcat(full_path, "/");
            
            int continue_path = 1;
            while (continue_path && (check(parser, TK_IDENT) || check(parser, TK_SLASH))) {
                if (check(parser, TK_IDENT)) {
                    Token path_seg = current_token(parser);
                    strcat(full_path, path_seg.lexeme);
                    advance(parser);
                    
                    /* 检查下一个是否还是斜杠（继续路径）或反斜杠（成员访问） */
                    while (match(parser, TK_NEWLINE));
                    
                    if (check(parser, TK_SLASH)) {
                        strcat(full_path, "/");
                        advance(parser);
                        while (match(parser, TK_NEWLINE));
                    } else if (check(parser, TK_BACKSLASH)) {
                        /* 转到反斜杠处理 */
                        break;
                    } else {
                        continue_path = 0;
                    }
                } else {
                    continue_path = 0;
                }
            }
            
            /* 现在处理反斜杠成员访问链：\member\submember */
            while (check(parser, TK_BACKSLASH)) {
                advance(parser);  /* 消耗 \ */
                while (match(parser, TK_NEWLINE));
                
                if (!check(parser, TK_IDENT)) {
                    error(parser, "Expected identifier after '\\'");
                    break;
                }
                
                Token member = current_token(parser);
                strcat(full_path, "\\");
                strcat(full_path, member.lexeme);
                advance(parser);
                
                while (match(parser, TK_NEWLINE));
            }
            
            /* 创建访问节点 */
            ASTNode *access = ast_create_node(AST_ACCESS);
            access->data.access.object = node;
            access->data.access.member = safe_strdup(full_path);
            node = access;
        }
        else if (match(parser, TK_BACKSLASH)) {
            /* 反斜杠成员访问：obj\member
               特殊情况：hardware\isa\syscall() 等ISA调用 */
            while (match(parser, TK_NEWLINE));
            if (!check(parser, TK_IDENT) && !check(parser, TK_PTR) && !check(parser, TK_VIEW) && !check(parser, TK_METAL) && !check(parser, TK_ASM) && !check(parser, TK_BYTES)) {
                error(parser, "Expected identifier after '\\'");
                return node;
            }
            Token member = current_token(parser);
            advance(parser);
            
            /* 检查是否是ISA调用：hardware\isa\operation(...) */
            int is_isa_call = 0;
            char isa_operation[256] = "";
            
            if (node->type == AST_IDENT && strcmp(node->data.ident.name, "hardware") == 0 && 
                strcmp(member.lexeme, "isa") == 0) {
                /* 这可能是 hardware\isa\... 模式 */
                if (match(parser, TK_BACKSLASH)) {
                    while (match(parser, TK_NEWLINE));
                    if (check(parser, TK_IDENT)) {
                        Token op = current_token(parser);
                        strcpy(isa_operation, op.lexeme);
                        advance(parser);
                        is_isa_call = 1;
                    }
                }
            }
            
            if (is_isa_call && check(parser, TK_LPAREN)) {
                /* 这是 ISA直通调用 */
                advance(parser);  // 消耗 '('
                
                ASTNode *isa_call = ast_create_node(AST_HW_ISA_CALL);
                isa_call->data.hw_isa_call.isa_operation = safe_strdup(isa_operation);
                isa_call->data.hw_isa_call.operands = malloc(sizeof(ASTNode*) * 16);
                isa_call->data.hw_isa_call.operand_count = 0;
                isa_call->data.hw_isa_call.opcode_length = 0;
                
                while (match(parser, TK_NEWLINE));
                
                if (!check(parser, TK_RPAREN)) {
                    int capacity = 16;
                    int arg_count = 0;
                    
                    while (1) {
                        while (match(parser, TK_NEWLINE));
                        if (check(parser, TK_RPAREN)) break;
                        
                        if (arg_count >= capacity) {
                            capacity *= 2;
                            isa_call->data.hw_isa_call.operands = realloc(isa_call->data.hw_isa_call.operands, sizeof(ASTNode*) * capacity);
                        }
                        
                        ASTNode *arg = parse_expression(parser);
                        if (arg) {
                            isa_call->data.hw_isa_call.operands[arg_count++] = arg;
                        }
                        
                        while (match(parser, TK_NEWLINE));
                        
                        if (!match(parser, TK_COMMA)) break;
                    }
                    
                    isa_call->data.hw_isa_call.operand_count = arg_count;
                }
                
                while (match(parser, TK_NEWLINE));
                if (!match(parser, TK_RPAREN)) {
                    error(parser, "Expected ')' after ISA call arguments");
                }
                
                return isa_call;
            }
            
            /* 非ISA调用的普通反斜杠成员访问 */
            /* 检查是否有泛型参数（如 \read<UInt8, 32>）*/
            if (check(parser, TK_LT)) {
                advance(parser);  // 消耗 '<'
                
                /* 收集泛型参数"UInt8, 32"直到 > 或 >> */
                char generic_str[512] = "";
                int angle_depth = 1;
                
                while (angle_depth > 0 && !check(parser, TK_EOF)) {
                    Token t = current_token(parser);
                    
                    if (t.type == TK_LT) {
                        angle_depth++;
                        strcat(generic_str, t.lexeme);
                        advance(parser);
                    } else if (t.type == TK_GT) {
                        angle_depth--;
                        if (angle_depth > 0) {
                            strcat(generic_str, t.lexeme);
                        }
                        advance(parser);
                    } else if (t.type == TK_RSHIFT) {
                        /* RSHIFT (>>) 代表两个 > */
                        angle_depth -= 2;
                        if (angle_depth >= 1) {
                            strcat(generic_str, ">");
                        }
                        advance(parser);
                    } else if (t.type == TK_COMMA) {
                        strcat(generic_str, ",");
                        advance(parser);
                    } else if (t.type == TK_IDENT || t.type == TK_INT) {
                        if (strlen(generic_str) > 0 && generic_str[strlen(generic_str)-1] != ',') {
                            strcat(generic_str, " ");  /* 在类型和参数之间添加空格 */
                        }
                        strcat(generic_str, t.lexeme);
                        advance(parser);
                    } else {
                        break;  /* 停止收集，可能遇到了无法识别的token */
                    }
                }
                
                /* 现在 node 包含 object，member 包含成员名
                 * 将泛型约束编码到member中 */
                char full_member[512];
                snprintf(full_member, sizeof(full_member), "%s<%s>", member.lexeme, generic_str);
                
                /* 检查是否跟随函数调用 */
                if (check(parser, TK_LPAREN)) {
                    /* 这是一个泛型方法调用 (object\method<T>(...)) */
                    advance(parser);  // 消耗 '('
                    
                    ASTNode *method_call = ast_create_node(AST_CALL);
                    
                    ASTNode *method_access = ast_create_node(AST_ACCESS);
                    method_access->data.access.object = node;
                    method_access->data.access.member = safe_strdup(full_member);
                    
                    method_call->data.call.func = method_access;
                    method_call->data.call.args = NULL;
                    method_call->data.call.arg_count = 0;
                    
                    while (match(parser, TK_NEWLINE));
                    
                    if (!check(parser, TK_RPAREN)) {
                        int capacity = 64;
                        method_call->data.call.args = malloc(sizeof(ASTNode*) * capacity);
                        int loop_count = 0;
                        int max_loop_iterations = 10000;
                        
                        while (1) {
                            if (++loop_count > max_loop_iterations) {
                                Token t = current_token(parser);
                                fprintf(stderr, "FATAL: Infinite loop detected in generic method call args at line %d\n", t.line);
                                break;
                            }
                            
                            while (match(parser, TK_NEWLINE));
                            
                            if (check(parser, TK_RPAREN)) break;
                            
                            int pos_before = parser->pos;
                            ASTNode* arg = parse_expression(parser);
                            if (!arg) {
                                if (parser->pos == pos_before && !check(parser, TK_RPAREN) && !check(parser, TK_EOF)) {
                                    Token t = current_token(parser);
                                    fprintf(stderr, "Parse warning: Skipping unparseable generic method argument at line %d token '%s'\n", t.line, t.lexeme);
                                    advance(parser);
                                }
                                if (check(parser, TK_RPAREN) || check(parser, TK_EOF)) break;
                                continue;
                            }
                            method_call->data.call.args[method_call->data.call.arg_count++] = arg;
                            
                            while (match(parser, TK_NEWLINE));
                            
                            if (match(parser, TK_COMMA)) {
                                continue;
                            } else if (check(parser, TK_RPAREN)) {
                                break;
                            } else {
                                error(parser, "Expected ',' or ')' after argument in generic method call");
                                break;
                            }
                        }
                    }
                    
                    while (match(parser, TK_NEWLINE));
                    if (!match(parser, TK_RPAREN)) {
                        error(parser, "Expected ')' after generic method arguments");
                    }
                    
                    node = method_call;
                } else {
                    /* 没有函数调用，只是泛型成员访问 */
                    ASTNode *access = ast_create_node(AST_ACCESS);
                    access->data.access.object = node;
                    access->data.access.member = safe_strdup(full_member);
                    node = access;
                }
            }
            /* 检查是否是方法调用 (object\method(...)) */
            else if (check(parser, TK_LPAREN)) {
                /* 这是一个方法调用 */
                advance(parser);  // 消耗 '('
                
                ASTNode *method_call = ast_create_node(AST_CALL);
                
                /* 构造一个表示方法访问的节点 */
                ASTNode *method_access = ast_create_node(AST_ACCESS);
                method_access->data.access.object = node;
                method_access->data.access.member = safe_strdup(member.lexeme);
                
                method_call->data.call.func = method_access;
                method_call->data.call.args = NULL;
                method_call->data.call.arg_count = 0;
                
                while (match(parser, TK_NEWLINE));
                
                if (!check(parser, TK_RPAREN)) {
                    int capacity = 64;
                    method_call->data.call.args = malloc(sizeof(ASTNode*) * capacity);
                    int loop_count = 0;
                    int max_loop_iterations = 10000;
                    
                    while (1) {
                        if (++loop_count > max_loop_iterations) {
                            Token t = current_token(parser);
                            fprintf(stderr, "FATAL: Infinite loop detected in parse_postfix (method call args) at line %d\n", t.line);
                            error(parser, "Compiler error: Infinite loop in method argument parsing");
                            break;
                        }
                        
                        while (match(parser, TK_NEWLINE));
                        
                        if (check(parser, TK_RPAREN)) break;
                        
                        /* 支持参数标签 */
                        if (check(parser, TK_IDENT)) {
                            Token next = peek_token_skip_newlines(parser);
                            if (next.type == TK_COLON) {
                                advance(parser);
                                while (match(parser, TK_NEWLINE));
                                advance(parser);
                                while (match(parser, TK_NEWLINE));
                            }
                        }
                        
                        if (method_call->data.call.arg_count >= capacity) {
                            capacity *= 2;
                            method_call->data.call.args = realloc(method_call->data.call.args, sizeof(ASTNode*) * capacity);
                        }
                        
                        int pos_before = parser->pos;
                        ASTNode* arg = parse_expression(parser);
                        if (!arg) {
                            if (parser->pos == pos_before && !check(parser, TK_RPAREN) && !check(parser, TK_EOF)) {
                                Token t = current_token(parser);
                                fprintf(stderr, "Parse warning: Skipping unparseable method argument at line %d token '%s'\n", t.line, t.lexeme);
                                advance(parser);
                            }
                            if (check(parser, TK_RPAREN) || check(parser, TK_EOF)) break;
                            continue;
                        }
                        method_call->data.call.args[method_call->data.call.arg_count++] = arg;
                        
                        while (match(parser, TK_NEWLINE));
                        
                        if (match(parser, TK_COMMA)) {
                            continue;
                        } else if (check(parser, TK_RPAREN)) {
                            break;
                        } else {
                            error(parser, "Expected ',' or ')' after argument in method call");
                            break;
                        }
                    }
                }
                
                while (match(parser, TK_NEWLINE));
                if (!match(parser, TK_RPAREN)) {
                    error(parser, "Expected ')' after method arguments");
                }
                
                node = method_call;
            } else {
                /* 这是一个简单的成员访问 */
                ASTNode *access = ast_create_node(AST_ACCESS);
                access->data.access.object = node;
                access->data.access.member = safe_strdup(member.lexeme);
                node = access;
            }
        }
        else if (match(parser, TK_LBRACKET)) {
            while (match(parser, TK_NEWLINE));
            ASTNode *index_node = parse_expression(parser);
            (void)index_node;  // 索引表达式已在访问节点中处理
            while (match(parser, TK_NEWLINE));
            if (!match(parser, TK_RBRACKET)) error(parser, "Expected ']'");
            ASTNode *access = ast_create_node(AST_ACCESS);
            access->data.access.object = node;
            access->data.access.member = "[index]"; // 内部标记为索引访问
            access->data.access.index_expr = index_node;
            node = access;
        }
        else if (match(parser, TK_ARROW)) {
            /* 指针成员访问：ptr->member 等价于 (*ptr).member */
            while (match(parser, TK_NEWLINE));
            if (!check(parser, TK_IDENT) && !check(parser, TK_PTR) && !check(parser, TK_VIEW) && !check(parser, TK_METAL) && !check(parser, TK_ASM) && !check(parser, TK_BYTES)) {
                error(parser, "Expected identifier after '->'");
                return node;
            }
            Token member = current_token(parser);
            advance(parser);
            
            /* 检查是否是方法调用 (ptr->method(...)) */
            if (check(parser, TK_LPAREN)) {
                /* 这是一个方法调用 */
                advance(parser);  // 消耗 '('
                
                ASTNode *method_call = ast_create_node(AST_CALL);
                
                /* 构造一个表示指针成员访问的节点 */
                ASTNode *method_access = ast_create_node(AST_ACCESS);
                method_access->data.access.object = node;
                char access_str[256];
                snprintf(access_str, sizeof(access_str), "->%s", member.lexeme);
                method_access->data.access.member = safe_strdup(access_str);
                
                method_call->data.call.func = method_access;
                method_call->data.call.args = NULL;
                method_call->data.call.arg_count = 0;
                
                while (match(parser, TK_NEWLINE));
                
                if (!check(parser, TK_RPAREN)) {
                    int capacity = 64;
                    method_call->data.call.args = malloc(sizeof(ASTNode*) * capacity);
                    int loop_count = 0;
                    int max_loop_iterations = 10000;
                    
                    while (1) {
                        if (++loop_count > max_loop_iterations) {
                            Token t = current_token(parser);
                            fprintf(stderr, "FATAL: Infinite loop detected in parse_postfix (dot method args) at line %d\n", t.line);
                            error(parser, "Compiler error: Infinite loop in dot method argument parsing");
                            break;
                        }
                        
                        while (match(parser, TK_NEWLINE));
                        
                        if (check(parser, TK_RPAREN)) break;
                        
                        if (method_call->data.call.arg_count >= capacity) {
                            capacity *= 2;
                            method_call->data.call.args = realloc(method_call->data.call.args, sizeof(ASTNode*) * capacity);
                        }
                        
                        int pos_before = parser->pos;
                        ASTNode* arg = parse_expression(parser);
                        if (!arg) {
                            if (parser->pos == pos_before && !check(parser, TK_RPAREN) && !check(parser, TK_EOF)) {
                                Token t = current_token(parser);
                                fprintf(stderr, "Parse warning: Skipping unparseable dot method argument at line %d token '%s'\n", t.line, t.lexeme);
                                advance(parser);
                            }
                            if (check(parser, TK_RPAREN) || check(parser, TK_EOF)) break;
                            continue;
                        }
                        method_call->data.call.args[method_call->data.call.arg_count++] = arg;
                        
                        while (match(parser, TK_NEWLINE));
                        
                        if (match(parser, TK_COMMA)) {
                            continue;
                        } else if (check(parser, TK_RPAREN)) {
                            break;
                        }
                    }
                }
                
                while (match(parser, TK_NEWLINE));
                if (!match(parser, TK_RPAREN)) {
                    error(parser, "Expected ')' after method arguments");
                }
                
                node = method_call;
            } else {
                /* 这是一个简单的指针成员访问 */
                ASTNode *access = ast_create_node(AST_ACCESS);
                access->data.access.object = node;
                char access_str[256];
                snprintf(access_str, sizeof(access_str), "->%s", member.lexeme);
                access->data.access.member = safe_strdup(access_str);
                node = access;
            }
        }
        else if (match(parser, TK_EXCLAIM)) {
            // 强制解包 !
            ASTNode *unwrap = ast_create_node(AST_FORCE_UNWRAP);
            unwrap->data.force_unwrap.operand = node;
            node = unwrap;
        }
        else {
            break;
        }
    }
    
    /* 在postfix处理完成后，检查是否有struct literal初始化 { field: value, ... }
       这支持诸如 Type { ... } 或 CPUID/Features { ... } 的语法 
       注意：只有类型/标识符相关节点才可以是struct constructor，literals不能 */
    if (check(parser, TK_LBRACE) && node && 
        (node->type == AST_IDENT || node->type == AST_ACCESS || 
         node->type == AST_CALL)) {  /* 只允许标识符、访问表达式、或函数调用作为struct constructor */
        /* 前向查看，判断这是否真的是struct literal而不是块语句
           struct literal: identifier/path { field: expr, ... }  
           块语句: identifier { statements... } */
        
        int is_struct_literal = 0;
        int saved_pos = parser->pos;
        
        /* 检查 { 之后的内容 */
        advance(parser);  // 过 {
        while (match(parser, TK_NEWLINE));  // 跳过换行
        
        Token peek = current_token(parser);
        
        /* 如果是空块或标识符后跟冒号，就是struct literal */
        if (peek.type == TK_RBRACE) {
            is_struct_literal = 1;  // 空 {}
        } else if (peek.type == TK_IDENT) {
            /* 检查标识符后面是否是冒号 */
            advance(parser);
            while (match(parser, TK_NEWLINE));
            Token after_id = current_token(parser);
            if (after_id.type == TK_COLON) {
                is_struct_literal = 1;
            }
        }
        
        parser->pos = saved_pos;  /* 恢复位置 */
        
        if (is_struct_literal) {
            /* 解析struct literal */
            advance(parser);  // 消耗 {
            
            ASTNode *struct_init = ast_create_node(AST_CALL);
            struct_init->data.call.func = node;
            struct_init->data.call.args = malloc(sizeof(ASTNode *) * 64);
            struct_init->data.call.arg_count = 0;
            
            while (!check(parser, TK_RBRACE) && !check(parser, TK_EOF)) {
                while (match(parser, TK_NEWLINE));
                if (check(parser, TK_RBRACE)) break;
                
                Token field_name_tok = current_token(parser);
                if (!check(parser, TK_IDENT)) {
                    error(parser, "Expected field name in struct initialization");
                    break;
                }
                advance(parser);
                
                if (!match(parser, TK_COLON)) {
                    error(parser, "Expected ':' after field name in struct initialization");
                    break;
                }
                
                while (match(parser, TK_NEWLINE));
                
                ASTNode *field_value = parse_expression(parser);
                
                if (field_value) {
                    if (struct_init->data.call.arg_count + 1 < 64) {
                        ASTNode *field_name_node = ast_create_node(AST_IDENT);
                        field_name_node->data.ident.name = (char *)parser_intern_string(parser, field_name_tok.lexeme);
                        struct_init->data.call.args[struct_init->data.call.arg_count++] = field_name_node;
                        struct_init->data.call.args[struct_init->data.call.arg_count++] = field_value;
                    }
                }
                
                while (match(parser, TK_NEWLINE));
                
                if (!match(parser, TK_COMMA)) {
                    if (!check(parser, TK_RBRACE)) {
                        error(parser, "Expected ',' or '}' after field value in struct initialization");
                    }
                }
            }
            
            while (match(parser, TK_NEWLINE));
            if (!match(parser, TK_RBRACE)) {
                error(parser, "Expected '}' after struct fields");
            }
            
            node = struct_init;
        }
    }
    
    return node;
}

static ASTNode* parse_unary(Parser *parser) {
    if (check(parser, TK_EXCLAIM) || check(parser, TK_MINUS) ||
        check(parser, TK_PLUS) || check(parser, TK_TILDE) ||
        check(parser, TK_AMPERSAND) || check(parser, TK_STAR)) {
        Token op = current_token(parser);
        advance(parser);
        
        /* 特殊处理 & 引用操作符 */
        if (op.type == TK_AMPERSAND) {
            ASTNode *node = ast_create_node(AST_REFERENCE);
            node->data.reference.operand = parse_unary(parser);
            return node;
        }
        
        ASTNode *node = ast_create_node(AST_UNARY_OP);
        strncpy(node->data.unary_op.op, op.lexeme, 3);
        node->data.unary_op.op[3] = '\0';
        node->data.unary_op.operand = parse_unary(parser);
        return node;
    }
    
    return parse_postfix(parser);
}

static ASTNode* parse_multiplicative(Parser *parser) {
    ASTNode *left = parse_unary(parser);
    
    while (check(parser, TK_STAR) || check(parser, TK_SLASH) ||
           check(parser, TK_DIVIDE) ||
           check(parser, TK_PERCENT)) {
        Token op = current_token(parser);
        advance(parser);
        
        ASTNode *node = ast_create_node(AST_BINARY_OP);
        strncpy(node->data.binary_op.op, op.lexeme, 3);
        node->data.binary_op.op[3] = '\0';
        node->data.binary_op.left = left;
        node->data.binary_op.right = parse_unary(parser);
        left = node;
    }
    
    return left;
}

static ASTNode* parse_additive(Parser *parser) {
    ASTNode *left = parse_multiplicative(parser);
    
    while (check(parser, TK_PLUS) || check(parser, TK_MINUS)) {
        Token op = current_token(parser);
        advance(parser);
        
        ASTNode *node = ast_create_node(AST_BINARY_OP);
        strncpy(node->data.binary_op.op, op.lexeme, 3);
        node->data.binary_op.op[3] = '\0';
        node->data.binary_op.left = left;
        node->data.binary_op.right = parse_multiplicative(parser);
        left = node;
    }
    
    return left;
}

/* 修复：新增 parse_range 处理 .. 和 ... */
static ASTNode* parse_range(Parser *parser) {
    ASTNode *left = parse_cast(parser);  /* 改为调用 parse_cast */
    
    if (check(parser, TK_RANGE) || check(parser, TK_RANGE3) || check(parser, TK_RANGE_EXCLUSIVE)) {
        Token op = current_token(parser);
        advance(parser);
        
        ASTNode *node = ast_create_node(AST_RANGE);
        node->data.range.start = left;
        
        // 检查是否有end表达式（在切片操作中可能没有）
        // 例如：arr[1..] 或 arr[1...]
        if (check(parser, TK_RBRACKET) || check(parser, TK_COMMA) || check(parser, TK_RPAREN)) {
            // 没有end表达式，这是一个"开放"范围
            node->data.range.end = NULL;
        } else {
            node->data.range.end = parse_cast(parser);  /* 改为调用 parse_cast */
        }
        
        // TK_RANGE3 (..) 和 TK_RANGE (inclusive)，TK_RANGE_EXCLUSIVE (..<) exclusive
        node->data.range.inclusive = (op.type != TK_RANGE_EXCLUSIVE); // exclusive if ..<
        return node;
    }
    
    return left;
}

/* 新增：处理 as 类型转换运算符 */
static ASTNode* parse_cast(Parser *parser) {
    ASTNode *left = parse_shift(parser);
    
    while (check(parser, TK_AS)) {
        advance(parser); // 消耗 'as'
        
        // 解析完整的目标类型（包括指针、数组等）
        ASTNode *target_type = parse_type(parser);
        if (!target_type) {
            error(parser, "Expected type name after 'as'");
            return left;
        }
        
        /* 创建 AST_TYPECAST 节点（工业级正确实现） */
        ASTNode *node = ast_create_node(AST_TYPECAST);
        node->data.typecast.expr = left;
        node->data.typecast.target_type = target_type;
        left = node;
    }
    
    return left;
}

static ASTNode* parse_relational(Parser *parser) {
    ASTNode *left = parse_range(parser); // 调用 parse_range (现在处理 as)
    
    while (check(parser, TK_LT) || check(parser, TK_LE) ||
           check(parser, TK_GT) || check(parser, TK_GE)) {
        Token op = current_token(parser);
        advance(parser);
        
        ASTNode *node = ast_create_node(AST_BINARY_OP);
        strncpy(node->data.binary_op.op, op.lexeme, 3);
        node->data.binary_op.op[3] = '\0';
        node->data.binary_op.left = left;
        node->data.binary_op.right = parse_cast(parser); // 改为 parse_cast
        left = node;
    }
    
    return left;
}

/* 新增：位移运算 << >> */
static ASTNode* parse_shift(Parser *parser) {
    ASTNode *left = parse_additive(parser);
    while (check(parser, TK_LSHIFT) || check(parser, TK_RSHIFT)) {
        Token op = current_token(parser);
        advance(parser);
        ASTNode *node = ast_create_node(AST_BINARY_OP);
        strncpy(node->data.binary_op.op, op.lexeme, 3);
        node->data.binary_op.op[3] = '\0';
        node->data.binary_op.left = left;
        node->data.binary_op.right = parse_additive(parser);
        left = node;
    }
    return left;
}

static ASTNode* parse_equality(Parser *parser) {
    ASTNode *left = parse_relational(parser);
    
    while (check(parser, TK_EQ) || check(parser, TK_NE)) {
        Token op = current_token(parser);
        advance(parser);
        
        ASTNode *node = ast_create_node(AST_BINARY_OP);
        strncpy(node->data.binary_op.op, op.lexeme, 3);
        node->data.binary_op.op[3] = '\0';
        node->data.binary_op.left = left;
        node->data.binary_op.right = parse_relational(parser);
        left = node;
    }
    
    return left;
}

static ASTNode* parse_logical_and(Parser *parser) {
    ASTNode *left = parse_null_coalesce(parser); // 修改这里：从 parse_bitwise_or 改为 parse_null_coalesce
    
    while (check(parser, TK_AND)) {
        Token op = current_token(parser);
        advance(parser);
        
        ASTNode *node = ast_create_node(AST_BINARY_OP);
        strncpy(node->data.binary_op.op, op.lexeme, 3);
        node->data.binary_op.op[3] = '\0';
        node->data.binary_op.left = left;
        node->data.binary_op.right = parse_null_coalesce(parser); // 修改这里
        left = node;
    }
    
    return left;
}

/* Null coalesce 操作符 ?? */
static ASTNode* parse_null_coalesce(Parser *parser) {
    ASTNode *left = parse_bitwise_or(parser);
    
    while (check(parser, TK_NULL_COALESCE)) {
        Token op = current_token(parser);
        advance(parser);
        
        ASTNode *node = ast_create_node(AST_BINARY_OP);
        strncpy(node->data.binary_op.op, op.lexeme, 3);
        node->data.binary_op.op[3] = '\0';
        node->data.binary_op.left = left;
        node->data.binary_op.right = parse_bitwise_or(parser);
        left = node;
    }
    
    return left;
}

static ASTNode* parse_logical_or(Parser *parser) {
    ASTNode *left = parse_logical_and(parser);
    
    while (check(parser, TK_OR)) {
        Token op = current_token(parser);
        advance(parser);
        
        ASTNode *node = ast_create_node(AST_BINARY_OP);
        strncpy(node->data.binary_op.op, op.lexeme, 3);
        node->data.binary_op.op[3] = '\0';
        node->data.binary_op.left = left;
        node->data.binary_op.right = parse_logical_and(parser);
        left = node;
    }
    
    return left;
}

/* 新增：按位或 | */
static ASTNode* parse_bitwise_or(Parser *parser) {
    ASTNode *left = parse_bitwise_xor(parser);
    while (check(parser, TK_PIPE)) {
        Token op = current_token(parser);
        advance(parser);
        ASTNode *node = ast_create_node(AST_BINARY_OP);
        strncpy(node->data.binary_op.op, op.lexeme, 3);
        node->data.binary_op.op[3] = '\0';
        node->data.binary_op.left = left;
        node->data.binary_op.right = parse_bitwise_xor(parser);
        left = node;
    }
    return left;
}

/* 新增：按位异或 ^ */
static ASTNode* parse_bitwise_xor(Parser *parser) {
    ASTNode *left = parse_bitwise_and(parser);
    while (check(parser, TK_CARET)) {
        Token op = current_token(parser);
        advance(parser);
        ASTNode *node = ast_create_node(AST_BINARY_OP);
        strncpy(node->data.binary_op.op, op.lexeme, 3);
        node->data.binary_op.op[3] = '\0';
        node->data.binary_op.left = left;
        node->data.binary_op.right = parse_bitwise_and(parser);
        left = node;
    }
    return left;
}

/* 新增：按位与 & (解决报错的关键) */
static ASTNode* parse_bitwise_and(Parser *parser) {
    ASTNode *left = parse_equality(parser);
    while (check(parser, TK_AMPERSAND)) {
        Token op = current_token(parser);
        advance(parser);
        ASTNode *node = ast_create_node(AST_BINARY_OP);
        strncpy(node->data.binary_op.op, op.lexeme, 3);
        node->data.binary_op.op[3] = '\0';
        node->data.binary_op.left = left;
        node->data.binary_op.right = parse_equality(parser);
        left = node;
    }
    return left;
}

static ASTNode* parse_expression(Parser *parser) {
    // 如果在panic mode，返回NULL以避免继续解析
    if (parser->panic_mode) {
        return NULL;
    }
    
    /* 在expression前跳过换行 */
    while (match(parser, TK_NEWLINE));
    
    Token expr_start = current_token(parser);
    if (parser->debug) {
        fprintf(stderr, "[DEBUG] parse_expression: Starting at line %d, token type %d ('%s'), pos=%d\n",
                expr_start.line, expr_start.type, expr_start.lexeme, parser->pos);
    }
    
    ASTNode *left = parse_ternary(parser);
    
    if (parser->debug) {
        fprintf(stderr, "[DEBUG] parse_expression: parse_ternary returned %s at pos=%d\n",
                (left ? "node" : "NULL"), parser->pos);
    }
    
    /* 在检查赋值前跳过换行 */
    while (match(parser, TK_NEWLINE));
    
    /* 处理赋值和复合赋值 */
    if (check(parser, TK_ASSIGN) || check(parser, TK_PLUS_ASSIGN) ||
        check(parser, TK_MINUS_ASSIGN) || check(parser, TK_STAR_ASSIGN) ||
        check(parser, TK_SLASH_ASSIGN) || check(parser, TK_PIPE_ASSIGN) ||
        check(parser, TK_AMPERSAND_ASSIGN) || check(parser, TK_CARET_ASSIGN)) {
        advance(parser);  // 操作符类型已由条件检查确定
        
        /* 在赋值值前跳过换行 */
        while (match(parser, TK_NEWLINE));
        
        ASTNode *node = ast_create_node(AST_ASSIGNMENT);
        node->data.assignment.left = left;
        // 右侧不应该包含 else 关键字
        node->data.assignment.right = parse_ternary(parser);
        return node;
    }
    
    return left;
}

/* 三元条件操作符 (cond ? true_expr : false_expr) */
static ASTNode* parse_ternary(Parser *parser) {
    ASTNode *left = parse_logical_or(parser);
    
    if (match(parser, TK_QUESTION)) {
        ASTNode *true_expr = parse_logical_or(parser);
        
        if (!match(parser, TK_COLON)) {
            error(parser, "Expected ':' in ternary expression");
        }
        
        ASTNode *false_expr = parse_ternary(parser);  // 右结合
        
        // 创建一个特殊的三元节点（暂时用二元操作符模拟）
        ASTNode *node = ast_create_node(AST_BINARY_OP);
        strcpy(node->data.binary_op.op, "?:");
        node->data.binary_op.left = left;
        // 注意：我们需要一个三元节点结构，暂时使用嵌套二元的hack
        ASTNode *ternary_result = ast_create_node(AST_BINARY_OP);
        strcpy(ternary_result->data.binary_op.op, ":");
        ternary_result->data.binary_op.left = true_expr;
        ternary_result->data.binary_op.right = false_expr;
        node->data.binary_op.right = ternary_result;
        
        return node;
    }
    
    return left;
}

static ASTNode* parse_type(Parser *parser) {
    // 尝试解析元组类型 (Type1, Type2, ...)
    // 检查是否是元组：( 后面跟 Identifier 然后是逗号
    if (check(parser, TK_LPAREN)) {
        int pos = parser->pos;  // 保存位置以便回溯
        advance(parser);  // consume '('
        
        // 跳过初始的换行
        while (match(parser, TK_NEWLINE));
        
        // 尝试解析第一个类型
        if (check(parser, TK_IDENT) || check(parser, TK_STAR) || check(parser, TK_LBRACKET)) {
            // 可能是元组类型，尝试解析
            ASTNode *first_type = parse_type_no_paren(parser);  // 不递归处理括号
            
            // 如果后面跟着逗号，确实是元组类型
            while (match(parser, TK_NEWLINE));
            
            if (check(parser, TK_COMMA)) {
                char tuple_str[512];
                strcpy(tuple_str, first_type->data.type.name);
                
                while (match(parser, TK_COMMA)) {
                    while (match(parser, TK_NEWLINE));
                    
                    if (check(parser, TK_RPAREN)) break;
                    
                    ASTNode *elem_type = parse_type_no_paren(parser);
                    strcat(tuple_str, ",");
                    strcat(tuple_str, elem_type->data.type.name);
                    
                    while (match(parser, TK_NEWLINE));
                }
                
                if (!match(parser, TK_RPAREN)) {
                    error(parser, "Expected ')' after tuple type");
                }
                
                ASTNode *node = ast_create_node(AST_TYPE);
                node->data.type.name = malloc(strlen(tuple_str) + 3);
                sprintf(node->data.type.name, "(%s)", tuple_str);
                node->data.type.type_params = NULL;
                node->data.type.type_param_count = 0;
                return node;
            } else if (check(parser, TK_RPAREN)) {
                // 单元素 (Type)
                if (!match(parser, TK_RPAREN)) {
                    error(parser, "Expected ')' after type");
                }
                return first_type;
            }
        }
        
        // 不是类型，回溯
        parser->pos = pos;
    }
    
    return parse_type_no_paren(parser);
}

/* 解析数组大小表达式（支持算术运算如 1024 * 512）
 * 这是为了支持 Makefile 中的多维数组声明，如 [1024 * 512]u8
 */
static char* parse_array_size_expression(Parser *parser) {
    char size_expr[256] = {0};
    size_t expr_len = 0;
    int paren_depth = 0;  // 用于跟踪括号深度
    (void)paren_depth;  // 避免未使用变量警告（某些编译路径可能不使用）
    
    /* 收集从当前位置到 ] 的所有字符，处理表达式 */
    while (current_token(parser).type != TK_RBRACKET && 
           current_token(parser).type != TK_EOF &&
           expr_len < sizeof(size_expr) - 1) {
        
        Token tok = current_token(parser);
        
        /* 支持数字、变量名、和算术运算符 */
        if (tok.type == TK_INT) {
            int written = snprintf(size_expr + expr_len, sizeof(size_expr) - expr_len, "%s", tok.lexeme);
            if (written > 0) expr_len += written;
            advance(parser);
        } else if (tok.type == TK_IDENT) {
            int written = snprintf(size_expr + expr_len, sizeof(size_expr) - expr_len, "%s", tok.lexeme);
            if (written > 0) expr_len += written;
            advance(parser);
        } else if (tok.type == TK_STAR) {
            int written = snprintf(size_expr + expr_len, sizeof(size_expr) - expr_len, "*");
            if (written > 0) expr_len += written;
            advance(parser);
        } else if (tok.type == TK_PLUS) {
            int written = snprintf(size_expr + expr_len, sizeof(size_expr) - expr_len, "+");
            if (written > 0) expr_len += written;
            advance(parser);
        } else if (tok.type == TK_MINUS) {
            int written = snprintf(size_expr + expr_len, sizeof(size_expr) - expr_len, "-");
            if (written > 0) expr_len += written;
            advance(parser);
        } else if (tok.type == TK_SLASH) {
            int written = snprintf(size_expr + expr_len, sizeof(size_expr) - expr_len, "/");
            if (written > 0) expr_len += written;
            advance(parser);
        } else if (tok.type == TK_LPAREN) {
            int written = snprintf(size_expr + expr_len, sizeof(size_expr) - expr_len, "(");
            if (written > 0) expr_len += written;
            paren_depth++;
            advance(parser);
        } else if (tok.type == TK_RPAREN) {
            int written = snprintf(size_expr + expr_len, sizeof(size_expr) - expr_len, ")");
            if (written > 0) expr_len += written;
            paren_depth--;
            advance(parser);
        } else {
            /* 遇到不支持的token，表达式结束 */
            break;
        }
    }
    
    if (expr_len == 0) {
        error(parser, "Expected array size expression");
        return "0";
    }
    
    char *result = malloc(expr_len + 1);
    strcpy(result, size_expr);
    return result;
}

/* 处理不以括号开头的类型 */
static ASTNode* parse_type_no_paren(Parser *parser) {
    /* 处理硬件层装饰器（如@volatile）在类型前面 */
    char decorators[256] = "";
    while (check(parser, TK_AT)) {
        advance(parser);  // 消耗 '@'
        
        if (check(parser, TK_IDENT)) {
            Token decorator = current_token(parser);
            advance(parser);
            
            if (strlen(decorators) > 0) {
                strcat(decorators, " ");
            }
            strcat(decorators, "@");
            strcat(decorators, decorator.lexeme);
        } else {
            error(parser, "Expected decorator name after '@'");
        }
    }
    
    /* 处理数组和字典类型 [Type] 或 [KeyType: ValueType] 或 [Size]Type 或 [*]Type 或 [Type;Size] */
    if (check(parser, TK_LBRACKET)) {
        advance(parser);  // consume '['
        
        /* 检查是否是不定大小数组指针 [*] */
        if (check(parser, TK_STAR) && peek_token(parser, 1).type == TK_RBRACKET) {
            advance(parser);  // consume '*'
            advance(parser);  // consume ']'
            /* 现在解析指向的类型 */
            ASTNode *elem_type = parse_type_no_paren(parser);
            
            ASTNode *node = ast_create_node(AST_TYPE);
            char full_name[512];
            if (strlen(decorators) > 0) {
                sprintf(full_name, "%s [*]%s", decorators, elem_type->data.type.name);
            } else {
                sprintf(full_name, "[*]%s", elem_type->data.type.name);
            }
            node->data.type.name = malloc(strlen(full_name) + 1);
            strcpy(node->data.type.name, full_name);
            node->data.type.type_params = NULL;
            node->data.type.type_param_count = 0;
            return node;
        }
        
        /* 检查是否是固定大小数组 [12]u16 或 [1024*512]u8 或动态数组 [Type] */
        Token peek = current_token(parser);
        
        /* 固定大小数组：[NUMBER]Type 或 [EXPR]Type (支持表达式如 1024*512) */
        if (peek.type == TK_INT) {
            /* 可能是一个数字，或者是表达式的开始 */
            char *array_size_expr = parse_array_size_expression(parser);
            
            if (!match(parser, TK_RBRACKET)) {
                error(parser, "Expected ']' after array size");
            }
            
            /* 现在解析 Type */
            if (!check(parser, TK_IDENT)) {
                error(parser, "Expected type name after array size");
                ASTNode *node = ast_create_node(AST_TYPE);
                node->data.type.name = "unknown";
                node->data.type.type_params = NULL;
                node->data.type.type_param_count = 0;
                return node;
            }
            
            Token type_name = current_token(parser);
            advance(parser);
            
            /* 返回固定大小数组类型（支持表达式） */
            ASTNode *node = ast_create_node(AST_TYPE);
            size_t total_len = strlen(array_size_expr) + strlen(type_name.lexeme) + 5;
            char *array_type = malloc(total_len);
            sprintf(array_type, "[%s]%s", array_size_expr, type_name.lexeme);
            node->data.type.name = array_type;
            node->data.type.type_params = NULL;
            node->data.type.type_param_count = 0;
            free(array_size_expr);
            return node;
        }
        
        /* 尝试解析为 Rust 风格数组 [Type;Size] 或其他格式 */
        ASTNode *key_type = parse_type_no_paren(parser);
        
        /* 检查是否是 Rust 风格数组类型 [Type; Size] */
        if (check(parser, TK_SEMICOLON)) {
            advance(parser);  // consume ';'
            
            /* 收集大小表达式直到 ] */
            char size_expr[256] = {0};
            size_t expr_len = 0;
            
            while (current_token(parser).type != TK_RBRACKET && 
                   current_token(parser).type != TK_EOF &&
                   expr_len < sizeof(size_expr) - 1) {
                Token tok = current_token(parser);
                
                if (tok.type == TK_INT || tok.type == TK_IDENT) {
                    int written = snprintf(size_expr + expr_len, sizeof(size_expr) - expr_len, "%s", tok.lexeme);
                    if (written > 0) expr_len += written;
                    advance(parser);
                } else if (tok.type == TK_STAR || tok.type == TK_PLUS || tok.type == TK_MINUS || 
                           tok.type == TK_SLASH || tok.type == TK_PERCENT) {
                    int written = snprintf(size_expr + expr_len, sizeof(size_expr) - expr_len, "%s", tok.lexeme);
                    if (written > 0) expr_len += written;
                    advance(parser);
                } else if (tok.type == TK_LPAREN) {
                    size_expr[expr_len++] = '(';
                    advance(parser);
                } else if (tok.type == TK_RPAREN) {
                    size_expr[expr_len++] = ')';
                    advance(parser);
                } else {
                    break;
                }
            }
            
            if (!match(parser, TK_RBRACKET)) {
                error(parser, "Expected ']' after Rust-style array size");
            }
            
            /* 返回 Rust 风格数组类型 [Type;Size] */
            ASTNode *node = ast_create_node(AST_TYPE);
            node->data.type.name = malloc(strlen(key_type->data.type.name) + expr_len + 3);
            sprintf(node->data.type.name, "[%s;%s]", key_type->data.type.name, size_expr);
            node->data.type.type_params = NULL;
            node->data.type.type_param_count = 0;
            return node;
        }
        
        /* 检查是否是字典类型 [KeyType: ValueType] */
        if (match(parser, TK_COLON)) {
            ASTNode *value_type = parse_type_no_paren(parser);
            
            if (!match(parser, TK_RBRACKET)) {
                error(parser, "Expected ']' after dictionary type");
            }
            
            /* 将字典类型表示为 [KeyType: ValueType] */
            char *dict_type = malloc(strlen(key_type->data.type.name) + strlen(value_type->data.type.name) + 5);
            sprintf(dict_type, "[%s:%s]", key_type->data.type.name, value_type->data.type.name);
            
            ASTNode *node = ast_create_node(AST_TYPE);
            node->data.type.name = dict_type;
            node->data.type.type_params = NULL;
            node->data.type.type_param_count = 0;
            
            return node;
        }
        
        /* 数组类型 [Type] */
        if (!match(parser, TK_RBRACKET)) {
            error(parser, "Expected ']' after array type");
        }
        
        /* 将数组类型表示为 [TypeName] */
        char *array_type = malloc(strlen(key_type->data.type.name) + 3);
        sprintf(array_type, "[%s]", key_type->data.type.name);
        
        ASTNode *node = ast_create_node(AST_TYPE);
        node->data.type.name = array_type;
        node->data.type.type_params = NULL;
        node->data.type.type_param_count = 0;
        
        return node;
    }
    
    /* 处理 ptr<T> 泛型指针类型 */
    if (check(parser, TK_PTR)) {
        advance(parser);  // consume 'ptr'
        
        if (!match(parser, TK_LT)) {
            error(parser, "Expected '<' after ptr");
            ASTNode *node = ast_create_node(AST_TYPE);
            node->data.type.name = "ptr<unknown>";
            node->data.type.type_params = NULL;
            node->data.type.type_param_count = 0;
            return node;
        }
        
        /* 使用特殊的泛型参数解析函数，避免 > 被误识别 */
        ASTNode *elem_type = parse_type_in_generic(parser);
        
        if (!match_closing_angle_bracket(parser)) {
            error(parser, "Expected '>' after ptr type parameter");
        }
        
        ASTNode *node = ast_create_node(AST_TYPE);
        node->data.type.name = malloc(strlen(elem_type->data.type.name) + 8);
        sprintf(node->data.type.name, "ptr<%s>", elem_type->data.type.name);
        node->data.type.type_params = NULL;
        node->data.type.type_param_count = 0;
        
        return node;
    }
    
    /* 处理 view<T> 零拷贝视图类型 */
    if (check(parser, TK_VIEW)) {
        advance(parser);  // consume 'view'
        
        if (!match(parser, TK_LT)) {
            error(parser, "Expected '<' after view");
            ASTNode *node = ast_create_node(AST_TYPE);
            node->data.type.name = "view<unknown>";
            node->data.type.type_params = NULL;
            node->data.type.type_param_count = 0;
            return node;
        }
        
        /* 使用特殊的泛型参数解析函数 */
        ASTNode *elem_type = parse_type_in_generic(parser);
        
        if (!match_closing_angle_bracket(parser)) {
            error(parser, "Expected '>' after view type parameter");
        }
        
        ASTNode *node = ast_create_node(AST_TYPE);
        node->data.type.name = malloc(strlen(elem_type->data.type.name) + 8);
        sprintf(node->data.type.name, "view<%s>", elem_type->data.type.name);
        node->data.type.type_params = NULL;
        node->data.type.type_param_count = 0;
        
        return node;
    }
    
    /* 处理物理寄存器类型：reg<"rax", UInt64> */
    if (check(parser, TK_REG)) {
        advance(parser);  // consume 'reg'
        
        if (!match(parser, TK_LT)) {
            error(parser, "Expected '<' after reg");
            ASTNode *node = ast_create_node(AST_TYPE);
            node->data.type.name = "reg<unknown>";
            node->data.type.type_params = NULL;
            node->data.type.type_param_count = 0;
            return node;
        }
        
        /* 解析reg名称（字符串）和类型 */
        char reg_name[256] = "";
        
        /* 期望第一个参数是寄存器名称（作为字符串） */
        if (check(parser, TK_STRING)) {
            Token str_tok = current_token(parser);
            advance(parser);
            
            /* 从字符串中提取寄存器名称（仅在确有引号时去掉） */
            int len = strlen(str_tok.lexeme);
            if (len >= 2 && str_tok.lexeme[0] == '"' && str_tok.lexeme[len - 1] == '"') {
                strncpy(reg_name, str_tok.lexeme + 1, len - 2);
                reg_name[len - 2] = '\0';
            } else if (len > 0) {
                strncpy(reg_name, str_tok.lexeme, sizeof(reg_name) - 1);
                reg_name[sizeof(reg_name) - 1] = '\0';
            } else {
                strcpy(reg_name, "unknown");
            }
        } else if (check(parser, TK_IDENT)) {
            Token id_tok = current_token(parser);
            advance(parser);
            strcpy(reg_name, id_tok.lexeme);
        } else {
            error(parser, "Expected register name as string or identifier");
            strcpy(reg_name, "unknown");
        }
        
        if (!match(parser, TK_COMMA)) {
            error(parser, "Expected ',' after register name");
        }
        
        /* 解析类型参数 */
        ASTNode *reg_type = parse_type_in_generic(parser);
        
        /* 处理 > 或 >> 的闭合 */
        if (!match_closing_angle_bracket(parser)) {
            error(parser, "Expected '>' after reg type parameter");
        }
        
        ASTNode *node = ast_create_node(AST_TYPE);
        char full_type[512];
        sprintf(full_type, "reg<%s,%s>", reg_name, reg_type->data.type.name);
        node->data.type.name = malloc(strlen(full_type) + 1);
        strcpy(node->data.type.name, full_type);
        node->data.type.type_params = NULL;
        node->data.type.type_param_count = 0;
        
        return node;
    }
    
    /* 原初层：处理 PhysAddr 强类型地址 */
    if (check(parser, TK_PHYSADDR)) {
        advance(parser);  // consume 'PhysAddr'
        
        ASTNode *node = ast_create_node(AST_TYPE);
        node->data.type.name = malloc(10);
        strcpy(node->data.type.name, "PhysAddr");
        node->data.type.type_params = NULL;
        node->data.type.type_param_count = 0;
        
        return node;
    }
    
    /* 原初层：处理 VirtAddr 强类型地址 */
    if (check(parser, TK_VIRTADDR)) {
        advance(parser);  // consume 'VirtAddr'
        
        ASTNode *node = ast_create_node(AST_TYPE);
        node->data.type.name = malloc(10);
        strcpy(node->data.type.name, "VirtAddr");
        node->data.type.type_params = NULL;
        node->data.type.type_param_count = 0;
        
        return node;
    }
    
    /* 处理指针类型 *Type */
    if (check(parser, TK_STAR)) {
        advance(parser);  // consume '*'
        
        if (!check(parser, TK_IDENT) && !check(parser, TK_LBRACKET) && !check(parser, TK_STAR)) {
            error(parser, "Expected type name after '*'");
            ASTNode *node = ast_create_node(AST_TYPE);
            node->data.type.name = "*unknown";
            node->data.type.type_params = NULL;
            node->data.type.type_param_count = 0;
            return node;
        }
        
        ASTNode *base_type = parse_type_no_paren(parser);
        
        ASTNode *node = ast_create_node(AST_TYPE);
        /* 添加 * 前缀到类型名 */
        char *ptr_type = malloc(strlen(base_type->data.type.name) + 2);
        sprintf(ptr_type, "*%s", base_type->data.type.name);
        node->data.type.name = ptr_type;
        node->data.type.type_params = NULL;
        node->data.type.type_param_count = 0;
        
        return node;
    }
    
    if (!check(parser, TK_IDENT)) {
        error(parser, "Expected type name");
        ASTNode *node = ast_create_node(AST_TYPE);
        node->data.type.name = "unknown";
        node->data.type.type_params = NULL;
        node->data.type.type_param_count = 0;
        return node;
    }
    
    Token name = current_token(parser);
    advance(parser);
    
    ASTNode *node = ast_create_node(AST_TYPE);
    
    /* 构造类型名称，添加decorators前缀 */
    char full_name[512];
    if (strlen(decorators) > 0) {
        sprintf(full_name, "%s %s", decorators, name.lexeme);
    } else {
        strcpy(full_name, name.lexeme);
    }
    
    node->data.type.name = malloc(strlen(full_name) + 1);
    strcpy(node->data.type.name, full_name);
    node->data.type.type_params = NULL;
    node->data.type.type_param_count = 0;
    
    /* 处理泛型类型参数 Type<T, U> */
    if (check(parser, TK_LT) && strcmp(name.lexeme, "ptr") != 0 && strcmp(name.lexeme, "view") != 0) {
        if (match(parser, TK_LT)) {
            /* 泛型类型参数解析 - 使用专门的泛型参数解析函数 */
            ASTNode **type_params = (ASTNode**)safe_malloc(sizeof(ASTNode*) * 32);
            int param_count = 0;
            
            /* 处理 > 或 >> 的闭合 - RSHIFT表示两个连续的 > */
            while (!check(parser, TK_GT) && !check(parser, TK_RSHIFT) && param_count < 32) {
                /* 使用泛型参数解析函数，避免 > 被误认 */
                ASTNode *param_type = parse_type_in_generic(parser);
                if (param_type) {
                    type_params[param_count++] = param_type;
                }
                
                /* 检查是否还有更多参数 */
                if (!match(parser, TK_COMMA)) {
                    break;
                }
            }
            
            /* 处理 > 或 >> 的闭合 */
            if (!match_closing_angle_bracket(parser)) {
                error(parser, "Expected '>' after type parameters");
            }
            
            node->data.type.type_params = type_params;
            node->data.type.type_param_count = param_count;
            
            /* 重新构建完整的类型名字以包含泛型参数和decorators */
            char full_type_name[1024];
            if (strlen(decorators) > 0) {
                sprintf(full_type_name, "%s %s<", decorators, name.lexeme);
            } else {
                sprintf(full_type_name, "%s<", name.lexeme);
            }
            
            for (int i = 0; i < param_count; i++) {
                if (i > 0) strcat(full_type_name, ",");
                strcat(full_type_name, type_params[i]->data.type.name);
            }
            
            strcat(full_type_name, ">");
            free(node->data.type.name);
            node->data.type.name = malloc(strlen(full_type_name) + 1);
            strcpy(node->data.type.name, full_type_name);
        }
    }
    
    // 检查是否是 optional 类型 (Type?)
    if (match(parser, TK_QUESTION)) {
        char *optional_type = malloc(strlen(node->data.type.name) + 2);
        sprintf(optional_type, "%s?", node->data.type.name);
        free(node->data.type.name);
        node->data.type.name = optional_type;
    }
    
    return node;
}

static ASTNode* parse_type_in_generic(Parser *parser) {
    /* 处理硬件层装饰器（如@volatile）在类型前面 */
    char decorators[256] = "";
    while (check(parser, TK_AT)) {
        advance(parser);  // 消耗 '@'
        
        if (check(parser, TK_IDENT)) {
            Token decorator = current_token(parser);
            advance(parser);
            
            if (strlen(decorators) > 0) {
                strcat(decorators, " ");
            }
            strcat(decorators, "@");
            strcat(decorators, decorator.lexeme);
        } else {
            error(parser, "Expected decorator name after '@'");
        }
    }
    
    /* 处理 ptr<T> - 递归时停止条件是看到 > */
    if (check(parser, TK_PTR)) {
        advance(parser);  // consume 'ptr'
        
        if (!match(parser, TK_LT)) {
            error(parser, "Expected '<' after ptr");
            ASTNode *node = ast_create_node(AST_TYPE);
            node->data.type.name = "ptr<unknown>";
            node->data.type.type_params = NULL;
            node->data.type.type_param_count = 0;
            return node;
        }
        
        /* 递归解析泛型参数 */
        ASTNode *elem_type = parse_type_in_generic(parser);
        
        /* 处理 > 或 >> 的闭合
         * RSHIFT (>>) 在嵌套泛型中表示两个 >，我们只虚拟地处理第一个
         */
        if (!match_closing_angle_bracket(parser)) {
            error(parser, "Expected '>' after ptr type parameter");
        }
        
        ASTNode *node = ast_create_node(AST_TYPE);
        char full_name[512];
        if (strlen(decorators) > 0) {
            sprintf(full_name, "%s ptr<%s>", decorators, elem_type->data.type.name);
        } else {
            sprintf(full_name, "ptr<%s>", elem_type->data.type.name);
        }
        node->data.type.name = malloc(strlen(full_name) + 1);
        strcpy(node->data.type.name, full_name);
        node->data.type.type_params = NULL;
        node->data.type.type_param_count = 0;
        return node;
    }
    
    /* 处理 view<T> */
    if (check(parser, TK_VIEW)) {
        advance(parser);  // consume 'view'
        
        if (!match(parser, TK_LT)) {
            error(parser, "Expected '<' after view");
            ASTNode *node = ast_create_node(AST_TYPE);
            node->data.type.name = "view<unknown>";
            node->data.type.type_params = NULL;
            node->data.type.type_param_count = 0;
            return node;
        }
        
        ASTNode *elem_type = parse_type_in_generic(parser);
        
        /* 处理 > 或 >> 的闭合
         * RSHIFT (>>) 在嵌套泛型中表示两个 >，我们只虚拟地处理第一个
         */
        if (!match_closing_angle_bracket(parser)) {
            error(parser, "Expected '>' after view type parameter");
        }
        
        ASTNode *node = ast_create_node(AST_TYPE);
        char full_name[512];
        if (strlen(decorators) > 0) {
            sprintf(full_name, "%s view<%s>", decorators, elem_type->data.type.name);
        } else {
            sprintf(full_name, "view<%s>", elem_type->data.type.name);
        }
        node->data.type.name = malloc(strlen(full_name) + 1);
        strcpy(node->data.type.name, full_name);
        node->data.type.type_params = NULL;
        node->data.type.type_param_count = 0;
        return node;
    }
    
    /* 处理物理寄存器类型：reg<"rax", UInt64> 或 reg<"ymm0", vector<UInt8, 32>> */
    if (check(parser, TK_REG)) {
        advance(parser);  // consume 'reg'
        
        if (!match(parser, TK_LT)) {
            error(parser, "Expected '<' after reg");
            ASTNode *node = ast_create_node(AST_TYPE);
            node->data.type.name = "reg<unknown>";
            node->data.type.type_params = NULL;
            node->data.type.type_param_count = 0;
            return node;
        }
        
        /* 激进方案：增加嵌套深度 */
        parser->generic_nesting_depth++;
        
        /* 解析reg名称（字符串）和类型 */
        char reg_name[256] = "";
        
        /* 期望第一个参数是寄存器名称（作为字符串） */
        if (check(parser, TK_STRING)) {
            Token str_tok = current_token(parser);
            advance(parser);
            
            /* 从字符串中提取寄存器名称（仅在确有引号时去掉） */
            int len = strlen(str_tok.lexeme);
            if (len >= 2 && str_tok.lexeme[0] == '"' && str_tok.lexeme[len - 1] == '"') {
                strncpy(reg_name, str_tok.lexeme + 1, len - 2);
                reg_name[len - 2] = '\0';
            } else if (len > 0) {
                strncpy(reg_name, str_tok.lexeme, sizeof(reg_name) - 1);
                reg_name[sizeof(reg_name) - 1] = '\0';
            } else {
                strcpy(reg_name, "unknown");
            }
        } else if (check(parser, TK_IDENT)) {
            Token id_tok = current_token(parser);
            advance(parser);
            strcpy(reg_name, id_tok.lexeme);
        } else {
            error(parser, "Expected register name as string or identifier");
            strcpy(reg_name, "unknown");
        }
        
        if (!match(parser, TK_COMMA)) {
            error(parser, "Expected ',' after register name");
            parser->generic_nesting_depth--;
        }
        
        /* 递归解析泛型参数时处理嵌套的>符号 */
        ASTNode *elem_type = parse_type_in_generic(parser);
        
        /* 激进方案：处理虚拟GT token回放机制
         * 如果内层设置了has_virtual_gt_token，说明它消耗了RSHIFT
         * 并为我们保留了虚拟的>。我们直接向下传递或使用它。
         */
        if (!match_closing_angle_bracket(parser)) {
            error(parser, "Expected '>' after reg type parameter");
        }
        
        parser->generic_nesting_depth--;
        
        ASTNode *node = ast_create_node(AST_TYPE);
        char full_type[512];
        if (strlen(decorators) > 0) {
            sprintf(full_type, "%s reg<%s,%s>", decorators, reg_name, elem_type->data.type.name);
        } else {
            sprintf(full_type, "reg<%s,%s>", reg_name, elem_type->data.type.name);
        }
        node->data.type.name = malloc(strlen(full_type) + 1);
        strcpy(node->data.type.name, full_type);
        node->data.type.type_params = NULL;
        node->data.type.type_param_count = 0;
        
        return node;
    }
    
    /* 处理指针类型 *Type */
    if (check(parser, TK_STAR)) {
        advance(parser);  // consume '*'
        
        ASTNode *base_type = parse_type_in_generic(parser);
        
        ASTNode *node = ast_create_node(AST_TYPE);
        char full_name[512];
        if (strlen(decorators) > 0) {
            sprintf(full_name, "%s *%s", decorators, base_type->data.type.name);
        } else {
            sprintf(full_name, "*%s", base_type->data.type.name);
        }
        node->data.type.name = malloc(strlen(full_name) + 1);
        strcpy(node->data.type.name, full_name);
        node->data.type.type_params = NULL;
        node->data.type.type_param_count = 0;
        return node;
    }
    
    /* 处理数组类型 [Type] */
    if (check(parser, TK_LBRACKET)) {
        advance(parser);  // consume '['
        
        ASTNode *elem_type = parse_type_in_generic(parser);
        
        if (!match(parser, TK_RBRACKET)) {
            error(parser, "Expected ']' after array type");
        }
        
        ASTNode *node = ast_create_node(AST_TYPE);
        char full_name[512];
        if (strlen(decorators) > 0) {
            sprintf(full_name, "%s [%s]", decorators, elem_type->data.type.name);
        } else {
            sprintf(full_name, "[%s]", elem_type->data.type.name);
        }
        node->data.type.name = malloc(strlen(full_name) + 1);
        strcpy(node->data.type.name, full_name);
        node->data.type.type_params = NULL;
        node->data.type.type_param_count = 0;
        return node;
    }
    
    /* 处理硬件层特有的字符串字面量（如reg<"rax", UInt64>）*/
    if (check(parser, TK_STRING)) {
        Token str_token = current_token(parser);
        advance(parser);
        
        ASTNode *node = ast_create_node(AST_TYPE);
        /* 保留原始字符串字面量（包含引号）*/
        char full_name[512];
        if (strlen(decorators) > 0) {
            sprintf(full_name, "%s %s", decorators, str_token.lexeme);
        } else {
            strcpy(full_name, str_token.lexeme);
        }
        node->data.type.name = malloc(strlen(full_name) + 1);
        strcpy(node->data.type.name, full_name);
        node->data.type.type_params = NULL;
        node->data.type.type_param_count = 0;
        return node;
    }
    
    /* 处理简单类型标识符、字符串字面量或数字字面量 */
    if (!check(parser, TK_IDENT) && !check(parser, TK_INT)) {
        error(parser, "Expected type name, integer, or string literal in generic");
        ASTNode *node = ast_create_node(AST_TYPE);
        node->data.type.name = "unknown";
        node->data.type.type_params = NULL;
        node->data.type.type_param_count = 0;
        return node;
    }
    
    /* 首先检查是否是整数字面量（用于向量大小如vector<UInt8, 32>）*/
    if (check(parser, TK_INT)) {
        Token num_token = current_token(parser);
        advance(parser);
        
        ASTNode *node = ast_create_node(AST_TYPE);
        char full_name[512];
        if (strlen(decorators) > 0) {
            sprintf(full_name, "%s %s", decorators, num_token.lexeme);
        } else {
            strcpy(full_name, num_token.lexeme);
        }
        node->data.type.name = malloc(strlen(full_name) + 1);
        strcpy(node->data.type.name, full_name);
        node->data.type.type_params = NULL;
        node->data.type.type_param_count = 0;
        return node;
    }
    
    Token name = current_token(parser);
    advance(parser);
    
    ASTNode *node = ast_create_node(AST_TYPE);
    char full_name[512];
    if (strlen(decorators) > 0) {
        sprintf(full_name, "%s %s", decorators, name.lexeme);
    } else {
        strcpy(full_name, name.lexeme);
    }
    node->data.type.name = malloc(strlen(full_name) + 1);
    strcpy(node->data.type.name, full_name);
    node->data.type.type_params = NULL;
    node->data.type.type_param_count = 0;
    
    /* 处理泛型类型参数 Type<T, U> (嵌套在泛型参数中) */
    if (check(parser, TK_LT) && strcmp(name.lexeme, "ptr") != 0 && strcmp(name.lexeme, "view") != 0) {
        if (match(parser, TK_LT)) {
            /* 激进方案：增加嵌套深度 */
            parser->generic_nesting_depth++;
            
            /* 泛型类型参数解析 - 支持多参数 */
            ASTNode **type_params = (ASTNode**)safe_malloc(sizeof(ASTNode*) * 32);
            int param_count = 0;
            
            /* 处理 > 或 >> 的闭合 - RSHIFT表示两个连续的 > */
            while (!check(parser, TK_GT) && !check(parser, TK_RSHIFT) && param_count < 32) {
                /* 使用泛型参数解析函数，避免 > 和 , 被误认 */
                ASTNode *param_type = parse_type_in_generic(parser);
                if (param_type) {
                    type_params[param_count++] = param_type;
                }
                
                /* 检查是否还有更多参数 */
                if (!match(parser, TK_COMMA)) {
                    break;
                }
            }
            
            /* 激进方案：明确处理RSHIFT的消耗
             * 关键洞察：当我们在vector<UInt8, 32>递归时：
             * 1. parse_type_in_generic(vector) 调用
             * 2. 进入match(TK_LT) 块，增加depth
             * 3. 解析UInt8: parse_type_in_generic(UInt8) 返回，没有RSHIFT
             * 4. match(,)
             * 5. 解析32: parse_type_in_generic(32) 返回，看到RSHIFT
             * 6. 此时，RSHIFT的第一个>应该关闭vector，我们需要消耗它
             */
            if (!match_closing_angle_bracket(parser)) {
                error(parser, "Expected '>' after type parameters in generic context");
            }
            
            parser->generic_nesting_depth--;
            
            node->data.type.type_params = type_params;
            node->data.type.type_param_count = param_count;
            
            /* 重新构建类型名字以包含泛型参数 */
            char full_type_name[1024];
            strcpy(full_type_name, name.lexeme);
            strcat(full_type_name, "<");
            
            for (int i = 0; i < param_count; i++) {
                if (i > 0) strcat(full_type_name, ",");
                strcat(full_type_name, type_params[i]->data.type.name);
            }
            
            strcat(full_type_name, ">");
            free(node->data.type.name);
            node->data.type.name = malloc(strlen(full_type_name) + 1);
            strcpy(node->data.type.name, full_type_name);
        }
    }
    
    /* 检查是否是 optional 类型 (Type?) */
    if (match(parser, TK_QUESTION)) {
        char *optional_type = malloc(strlen(node->data.type.name) + 2);
        sprintf(optional_type, "%s?", node->data.type.name);
        free(node->data.type.name);
        node->data.type.name = optional_type;
    }
    
    return node;
}

/* 修改 parse_block 函数以支持动态扩容，移除 100 条语句的限制 */
static ASTNode* parse_block(Parser *parser) {
    if (!match(parser, TK_LBRACE)) {
        error(parser, "Expected '{'");
        return NULL;
    }
    
    ASTNode *block = ast_create_node(AST_BLOCK);
    int capacity = 64;
    block->data.block.statements = malloc(sizeof(ASTNode*) * capacity);
    block->data.block.stmt_count = 0;
    
    int block_loop_count = 0;
    int max_block_iterations = 100000;
    
    while (!check(parser, TK_RBRACE) && !check(parser, TK_EOF) && block_loop_count++ < max_block_iterations) {
        while (match(parser, TK_NEWLINE));
        
        if (check(parser, TK_RBRACE) || check(parser, TK_EOF)) break;
        
        // 记录解析前的位置
        int start_pos = parser->pos;
        
        ASTNode *stmt = parse_statement(parser);
        
        if (stmt) {
            if (block->data.block.stmt_count >= capacity) {
                capacity *= 2;
                block->data.block.statements = realloc(block->data.block.statements, sizeof(ASTNode*) * capacity);
            }
            block->data.block.statements[block->data.block.stmt_count++] = stmt;
            // 在块内语句后跳过换行和分号，但小心对待 else
            while (check(parser, TK_NEWLINE) || check(parser, TK_SEMICOLON)) {
                advance(parser);
            }
        } else {
            // 语句解析失败
            // 检查是否是块结束标记（} 或 EOF）
            // 如果是，则不advance，让外层处理；如果不是，则跳过token以防止死循环
            if (parser->pos == start_pos) {
                // 如果解析器位置没有移动，说明卡在某个 Token 上了
                Token t = current_token(parser);
                
                // 不要跳过块结束符号，让后面的逻辑处理
                if (t.type != TK_RBRACE && t.type != TK_EOF) {
                    if (!parser->panic_mode) {
                        fprintf(stderr,
                                "Parse Error: Unexpected token '%s' (type %d) at line %d. Skipping to avoid hang.\n",
                                t.lexeme, t.type, t.line);
                    }
                    
                    // 强制跳过当前 Token，防止死循环（但不跳过块结束）
                    advance(parser);
                } else {
                    // 块结束或EOF，需要退出循环
                    break;
                }
            }
        }
    }
    
    if (block_loop_count >= max_block_iterations) {
        Token t = current_token(parser);
        fprintf(stderr, "FATAL: Infinite loop detected in parse_block at line %d\n", t.line);
        error(parser, "Compiler error: Infinite loop in block parsing");
    }
    
    if (!match(parser, TK_RBRACE)) {
        error(parser, "Expected '}'");
    }
    
    return block;
}

static ASTNode* parse_statement(Parser *parser) {
    // Variable Declaration
    if (check(parser, TK_VAR) || check(parser, TK_LET)) {
        int is_let_var = check(parser, TK_LET);
        (void)is_let_var;  // 声明类型信息已在条件中获得
        advance(parser);
        
        /* 检查是否是元组解构 (a, b, c) 或简单变量 */
        if (match(parser, TK_LPAREN)) {
            /* 元组解构模式：let (a, b) = expr */
            char **names = (char**)safe_malloc(sizeof(char*) * 10);
            int name_count = 0;
            
            while (!check(parser, TK_RPAREN) && name_count < 10) {
                if (check(parser, TK_IDENT)) {
                    Token name_tok = current_token(parser);
                    names[name_count++] = safe_strdup(name_tok.lexeme);
                    advance(parser);
                } else {
                    error(parser, "Expected identifier in tuple pattern");
                    break;
                }
                
                if (!match(parser, TK_COMMA)) break;
            }
            
            if (!match(parser, TK_RPAREN)) {
                error(parser, "Expected ')' after tuple pattern");
            }
            
            /* 创建一个元组模式节点 */
            ASTNode *tuple_pattern = ast_create_node(AST_TUPLE_PATTERN);
            tuple_pattern->data.tuple_pattern.names = names;
            tuple_pattern->data.tuple_pattern.element_count = name_count;
            
            ASTNode *var = ast_create_node(AST_VAR_DECL);
            /* 将元组模式存储在 name 字段中（作为特殊标记） */
            var->data.var_decl.name = (char *)parser_intern_string(parser, "__tuple_pattern__");
            var->data.var_decl.type = NULL;
            var->data.var_decl.init = NULL;
            
            /* 跳过类型注解（如果有） */
            if (match(parser, TK_COLON)) {
                parse_type(parser);
            }
            
            /* 期望赋值 */
            if (match(parser, TK_ASSIGN)) {
                var->data.var_decl.init = parse_expression(parser);
            }
            
            if (check(parser, TK_SEMICOLON)) advance(parser);
            return var;
        }
        
        /* 简单变量声明 */
        if (!check(parser, TK_IDENT) && !check(parser, TK_PTR) && !check(parser, TK_VIEW) && !check(parser, TK_METAL) && !check(parser, TK_ASM) && !check(parser, TK_BYTES)) {
            error(parser, "Expected variable name");
            return NULL;
        }
        
        Token name = current_token(parser);
        advance(parser);
        
        ASTNode *var = ast_create_node(AST_VAR_DECL);
        var->data.var_decl.name = (char *)parser_intern_string(parser, name.lexeme);
        var->data.var_decl.type = NULL;
        var->data.var_decl.init = NULL;
        
        if (match(parser, TK_COLON)) {
            var->data.var_decl.type = parse_type(parser);
        }
        
        if (match(parser, TK_ASSIGN)) {
            var->data.var_decl.init = parse_expression(parser);
        }
        
        if (check(parser, TK_SEMICOLON)) advance(parser);
        return var;
    }
    
    /* 修复：For 循环支持 AE 风格 (for i in 0..10) */
    if (match(parser, TK_FOR)) {
        ASTNode *node = ast_create_node(AST_FOR_STMT);
        
        // 尝试 AE 风格
        if (check(parser, TK_IDENT)) {
            Token var_name = current_token(parser);
            advance(parser);
            
            if (match(parser, TK_IN)) {
                node->data.for_stmt.variable = safe_strdup(var_name.lexeme);
                node->data.for_stmt.iterable = parse_expression(parser);
                // 循环体可能是块或语句
                if (check(parser, TK_LBRACE)) {
                    node->data.for_stmt.body = parse_block(parser);
                } else {
                    node->data.for_stmt.body = parse_statement(parser);
                }
                return node;
            }
            // 如果不是 IN，报错
            error(parser, "Expected 'in' after for variable");
            return NULL;
        }
        
        // C 风格 (for (init; cond; inc))
        if (!match(parser, TK_LPAREN)) error(parser, "Expected '('");
        if (!check(parser, TK_SEMICOLON)) node->data.for_stmt.init = parse_expression(parser);
        if (!match(parser, TK_SEMICOLON)) error(parser, "Expected ';'");
        if (!check(parser, TK_SEMICOLON)) node->data.for_stmt.condition = parse_expression(parser);
        if (!match(parser, TK_SEMICOLON)) error(parser, "Expected ';'");
        if (!check(parser, TK_RPAREN)) node->data.for_stmt.increment = parse_expression(parser);
        if (!match(parser, TK_RPAREN)) error(parser, "Expected ')'");
        if (check(parser, TK_LBRACE)) {
            node->data.for_stmt.body = parse_block(parser);
        } else {
            node->data.for_stmt.body = parse_statement(parser);
        }
        return node;
    }

    /* Return 语句 */
    if (check(parser, TK_IDENT) && strcmp(current_token(parser).lexeme, "loop") == 0) {
        advance(parser);
        while (match(parser, TK_NEWLINE));

        ASTNode *node = ast_create_node(AST_WHILE_STMT);
        ASTNode *cond = ast_create_node(AST_LITERAL);
        cond->data.literal.is_float = 0;
        cond->data.literal.value.int_value = 1;
        node->data.while_stmt.condition = cond;

        if (check(parser, TK_LBRACE)) {
            node->data.while_stmt.body = parse_block(parser);
        } else {
            node->data.while_stmt.body = parse_statement(parser);
        }
        return node;
    }

    if (check(parser, TK_IDENT) && strcmp(current_token(parser).lexeme, "morph") == 0) {
        advance(parser);
        while (match(parser, TK_NEWLINE));

        ASTNode *node = ast_create_node(AST_HW_MORPH_BLOCK);
        node->data.hw_morph_block.statements = (ASTNode**)safe_malloc(sizeof(ASTNode*) * 128);
        node->data.hw_morph_block.stmt_count = 0;

        if (!match(parser, TK_LBRACE)) {
            error(parser, "Expected '{' after morph");
            return node;
        }

        while (!check(parser, TK_RBRACE) && !check(parser, TK_EOF)) {
            while (match(parser, TK_NEWLINE) || match(parser, TK_SEMICOLON));
            if (check(parser, TK_RBRACE)) break;
            ASTNode *stmt = parse_statement(parser);
            if (stmt && node->data.hw_morph_block.stmt_count < 128) {
                node->data.hw_morph_block.statements[node->data.hw_morph_block.stmt_count++] = stmt;
            } else if (!stmt && !check(parser, TK_RBRACE) && !check(parser, TK_EOF)) {
                advance(parser);
            }
        }

        if (!match(parser, TK_RBRACE)) {
            error(parser, "Expected '}' after morph block");
        }
        return node;
    }

    if (check(parser, TK_IDENT) && strcmp(current_token(parser).lexeme, "goto") == 0) {
        ASTNode *node = ast_create_node(AST_HW_ISA_CALL);
        advance(parser); /* consume goto */
        while (match(parser, TK_NEWLINE));
        node->data.hw_isa_call.isa_operation = safe_strdup("goto");
        node->data.hw_isa_call.operands = (ASTNode**)safe_malloc(sizeof(ASTNode*) * 1);
        node->data.hw_isa_call.operand_count = 0;
        node->data.hw_isa_call.opcode_length = 0;
        if (!check(parser, TK_SEMICOLON) && !check(parser, TK_NEWLINE) &&
            !check(parser, TK_RBRACE) && !check(parser, TK_EOF)) {
            ASTNode *target = parse_expression(parser);
            if (target) {
                node->data.hw_isa_call.operands[0] = target;
                node->data.hw_isa_call.operand_count = 1;
            }
        }
        if (check(parser, TK_SEMICOLON)) advance(parser);
        return node;
    }

    /* Guard 语句 */
    if (match(parser, TK_GUARD)) {
        ASTNode *node = ast_create_node(AST_GUARD_STMT);
        
        /* 检查是否是 guard case 或 guard let */
        if (match(parser, TK_CASE)) {
            /* guard case 模式匹配 - 仅支持反斜杠 */
            if (match(parser, TK_BACKSLASH)) {
                /* 枚举情况模式 \CaseName(let binding) */
                if (!check(parser, TK_IDENT)) {
                    error(parser, "Expected identifier after '\\' in guard case pattern");
                    return NULL;
                }
                (void)current_token(parser);  // 情况名称已由check验证存在
                advance(parser);
                
                /* 如果有关联值，解析 (let binding, ...) */
                if (match(parser, TK_LPAREN)) {
                    while (!check(parser, TK_RPAREN) && !check(parser, TK_EOF)) {
                        if (match(parser, TK_LET)) {
                            if (check(parser, TK_IDENT)) {
                                Token binding = current_token(parser);
                                node->data.guard_stmt.binding_name = safe_strdup(binding.lexeme);
                                advance(parser);
                            }
                        }
                        
                        if (!match(parser, TK_COMMA)) break;
                    }
                    
                    if (!match(parser, TK_RPAREN)) {
                        error(parser, "Expected ')' after guard case pattern");
                    }
                }
            } else {
                error(parser, "Expected '\\' after guard case (no dot access allowed)");
                return NULL;
            }
            
            /* 期望 = */
            if (!match(parser, TK_ASSIGN)) {
                error(parser, "Expected '=' after guard case pattern");
                return NULL;
            }
            
            node->data.guard_stmt.binding_expr = parse_expression(parser);
        } else if (match(parser, TK_LET)) {
            /* guard let pattern = expr else { ... } */
            if (check(parser, TK_IDENT)) {
                Token name_tok = current_token(parser);
                node->data.guard_stmt.binding_name = safe_strdup(name_tok.lexeme);
                advance(parser);
            } else {
                error(parser, "Expected identifier in guard let");
                return NULL;
            }
            
            if (!match(parser, TK_ASSIGN)) {
                error(parser, "Expected '=' in guard let");
                return NULL;
            }
            
            node->data.guard_stmt.binding_expr = parse_expression(parser);
        } else {
            /* guard condition else { ... } */
            node->data.guard_stmt.condition = parse_expression(parser);
        }
        
        /* 期望 else */
        while (match(parser, TK_NEWLINE));
        if (!match(parser, TK_ELSE)) {
            error(parser, "Expected 'else' after guard condition");
            return NULL;
        }
        
        /* else 分支（通常是 return、break、continue 等） */
        while (match(parser, TK_NEWLINE));
        if (check(parser, TK_LBRACE)) {
            node->data.guard_stmt.else_branch = parse_block(parser);
        } else {
            node->data.guard_stmt.else_branch = parse_statement(parser);
        }
        
        return node;
    }
    
    /* Return 语句 */
    if (match(parser, TK_RETURN)) {
        ASTNode *node = ast_create_node(AST_RETURN_STMT);
        if (!check(parser, TK_SEMICOLON) && !check(parser, TK_RBRACE) && !check(parser, TK_NEWLINE) && !check(parser, TK_EOF)) {
            node->data.return_stmt.value = parse_expression(parser);
        }
        while (match(parser, TK_NEWLINE));
        if (check(parser, TK_SEMICOLON)) advance(parser);
        return node;
    }
    
    /* If 语句 */
    if (match(parser, TK_IF)) {
        ASTNode *node = ast_create_node(AST_IF_STMT);
        
        // 检查是否是 if case 模式匹配
        if (check(parser, TK_CASE)) {
            advance(parser);  // 消耗 case
            
            /* 解析枚举模式（仅支持反斜杠） */
            if (match(parser, TK_BACKSLASH)) {
                /* 枚举情况模式 \CaseName */
                if (!check(parser, TK_IDENT)) {
                    error(parser, "Expected identifier after '\\' in if case pattern");
                    return NULL;
                }
                Token case_name = current_token(parser);
                advance(parser);
                
                /* 构造一个虚拟条件表达式 */
                ASTNode *pattern = ast_create_node(AST_IDENT);
                char buf[256];
                snprintf(buf, sizeof(buf), "\\%s", case_name.lexeme);
                pattern->data.ident.name = (char *)parser_intern_string(parser, buf);
                node->data.if_stmt.condition = pattern;
            } else {
                /* 其他模式或简单标识符 */
                node->data.if_stmt.condition = parse_expression(parser);
            }
            
            /* 期望 = */
            if (!match(parser, TK_ASSIGN)) {
                error(parser, "Expected '=' after pattern in if case");
            }
            
            /* 解析条件表达式 */
            parse_expression(parser);
            
            // then 分支
            while (match(parser, TK_NEWLINE));
            if (check(parser, TK_LBRACE)) {
                node->data.if_stmt.then_branch = parse_block(parser);
            } else {
                node->data.if_stmt.then_branch = parse_statement(parser);
            }
            
            // else 分支
            while (match(parser, TK_NEWLINE));
            if (match(parser, TK_ELSE)) {
                while (match(parser, TK_NEWLINE));
                if (check(parser, TK_LBRACE)) {
                    node->data.if_stmt.else_branch = parse_block(parser);
                } else {
                    node->data.if_stmt.else_branch = parse_statement(parser);
                }
            }
            
            return node;
        }
        
        // 检查是否是 if let
        if (check(parser, TK_LET)) {
            advance(parser);  // 消耗 let
            
            /* 解析模式 - 可以是简单标识符或 (a, b, c) 元组模式 */
            if (match(parser, TK_LPAREN)) {
                /* 元组模式 (a, b, c) */
                char **names = (char**)safe_malloc(sizeof(char*) * 10);
                int name_count = 0;
                
                while (!check(parser, TK_RPAREN) && name_count < 10) {
                    if (check(parser, TK_IDENT)) {
                        Token name_tok = current_token(parser);
                        names[name_count++] = safe_strdup(name_tok.lexeme);
                        advance(parser);
                    } else {
                        error(parser, "Expected identifier in tuple pattern");
                        break;
                    }
                    
                    if (!match(parser, TK_COMMA)) break;
                }
                
                if (!match(parser, TK_RPAREN)) {
                    error(parser, "Expected ')' after tuple pattern");
                }
                
                /* 期望 = */
                if (!match(parser, TK_ASSIGN)) {
                    error(parser, "Expected '=' after pattern in if let");
                    return NULL;
                }
                
                /* 解析条件表达式 */
                node->data.if_stmt.condition = parse_expression(parser);
            } else if (check(parser, TK_IDENT)) {
                /* 简单标识符模式 */
                (void)current_token(parser);  // 标识符已由check验证存在
                advance(parser);
                
                if (!match(parser, TK_ASSIGN)) {
                    error(parser, "Expected '=' after pattern in if let");
                    return NULL;
                }
                
                node->data.if_stmt.condition = parse_expression(parser);
            } else {
                error(parser, "Expected pattern in if let");
                return NULL;
            }
            
            // then 分支
            while (match(parser, TK_NEWLINE));
            if (check(parser, TK_LBRACE)) {
                node->data.if_stmt.then_branch = parse_block(parser);
            } else {
                node->data.if_stmt.then_branch = parse_statement(parser);
            }
            
            // else 分支
            while (match(parser, TK_NEWLINE));
            if (match(parser, TK_ELSE)) {
                while (match(parser, TK_NEWLINE));
                if (check(parser, TK_LBRACE)) {
                    node->data.if_stmt.else_branch = parse_block(parser);
                } else {
                    node->data.if_stmt.else_branch = parse_statement(parser);
                }
            }
            
            return node;
        }
        
        // 普通 if 语句
        // 允许省略括号，但不管有没有括号都解析完整的条件表达式
        // if 条件可以是：
        //   if (expr)
        //   if expr
        //   if (expr) op (expr)  <- 关键：支持比较和其他二元运算
        //   if let x = expr, condition <- 支持逗号分隔的多条件
        
        node->data.if_stmt.condition = parse_expression(parser);
        
        /* 支持逗号分隔的条件：if expr, condition { ... } */
        while (match(parser, TK_COMMA)) {
            while (match(parser, TK_NEWLINE));
            parse_expression(parser);  /* 解析但忽略后续条件 */
        }
        
        // then 分支应该是块或单个语句
        while (match(parser, TK_NEWLINE));  // 跳过换行
        if (check(parser, TK_LBRACE)) {
            node->data.if_stmt.then_branch = parse_block(parser);
        } else {
            node->data.if_stmt.then_branch = parse_statement(parser);
        }
        
        while (match(parser, TK_NEWLINE));  // 跳过换行
        if (match(parser, TK_ELSE)) {
            // else 分支也可以是块或语句
            while (match(parser, TK_NEWLINE));  // 跳过换行
            if (check(parser, TK_LBRACE)) {
                node->data.if_stmt.else_branch = parse_block(parser);
            } else {
                node->data.if_stmt.else_branch = parse_statement(parser);
            }
        }
        
        return node;
    }
    
    /* Break/Continue 语句 */
    if (match(parser, TK_SWITCH)) {
        ASTNode *node = ast_create_node(AST_SWITCH_STMT);
        
        /* 解析 switch 表达式 */
        if (check(parser, TK_LPAREN)) {
            advance(parser);
            node->data.switch_stmt.expr = parse_expression(parser);
            if (check(parser, TK_RPAREN)) advance(parser);
        } else {
            node->data.switch_stmt.expr = parse_expression(parser);
        }
        
        node->data.switch_stmt.cases = (ASTNode**)safe_malloc(sizeof(ASTNode*) * 64);
        node->data.switch_stmt.case_count = 0;
        node->data.switch_stmt.default_case = NULL;
        
        /* 期望 { */
        if (!match(parser, TK_LBRACE)) {
            error(parser, "Expected '{' after switch expression");
            return node;
        }
        
        /* 解析 case 和 default 子句 */
        while (!check(parser, TK_RBRACE) && !check(parser, TK_EOF)) {
            while (match(parser, TK_NEWLINE));  /* 跳过换行 */
            
            if (check(parser, TK_RBRACE)) break;
            
            if (check(parser, TK_CASE)) {
                advance(parser);
                
                /* 创建 case 节点 */
                ASTNode *case_node = ast_create_node(AST_SWITCH_CASE);
                case_node->data.switch_case.values = (ASTNode**)safe_malloc(sizeof(ASTNode*) * 16);
                case_node->data.switch_case.value_count = 0;
                case_node->data.switch_case.statements = (ASTNode**)safe_malloc(sizeof(ASTNode*) * 100);
                case_node->data.switch_case.stmt_count = 0;
                
                /* 解析 case 值（可能有多个，用逗号分隔） */
                do {
                    ASTNode *val = parse_expression(parser);
                    if (val && case_node->data.switch_case.value_count < 16) {
                        case_node->data.switch_case.values[case_node->data.switch_case.value_count++] = val;
                    }
                    while (match(parser, TK_NEWLINE));  /* 跳过换行 */
                } while (match(parser, TK_COMMA) && !check(parser, TK_COLON));
                
                /* 期望 : */
                if (!match(parser, TK_COLON)) {
                    error(parser, "Expected ':' after case value");
                    advance(parser);  /* 强制前进以避免无限循环 */
                }
                
                /* 跳过换行 */
                while (match(parser, TK_NEWLINE));
                
                /* 解析 case 中的语句，直到遇到下一个 case/default 或 } */
                int safety_counter = 0;
                while (!check(parser, TK_CASE) && !check(parser, TK_DEFAULT) && 
                       !check(parser, TK_RBRACE) && !check(parser, TK_EOF) && safety_counter < 1000) {
                    safety_counter++;
                    while (match(parser, TK_NEWLINE));
                    
                    if (check(parser, TK_CASE) || check(parser, TK_DEFAULT) || check(parser, TK_RBRACE)) {
                        break;
                    }
                    
                    int prev_pos = parser->pos;
                    ASTNode *stmt = parse_statement(parser);
                    if (stmt && case_node->data.switch_case.stmt_count < 100) {
                        case_node->data.switch_case.statements[case_node->data.switch_case.stmt_count++] = stmt;
                    }
                    
                    /* 如果解析器没有前进，强制前进以避免无限循环 */
                    if (parser->pos == prev_pos) {
                        advance(parser);
                    }
                    while (match(parser, TK_NEWLINE));
                }
                
                /* 添加 case 到 switch */
                if (node->data.switch_stmt.case_count < 64) {
                    node->data.switch_stmt.cases[node->data.switch_stmt.case_count++] = case_node;
                }
                
            } else if (check(parser, TK_DEFAULT)) {
                advance(parser);
                
                /* 创建 default 节点 */
                ASTNode *default_node = ast_create_node(AST_SWITCH_CASE);
                default_node->data.switch_case.values = NULL;
                default_node->data.switch_case.value_count = 0;
                default_node->data.switch_case.statements = (ASTNode**)safe_malloc(sizeof(ASTNode*) * 100);
                default_node->data.switch_case.stmt_count = 0;
                
                /* 期望 : */
                if (!match(parser, TK_COLON)) {
                    error(parser, "Expected ':' after default");
                    advance(parser);  /* 强制前进以避免无限循环 */
                }
                
                /* 跳过换行 */
                while (match(parser, TK_NEWLINE));
                
                /* 解析 default 中的语句 */
                int safety_counter = 0;
                while (!check(parser, TK_RBRACE) && !check(parser, TK_EOF) && safety_counter < 1000) {
                    safety_counter++;
                    while (match(parser, TK_NEWLINE));
                    
                    if (check(parser, TK_RBRACE)) {
                        break;
                    }
                    
                    int prev_pos = parser->pos;
                    ASTNode *stmt = parse_statement(parser);
                    if (stmt && default_node->data.switch_case.stmt_count < 100) {
                        default_node->data.switch_case.statements[default_node->data.switch_case.stmt_count++] = stmt;
                    }
                    
                    /* 如果解析器没有前进，强制前进以避免无限循环 */
                    if (parser->pos == prev_pos) {
                        advance(parser);
                    }
                    while (match(parser, TK_NEWLINE));
                }
                
                node->data.switch_stmt.default_case = default_node;
            } else {
                /* 跳过其他内容，避免无限循环 */
                advance(parser);
            }
        }
        
        /* 期望 } */
        if (!match(parser, TK_RBRACE)) {
            error(parser, "Expected '}' after switch cases");
        }
        
        return node;
    }
    
    /* Defer 语句 */
    if (match(parser, TK_DEFER)) {
        ASTNode *node = ast_create_node(AST_DEFER_STMT);
        
        /* defer 后面跟一个块或语句 */
        if (check(parser, TK_LBRACE)) {
            node->data.defer_stmt.body = parse_block(parser);
        } else {
            node->data.defer_stmt.body = parse_statement(parser);
        }
        
        return node;
    }
    
    /* While 循环 */
    if (match(parser, TK_WHILE)) {
        ASTNode *node = ast_create_node(AST_WHILE_STMT);
        if (check(parser, TK_LPAREN)) advance(parser);
        
        /* 支持逗号分隔的条件：while let x = expr, condition { ... } */
        node->data.while_stmt.condition = parse_expression(parser);
        
        /* 跳过其他逗号分隔的条件（暂时只解析第一个） */
        while (match(parser, TK_COMMA)) {
            while (match(parser, TK_NEWLINE));
            parse_expression(parser);  /* 解析但忽略后续条件 */
        }
        
        if (check(parser, TK_RPAREN)) advance(parser);
        
        node->data.while_stmt.body = parse_statement(parser);
        return node;
    }
    
    /* For 循环 */
    if (match(parser, TK_FOR)) {
        ASTNode *node = ast_create_node(AST_FOR_STMT);
        if (!match(parser, TK_LPAREN)) {
            error(parser, "Expected '('");
        }
        
        if (!check(parser, TK_SEMICOLON)) {
            node->data.for_stmt.init = parse_expression(parser);
        }
        if (!match(parser, TK_SEMICOLON)) {
            error(parser, "Expected ';'");
        }
        
        if (!check(parser, TK_SEMICOLON)) {
            node->data.for_stmt.condition = parse_expression(parser);
        }
        if (!match(parser, TK_SEMICOLON)) {
            error(parser, "Expected ';'");
        }
        
        if (!check(parser, TK_RPAREN)) {
            node->data.for_stmt.increment = parse_expression(parser);
        }
        if (!match(parser, TK_RPAREN)) {
            error(parser, "Expected ')'");
        }
        
        node->data.for_stmt.body = parse_statement(parser);
        return node;
    }
    
    /* Metal 块 - 低级系统编程 */
    if (match(parser, TK_METAL)) {
        ASTNode *node = ast_create_node(AST_METAL_BLOCK);
        
        if (!match(parser, TK_LBRACE)) {
            error(parser, "Expected '{' after 'metal'");
            return node;
        }
        
        node->data.metal_block.statements = (ASTNode**)safe_malloc(sizeof(ASTNode*) * 256);
        node->data.metal_block.stmt_count = 0;
        
        int metal_loop_count = 0;
        int metal_max_loop = 10000;
        
        while (!check(parser, TK_RBRACE) && !check(parser, TK_EOF) && metal_loop_count++ < metal_max_loop) {
            while (match(parser, TK_NEWLINE));
            if (check(parser, TK_RBRACE)) break;
            
            int pos_before = parser->pos;
            ASTNode *stmt = parse_statement(parser);
            
            if (stmt && node->data.metal_block.stmt_count < 256) {
                node->data.metal_block.statements[node->data.metal_block.stmt_count++] = stmt;
            }
            
            /* 错误恢复：如果位置没有改变，强制前进 */
            if (parser->pos == pos_before) {
                Token t = current_token(parser);
                if (t.type != TK_RBRACE && t.type != TK_EOF) {
                    if (!parser->panic_mode) {
                        fprintf(stderr,
                                "Parse warning: Skipping unparseable statement in metal block at line %d (token '%s')\n",
                                t.line, t.lexeme);
                    }
                    advance(parser);
                }
            }
            
            while (match(parser, TK_NEWLINE) || match(parser, TK_SEMICOLON));
        }
        
        if (metal_loop_count >= metal_max_loop) {
            Token t = current_token(parser);
            fprintf(stderr, "FATAL: Infinite loop in metal block parsing at line %d\n", t.line);
            error(parser, "Compiler error: Infinite loop in metal block");
        }
        
        if (!match(parser, TK_RBRACE)) {
            error(parser, "Expected '}' after metal block");
        }
        
        return node;
    }
    
    /* 硬件层块 - hardware { ... } 硬件层代码 */
    if (check(parser, TK_IDENT) && strcmp(current_token(parser).lexeme, "hardware") == 0) {
        int hw_saved_pos = parser->pos;
        advance(parser);  // 消耗 hardware 
        
        /* 检查是否是块（后跟{）还是ISA调用（后跟\） */
        if (check(parser, TK_LBRACE)) {
            /* hardware { ... } 块 */
            ASTNode *node = ast_create_node(AST_HW_BLOCK);
            node->data.hw_block.gate_type = NULL;
            node->data.hw_block.statements = (ASTNode**)safe_malloc(sizeof(ASTNode*) * 64);
            node->data.hw_block.stmt_count = 0;
            
            if (!match(parser, TK_LBRACE)) {
                error(parser, "Expected '{' after 'hardware'");
                return node;
            }
            
            int hw_loop_count = 0;
            int hw_max_loop = 10000;
            
            while (!check(parser, TK_RBRACE) && !check(parser, TK_EOF) && hw_loop_count++ < hw_max_loop) {
                while (match(parser, TK_NEWLINE));
                if (check(parser, TK_RBRACE)) break;
                
                int pos_before = parser->pos;
                ASTNode *stmt = parse_statement(parser);
                
                if (stmt && node->data.hw_block.stmt_count < 64) {
                    node->data.hw_block.statements[node->data.hw_block.stmt_count++] = stmt;
                } else if (pos_before == parser->pos) {
                    /* 如果parse_statement没有推进位置，强制跳过当前token */
                    Token t = current_token(parser);
                    if (t.type != TK_RBRACE && t.type != TK_EOF) {
                        advance(parser);
                    }
                }
                
                while (match(parser, TK_NEWLINE) || match(parser, TK_SEMICOLON));
            }
            
            if (hw_loop_count >= hw_max_loop) {
                Token t = current_token(parser);
                fprintf(stderr, "FATAL: Infinite loop in hardware block parsing at line %d\n", t.line);
                error(parser, "Compiler error: Infinite loop in hardware block");
            }
            
            if (!match(parser, TK_RBRACE)) {
                error(parser, "Expected '}' after hardware block");
            }
            
            return node;
        } else {
            /* 不是块，恢复位置并作为表达式语句处理（hardware\isa\*() 等） */
            parser->pos = hw_saved_pos;
        }
    }
    
    /* 硅基语义块 - 微架构和低级硬件控制 */
    if (match(parser, TK_SILICON)) {
        return parse_silicon_block(parser);
    }
    
    /* Asm 块 - 内联汇编 */
    if (match(parser, TK_ASM)) {
        ASTNode *node = ast_create_node(AST_ASM_BLOCK);
        
        if (!match(parser, TK_LBRACE)) {
            error(parser, "Expected '{' after 'asm'");
            return node;
        }
        
        /* 收集asm块中的所有内容作为原始汇编代码 */
        int brace_depth = 1;
        (void)parser->pos;  // 起始位置用于错误恢复
        char asm_code[8192] = "";
        int asm_len = 0;
        
        while (brace_depth > 0 && !check(parser, TK_EOF)) {
            Token tok = current_token(parser);
            
            if (check(parser, TK_LBRACE)) {
                brace_depth++;
            } else if (check(parser, TK_RBRACE)) {
                brace_depth--;
                if (brace_depth == 0) {
                    advance(parser);
                    break;
                }
            }
            
            /* 将token内容添加到asm代码 */
            if ((int)asm_len < (int)(sizeof(asm_code) - 1)) {
                if (tok.type == TK_STRING) {
                    /* 字符串token - 去掉引号 */
                    int len = strlen(tok.lexeme);
                    if (len > 2) {
                        strncat(asm_code, tok.lexeme + 1, len - 2);
                        asm_len += len - 2;
                    }
                } else {
                    strncat(asm_code, tok.lexeme, sizeof(asm_code) - 1 - asm_len);
                    asm_len = strlen(asm_code);
                }
                
                /* 添加空格分隔 */
                if ((int)asm_len < (int)(sizeof(asm_code) - 1)) {
                    strcat(asm_code, " ");
                    asm_len++;
                }
            }
            
            advance(parser);
        }
        
        node->data.asm_block.code = safe_strdup(asm_code);
        return node;
    }
    
    /* Block 语句 */
    if (check(parser, TK_LBRACE)) {
        return parse_block(parser);
    }
    
    /* Break 和 Continue */
    if (match(parser, TK_BREAK)) {
        if (check(parser, TK_SEMICOLON)) advance(parser);
        return ast_create_node(AST_BREAK_STMT);
    }
    
    if (match(parser, TK_CONTINUE)) {
        if (check(parser, TK_SEMICOLON)) advance(parser);
        return ast_create_node(AST_CONTINUE_STMT);
    }
    
    /* 表达式语句 或 赋值语句
     * 关键修复点：首先检查当前token是否可能是expression的开始
     * 如果是block结束或其他语句关键字，直接返回NULL，不要尝试解析expression
     * 这防止了在空块中parse_expression被调用导致的错误
     */
    Token curr_tok = current_token(parser);
    
    // 如果当前token不太可能是expression的开始，直接返回NULL
    // 这包括：块结束、条件关键字、EOF等
    if (curr_tok.type == TK_RBRACE || curr_tok.type == TK_ELSE || 
        curr_tok.type == TK_EOF || 
        curr_tok.type == TK_SEMICOLON || curr_tok.type == TK_CASE ||
        curr_tok.type == TK_DEFAULT) {
        return NULL;
    }
    
    ASTNode *expr = parse_expression(parser);
    if (!expr) {
        return NULL; // 解析失败，返回 NULL 供上层处理
    }

    /* parse_expression 已经解析了赋值表达式，直接作为赋值语句返回 */
    if (expr->type == AST_ASSIGNMENT) {
        if (check(parser, TK_SEMICOLON)) advance(parser);
        return expr;
    }
    
    /* 检查是否是赋值（=, +=, -=, *=, /=, etc.) */
    if (check(parser, TK_ASSIGN) || check(parser, TK_PLUS_ASSIGN) || 
        check(parser, TK_MINUS_ASSIGN) || check(parser, TK_STAR_ASSIGN) ||
        check(parser, TK_SLASH_ASSIGN)) {
        
        TokenType op = current_token(parser).type;
        advance(parser);  // 消耗赋值运算符
        
        ASTNode *rhs = parse_expression(parser);
        if (!rhs) {
            error(parser, "Expected expression after assignment operator");
            return NULL;
        }
        
        ASTNode *stmt = ast_create_node(AST_ASSIGNMENT);
        stmt->data.assignment.left = expr;
        stmt->data.assignment.right = rhs;
        
        // 简单处理：暂时不保存op，假设都是 =
        (void)op;
        
        if (check(parser, TK_SEMICOLON)) advance(parser);
        return stmt;
    }
    
    // 不是赋值，只是表达式语句
    if (check(parser, TK_SEMICOLON)) advance(parser);
    
    ASTNode *stmt = ast_create_node(AST_EXPR_STMT);
    stmt->data.assignment.left = expr;
    return stmt;
}

ASTNode* parser_parse_program(Parser *parser) {
    ASTNode *program = ast_create_node(AST_PROGRAM);
    program->data.program.declarations = malloc(sizeof(ASTNode*) * 100);
    program->data.program.decl_count = 0;
    int loop_count = 0;
    int max_loop_iterations = 100000;
    
    /* 生产级位置追踪：记录最后的安全位置以检测无进展 */
    int last_safe_pos = -1;
    int stalled_count = 0;
    int max_stall_tolerance = 5;
    
    while (!check(parser, TK_EOF) && !parser->panic_mode) {
        /* 检测无限循环 */
        if (++loop_count > max_loop_iterations) {
            Token t = current_token(parser);
            fprintf(stderr, "FATAL: Infinite loop detected in parser_parse_program at line %d (pos %d, token '%s' type %d)\n",
                    t.line, parser->pos, t.lexeme, t.type);
            error(parser, "Compiler error: Infinite loop in program parsing. "
                          "This may indicate a parsing bug with the current token or syntax.");
            break;
        }
        
        /* 检测停滞（位置没有进展）- 生产级防守 */
        if (parser->pos == last_safe_pos) {
            stalled_count++;
            if (stalled_count > max_stall_tolerance) {
                /* 强制跳过当前token以打破停滞 */
                Token t = current_token(parser);
                fprintf(stderr, "WARNING: Parser stalled at line %d token '%s' (type %d), forcing skip to recover\n",
                        t.line, t.lexeme, t.type);
                advance(parser);
                stalled_count = 0;
            }
        } else {
            last_safe_pos = parser->pos;
            stalled_count = 0;
        }
        
        while (match(parser, TK_NEWLINE));
        if (check(parser, TK_EOF) || parser->panic_mode) break;
        
        /* 解析属性装饰器 (@packed, @aligned, @entry, etc.) */
        char **attributes = NULL;
        int attr_count = 0;
        parse_attributes(parser, &attributes, &attr_count);
        
        /* 处理 use 语句 */
        if (check(parser, TK_USE)) {
            advance(parser);
            /* use 可以是: use "file.ae" 或 use module_name */
            if (check(parser, TK_STRING)) {
                advance(parser);
            } else if (check(parser, TK_IDENT)) {
                advance(parser);
            }
            while (match(parser, TK_NEWLINE) || match(parser, TK_SEMICOLON));
            continue;
        }
        
        /* 处理 Rimport (系统级导入) */
        if (check(parser, TK_RIMPORT)) {
            advance(parser);
            char *module_spec = parse_import_module_spec(parser);
            if (!module_spec) {
                break;
            }

            ASTNode *import_node = ast_create_node(AST_IMPORT_STMT);
            import_node->data.import_stmt.module = module_spec;
            import_node->data.import_stmt.is_rimport = 1;
            
            while (match(parser, TK_NEWLINE) || match(parser, TK_SEMICOLON));
            program->data.program.declarations[program->data.program.decl_count++] = import_node;
            continue;
        }
        
        /* 处理 import (标准导入) */
        if (check(parser, TK_IMPORT)) {
            advance(parser);
            char *module_spec = parse_import_module_spec(parser);
            if (!module_spec) {
                break;
            }

            ASTNode *import_node = ast_create_node(AST_IMPORT_STMT);
            import_node->data.import_stmt.module = module_spec;
            import_node->data.import_stmt.is_rimport = 0;
            
            while (match(parser, TK_NEWLINE) || match(parser, TK_SEMICOLON));
            program->data.program.declarations[program->data.program.decl_count++] = import_node;
            continue;
        }
        
        /* 处理顶级 metal 块 (用于 bootloader 和内核底层) - 必须在 const 之前处理 */
        if (check(parser, TK_METAL)) {
            ASTNode *metal_node = parse_metal_block(parser);
            if (metal_node) {
                if (program->data.program.decl_count < 1000) {
                    program->data.program.declarations[program->data.program.decl_count++] = metal_node;
                }
            }
            while (match(parser, TK_NEWLINE) || match(parser, TK_SEMICOLON));
            continue;
        }
        
        /* 处理顶级 asm 块 */
        if (check(parser, TK_ASM)) {
            ASTNode *asm_node = parse_asm_block(parser);
            if (asm_node) {
                if (program->data.program.decl_count < 1000) {
                    program->data.program.declarations[program->data.program.decl_count++] = asm_node;
                }
            }
            while (match(parser, TK_NEWLINE) || match(parser, TK_SEMICOLON));
            continue;
        }
        
        /* 处理顶级 bytes 块 */
        if (check(parser, TK_BYTES)) {
            ASTNode *bytes_node = parse_bytes_block(parser);
            if (bytes_node) {
                if (program->data.program.decl_count < 1000) {
                    program->data.program.declarations[program->data.program.decl_count++] = bytes_node;
                }
            }
            while (match(parser, TK_NEWLINE) || match(parser, TK_SEMICOLON));
            continue;
        }
        
        /* 处理顶级 map 定义 - 内存拓扑ASCII映射 */
        if (check(parser, TK_MAP)) {
            ASTNode *map_node = parse_map_definition(parser);
            if (map_node) {
                if (program->data.program.decl_count < 1000) {
                    program->data.program.declarations[program->data.program.decl_count++] = map_node;
                }
            }
            while (match(parser, TK_NEWLINE) || match(parser, TK_SEMICOLON));
            continue;
        }
        
        /* 处理顶级 syntax 定义 - 自定义语法扩展 */
        if (check(parser, TK_SYNTAX)) {
            ASTNode *syntax_node = parse_syntax_definition(parser);
            if (syntax_node) {
                if (program->data.program.decl_count < 1000) {
                    program->data.program.declarations[program->data.program.decl_count++] = syntax_node;
                }
            }
            while (match(parser, TK_NEWLINE) || match(parser, TK_SEMICOLON));
            continue;
        }
        
        /* 处理 const 常量声明 */
        if (check(parser, TK_CONST)) {
            advance(parser);
            if (!check(parser, TK_IDENT)) {
                error(parser, "Expected identifier after const");
                advance(parser);
                while (match(parser, TK_NEWLINE));
                continue;
            }
            
            Token const_name = current_token(parser);
            advance(parser);
            
            ASTNode *const_decl = ast_create_node(AST_VAR_DECL);
            const_decl->data.var_decl.name = (char *)parser_intern_string(parser, const_name.lexeme);
            const_decl->data.var_decl.is_const = 1;
            const_decl->data.var_decl.is_mutable = 0;
            
            /* 类型注解 */
            if (match(parser, TK_COLON)) {
                while (match(parser, TK_NEWLINE));
                parse_type(parser);
            }
            
            /* 初始化值 */
            if (match(parser, TK_ASSIGN)) {
                while (match(parser, TK_NEWLINE));
                const_decl->data.var_decl.init_value = parse_expression(parser);
            }
            
            while (match(parser, TK_NEWLINE) || match(parser, TK_SEMICOLON));
            program->data.program.declarations[program->data.program.decl_count++] = const_decl;
            continue;
        }
        
        /* 处理 unmanaged 关键字 */
        int is_unmanaged_decl = 0;
        if (check(parser, TK_UNMANAGED)) {
            advance(parser);
            is_unmanaged_decl = 1;
            (void)is_unmanaged_decl;  // unmanaged标记用于未来扩展
        }
        
        /* 处理 extern */
        int is_extern = 0;
        if (check(parser, TK_EXTERN)) {
            advance(parser);
            is_extern = 1;
        }

        /* 函数声明 */
        if (check(parser, TK_FUNC)) {
            advance(parser);
            if (!check(parser, TK_IDENT)) {
                error(parser, "Expected function name");
                break;
            }
            Token name = current_token(parser);
            advance(parser);
            
            ASTNode *func = ast_create_node(AST_FUNC_DECL);
            func->data.func_decl.name = (char *)parser_intern_string(parser, name.lexeme);
            func->data.func_decl.is_extern = is_extern;
            /* [TODO-06] 保存装饰器信息 */
            func->data.func_decl.attributes = attributes;
            func->data.func_decl.attr_count = attr_count;
            
            func->data.func_decl.params = (ASTNode**)safe_malloc(sizeof(ASTNode*) * 64);
            func->data.func_decl.param_count = 0;
            func->data.func_decl.return_type = NULL;
            func->data.func_decl.body = NULL;

            /* 参数列表 */
            if (match(parser, TK_LPAREN)) {
                while (!check(parser, TK_RPAREN) && !check(parser, TK_EOF)) {
                    while (match(parser, TK_NEWLINE));
                    if (check(parser, TK_RPAREN)) break;

                    /* 参数名 */
                    if (!check(parser, TK_IDENT) && !check(parser, TK_PTR) && !check(parser, TK_VIEW) &&
                        !check(parser, TK_REG) && !check(parser, TK_METAL) && !check(parser, TK_ASM) &&
                        !check(parser, TK_BYTES)) {
                        error(parser, "Expected parameter name");
                        /* 错误恢复：跳到下一个逗号或右括号 */
                        while (!check(parser, TK_COMMA) && !check(parser, TK_RPAREN) && !check(parser, TK_EOF)) {
                            advance(parser);
                        }
                        if (check(parser, TK_COMMA)) advance(parser);
                        continue;
                    }

                    Token param_name = current_token(parser);
                    advance(parser);

                    ASTNode *param = ast_create_node(AST_VAR_DECL);
                    param->data.var_decl.name = (char *)parser_intern_string(parser, param_name.lexeme);
                    param->data.var_decl.type = NULL;
                    param->data.var_decl.init = NULL;
                    param->data.var_decl.init_value = NULL;
                    param->data.var_decl.is_const = 0;
                    param->data.var_decl.is_mutable = 1;

                    while (match(parser, TK_NEWLINE));
                    if (match(parser, TK_COLON)) {
                        while (match(parser, TK_NEWLINE));
                        param->data.var_decl.type = parse_type(parser);
                    }

                    if (func->data.func_decl.param_count < 64) {
                        func->data.func_decl.params[func->data.func_decl.param_count++] = param;
                    }

                    while (match(parser, TK_NEWLINE));
                    if (match(parser, TK_COMMA)) {
                        continue;
                    }
                    if (check(parser, TK_RPAREN)) {
                        break;
                    }
                    /* 容错：如果既不是逗号也不是右括号，尝试继续 */
                    if (!check(parser, TK_EOF)) {
                        error(parser, "Expected ',' or ')' after parameter");
                        while (!check(parser, TK_COMMA) && !check(parser, TK_RPAREN) && !check(parser, TK_EOF)) {
                            advance(parser);
                        }
                        if (match(parser, TK_COMMA)) continue;
                    }
                }

                if (!match(parser, TK_RPAREN)) {
                    error(parser, "Expected ')' after parameter list");
                }
            }
            
            // 跳过换行符后再查找返回类型
            while (match(parser, TK_NEWLINE));
            
            // 返回类型
            if (match(parser, TK_ARROW) || match(parser, TK_COLON)) {
                func->data.func_decl.return_type = parse_type(parser);
            }
            
            if (is_extern) {
                // extern 声明没有函数体
                while (match(parser, TK_NEWLINE));
            } else {
                func->data.func_decl.body = parse_block(parser);
            }
            
            program->data.program.declarations[program->data.program.decl_count++] = func;
        }
        /* 结构体声明 */
        else if (check(parser, TK_STRUCT)) {
            advance(parser);
            if (!check(parser, TK_IDENT)) {
                error(parser, "Expected struct name");
                break;
            }
            
            Token struct_name = current_token(parser);
            advance(parser);
            
            ASTNode *struct_decl = ast_create_node(AST_STRUCT_DECL);
            struct_decl->data.struct_decl.name = (char *)parser_intern_string(parser, struct_name.lexeme);
            struct_decl->data.struct_decl.attributes = attributes;
            struct_decl->data.struct_decl.attr_count = attr_count;
            struct_decl->data.struct_decl.field_names = NULL;
            struct_decl->data.struct_decl.field_types = NULL;
            struct_decl->data.struct_decl.field_count = 0;
            
            // 解析结构体字段和方法
            if (match(parser, TK_LBRACE)) {
                int capacity = 16;
                struct_decl->data.struct_decl.field_names = (char**)safe_malloc(sizeof(char*) * capacity);
                struct_decl->data.struct_decl.field_types = (ASTNode**)safe_malloc(sizeof(ASTNode*) * capacity);
                
                while (!check(parser, TK_RBRACE) && !check(parser, TK_EOF)) {
                    while (match(parser, TK_NEWLINE));
                    if (check(parser, TK_RBRACE)) break;
                    
                    /* 检查是否是方法定义 */
                    int is_mutating_method = 0;
                    if (check(parser, TK_IDENT) && strcmp(current_token(parser).lexeme, "mutating") == 0) {
                        is_mutating_method = 1;
                        (void)is_mutating_method;  // mutating标记用于未来扩展
                        advance(parser);  // 消耗 mutating
                    }
                    
                    /* 如果是方法定义（func 关键字），跳过整个方法 */
                    if (check(parser, TK_FUNC)) {
                        advance(parser);  // 消耗 func
                        
                        /* 跳过方法名 */
                        if (check(parser, TK_IDENT)) advance(parser);
                        
                        /* 跳过参数列表 */
                        if (match(parser, TK_LPAREN)) {
                            int paren_depth = 1;
                            while (paren_depth > 0 && !check(parser, TK_EOF)) {
                                if (check(parser, TK_LPAREN)) paren_depth++;
                                else if (check(parser, TK_RPAREN)) paren_depth--;
                                advance(parser);
                            }
                        }
                        
                        // 跳过换行符后再查找返回类型
                        while (match(parser, TK_NEWLINE));
                        
                        /* 跳过返回类型（如果有 -> Type） */
                        if (match(parser, TK_ARROW) || match(parser, TK_COLON)) {
                            parse_type(parser);
                        }
                        
                        /* 跳过方法体 */
                        if (check(parser, TK_LBRACE)) {
                            int brace_depth = 0;
                            if (match(parser, TK_LBRACE)) {
                                brace_depth = 1;
                                while (brace_depth > 0 && !check(parser, TK_EOF)) {
                                    if (check(parser, TK_LBRACE)) brace_depth++;
                                    else if (check(parser, TK_RBRACE)) brace_depth--;
                                    advance(parser);
                                }
                            }
                        }
                        
                        continue;
                    }
                    
                    /* 支持 var/let 前缀（可选） */
                    if (check(parser, TK_VAR) || check(parser, TK_LET)) {
                        advance(parser);  // 消耗 var/let
                    }
                    
                    /* 允许任何 token 作为字段名，包括保留字（如 protocol, default 等）
                     * 这是必要的因为像 protocol 这样的词在某些 Aethelium 代码中被用作字段名
                     */
                    if (check(parser, TK_EOF) || check(parser, TK_RBRACE)) {
                        error(parser, "Expected field name or method definition");
                        break;
                    }
                    
                    Token field_name = current_token(parser);
                    advance(parser);
                    
                    if (!match(parser, TK_COLON)) {
                        error(parser, "Expected ':' after field name");
                        break;
                    }
                    
                    ASTNode *field_type = parse_type(parser);
                    
                    /* 支持硬件层字段位置指定符 (at 0x20) - 用于MMIO结构体 */
                    if (check(parser, TK_AT)) {
                        advance(parser);  // 消耗 'at'
                        /* 解析位置表达式（通常是十六进制数字常量） */
                        if (check(parser, TK_INT)) {
                            advance(parser);  // 消耗数字
                        } else if (check(parser, TK_IDENT)) {
                            advance(parser);  // 消耗标识符
                        } else {
                            error(parser, "Expected offset value after 'at'");
                        }
                    }
                    
                    /* 支持默认值初始化 (= init_value) */
                    if (match(parser, TK_ASSIGN)) {
                        parse_expression(parser);  // 解析但暂时忽略默认值
                    }
                    
                    if (struct_decl->data.struct_decl.field_count >= capacity) {
                        int new_capacity = capacity * 2;
                        char **new_names = (char**)realloc(struct_decl->data.struct_decl.field_names,
                                                           sizeof(char*) * new_capacity);
                        ASTNode **new_types = (ASTNode**)realloc(struct_decl->data.struct_decl.field_types,
                                                                 sizeof(ASTNode*) * new_capacity);
                        if (!new_names || !new_types) {
                            error(parser, "Out of memory while growing struct field table");
                            if (new_names) struct_decl->data.struct_decl.field_names = new_names;
                            if (new_types) struct_decl->data.struct_decl.field_types = new_types;
                            break;
                        }
                        struct_decl->data.struct_decl.field_names = new_names;
                        struct_decl->data.struct_decl.field_types = new_types;
                        capacity = new_capacity;
                    }
                    struct_decl->data.struct_decl.field_names[struct_decl->data.struct_decl.field_count] =
                        safe_strdup(field_name.lexeme);
                    struct_decl->data.struct_decl.field_types[struct_decl->data.struct_decl.field_count] =
                        field_type;
                    struct_decl->data.struct_decl.field_count++;
                    
                    if (match(parser, TK_COMMA)) continue;
                    while (match(parser, TK_NEWLINE) || match(parser, TK_SEMICOLON));
                }
                
                if (!match(parser, TK_RBRACE)) {
                    error(parser, "Expected '}' after struct fields");
                }
            }
            
            program->data.program.declarations[program->data.program.decl_count++] = struct_decl;
        }
        /* 枚举声明 */
        else if (check(parser, TK_ENUM)) {
            advance(parser);
            if (!check(parser, TK_IDENT)) {
                error(parser, "Expected enum name");
                break;
            }
            
            Token enum_name = current_token(parser);
            advance(parser);
            
            ASTNode *enum_decl = ast_create_node(AST_ENUM_DECL);
            enum_decl->data.enum_decl.name = (char *)parser_intern_string(parser, enum_name.lexeme);
            enum_decl->data.enum_decl.case_names = NULL;
            enum_decl->data.enum_decl.case_count = 0;
            
            // 解析枚举情况
            if (match(parser, TK_LBRACE)) {
                int capacity = 16;
                enum_decl->data.enum_decl.case_names = (char**)safe_malloc(sizeof(char*) * capacity);
                
                while (!check(parser, TK_RBRACE) && !check(parser, TK_EOF)) {
                    while (match(parser, TK_NEWLINE));
                    if (check(parser, TK_RBRACE)) break;
                    
                    if (!check(parser, TK_IDENT) && !check(parser, TK_CASE)) {
                        break;
                    }
                    
                    if (match(parser, TK_CASE)) {
                        // case CaseName
                        if (!check(parser, TK_IDENT)) {
                            error(parser, "Expected case name");
                            break;
                        }
                    }
                    
                    if (check(parser, TK_IDENT)) {
                        Token case_name = current_token(parser);
                        advance(parser);
                        
                        /* 支持值赋值: Name = value 或关联值: case Name(Type1, Type2) */
                        if (match(parser, TK_ASSIGN)) {
                            /* 跳过 = 后的值表达式 */
                            parse_expression(parser);
                        } else if (match(parser, TK_LPAREN)) {
                            /* 解析关联类型列表 - 使用括号深度来跟踪 */
                            int paren_depth = 1;
                            int bracket_depth = 0;  // 用于跟踪方括号深度
                            (void)bracket_depth;  // 避免未使用变量警告
                            
                            while (!check(parser, TK_EOF) && paren_depth > 0) {
                                if (check(parser, TK_LPAREN)) {
                                    paren_depth++;
                                    advance(parser);
                                } else if (check(parser, TK_RPAREN)) {
                                    paren_depth--;
                                    if (paren_depth > 0) {
                                        advance(parser);
                                    }
                                } else if (check(parser, TK_LBRACKET)) {
                                    bracket_depth++;
                                    advance(parser);
                                } else if (check(parser, TK_RBRACKET)) {
                                    bracket_depth--;
                                    advance(parser);
                                } else {
                                    advance(parser);
                                }
                            }
                            
                            if (!match(parser, TK_RPAREN)) {
                                // 如果已经遇到错误，尝试恢复
                                while (!check(parser, TK_RPAREN) && !check(parser, TK_EOF) && 
                                       !check(parser, TK_COMMA) && !check(parser, TK_RBRACE)) {
                                    advance(parser);
                                }
                                if (check(parser, TK_RPAREN)) {
                                    advance(parser);
                                }
                            }
                        }
                        
                        if (enum_decl->data.enum_decl.case_count < capacity) {
                            enum_decl->data.enum_decl.case_names[enum_decl->data.enum_decl.case_count++] =
                                safe_strdup(case_name.lexeme);
                        }
                    }
                    
                    if (match(parser, TK_COMMA)) continue;
                    while (match(parser, TK_NEWLINE) || match(parser, TK_SEMICOLON));
                }
                
                if (!match(parser, TK_RBRACE)) {
                    error(parser, "Expected '}' after enum cases");
                }
            }
            
            program->data.program.declarations[program->data.program.decl_count++] = enum_decl;
        }
        /* 变量声明 */
        else if (check(parser, TK_VAR) || check(parser, TK_LET)) {
            int pos_before = parser->pos;
            ASTNode *var_decl = parse_statement(parser); // 复用语句解析
            if (var_decl) {
                if (program->data.program.decl_count < 100) {
                    program->data.program.declarations[program->data.program.decl_count++] = var_decl;
                }
            }
            // 确保位置至少前进了一个token
            if (parser->pos == pos_before && !check(parser, TK_EOF)) {
                advance(parser);
            }
            // 跳过到下一个声明或可能的语句
            while (match(parser, TK_NEWLINE) || match(parser, TK_SEMICOLON));
        }
        else {
            // 无法识别的声明，执行激进的错误恢复
            (void)current_token(parser);  // token保存用于调试目的
            int pos_before = parser->pos;
            
            if (!check(parser, TK_EOF)) {
                // 激进跳过策略：找到下一个顶级声明或语句标记
                int bracket_depth = 0;
                int paren_depth = 0;
                int brace_depth = 0;
                int loop_count = 0;
                int max_skip = 500;  // 增加跳过token数量限制
                
                while (loop_count++ < max_skip && !check(parser, TK_EOF)) {
                    // 追踪括号深度
                    if (check(parser, TK_LBRACKET)) bracket_depth++;
                    else if (check(parser, TK_RBRACKET)) bracket_depth--;
                    else if (check(parser, TK_LPAREN)) paren_depth++;
                    else if (check(parser, TK_RPAREN)) paren_depth--;
                    else if (check(parser, TK_LBRACE)) brace_depth++;
                    else if (check(parser, TK_RBRACE)) brace_depth--;
                    
                    // 如果找到顶级的声明关键字（括号外），就停止
                    if (bracket_depth == 0 && paren_depth == 0 && brace_depth == 0) {
                        // 添加对顶级系统块的支持 (metal, asm, bytes)
                        if (check(parser, TK_FUNC) || check(parser, TK_STRUCT) || 
                            check(parser, TK_ENUM) || check(parser, TK_VAR) || 
                            check(parser, TK_LET) || check(parser, TK_CONST) ||
                            check(parser, TK_IMPORT) || check(parser, TK_RIMPORT) ||
                            check(parser, TK_USE) || check(parser, TK_METAL) ||
                            check(parser, TK_ASM) || check(parser, TK_BYTES)) {
                            break;
                        }
                        // 也可以在换行后停止
                        if (check(parser, TK_NEWLINE) && loop_count > 3) {
                            advance(parser);
                            break;
                        }
                    }
                    
                    advance(parser);
                }
                
                // 确保至少前进了一个位置，避免无限循环
                if (parser->pos == pos_before) {
                    advance(parser);
                }
            }
        }
        
        while (check(parser, TK_NEWLINE) || check(parser, TK_SEMICOLON)) advance(parser);
    }
    return program;
}

Parser* parser_create(Token *tokens, int debug) {
    Parser *parser = malloc(sizeof(Parser));
    if (!parser) {
        fprintf(stderr, "[FATAL] Failed to allocate Parser\n");
        return NULL;
    }
    
    parser->tokens = tokens;
    parser->pos = 0;
    strcpy(parser->error, "");
    parser->error_count = 0;
    parser->max_errors = 1;  /* 工业级别：第一个错误就停止 */
    parser->panic_mode = 0;
    parser->debug = debug;   /* 设置debug标志 */
    
    /* 激进方案：初始化嵌套泛型深度追踪与虚拟token回放 */
    parser->generic_nesting_depth = 0;
    parser->has_virtual_gt_token = 0;
    
    /* 工业级：初始化字符串表 */
    parser->string_table = NULL;
    parser->owns_string_table = 0;
    
    return parser;
}

void parser_destroy(Parser *parser) {
    if (!parser) return;
    
    /* 清理字符串表（如果Parser负责管理） */
    if (parser->owns_string_table && parser->string_table) {
        stringtable_destroy(parser->string_table);
    }
    
    free(parser);
}

/* =====================================================================
 * 字符串表集成 API
 * ===================================================================== */

int parser_initialize_string_table(Parser *parser) {
    return parser_initialize_string_table_with_config(parser, NULL);
}

int parser_initialize_string_table_with_config(Parser *parser, void *config) {
    if (!parser) {
        fprintf(stderr, "[ERROR] NULL parser pointer\n");
        return -1;
    }
    
    /* 如果已有字符串表，先清理 */
    if (parser->owns_string_table && parser->string_table) {
        stringtable_destroy(parser->string_table);
    }
    
    /* 创建新的字符串表 */
    if (config) {
        parser->string_table = stringtable_create_with_config((StringTableConfig *)config);
    } else {
        parser->string_table = stringtable_create();
    }
    
    if (!parser->string_table) {
        fprintf(stderr, "[ERROR] Failed to create string table in parser\n");
        return -1;
    }
    
    parser->owns_string_table = 1;
    
    if (parser->debug) {
        fprintf(stderr, "[DEBUG] String table initialized for parser\n");
    }
    
    return 0;
}

StringTable* parser_get_string_table(Parser *parser) {
    if (!parser) {
        return NULL;
    }
    return parser->string_table;
}

void parser_set_string_table(Parser *parser, StringTable *table, int takes_ownership) {
    if (!parser) return;
    
    /* 清理旧的字符串表 */
    if (parser->owns_string_table && parser->string_table) {
        stringtable_destroy(parser->string_table);
    }
    
    parser->string_table = table;
    parser->owns_string_table = takes_ownership;
    
    if (parser->debug) {
        fprintf(stderr, "[DEBUG] String table set for parser (owns=%d)\n", takes_ownership);
    }
}

const char* parser_get_error(Parser *parser) {
    return parser->error;
}

int parser_get_error_count(Parser *parser) {
    if (!parser) return 0;
    return parser->error_count;
}

int parser_is_panic(Parser *parser) {
    if (!parser) return 0;
    return parser->panic_mode;
}

void ast_print(ASTNode *node, int depth) {
    if (!node) return;
    
    char indent[256] = "";
    for (int i = 0; i < depth; i++) strcat(indent, "  ");
    
    printf("%s", indent);
    
    switch (node->type) {
    case AST_PROGRAM:
        printf("Program (%d declarations)\n", node->data.program.decl_count);
        for (int i = 0; i < node->data.program.decl_count; i++) {
            ast_print(node->data.program.declarations[i], depth + 1);
        }
        break;
    case AST_FUNC_DECL:
        printf("FuncDecl: %s\n", node->data.func_decl.name);
        if (node->data.func_decl.body) {
            ast_print(node->data.func_decl.body, depth + 1);
        }
        break;
    case AST_VAR_DECL:
        printf("VarDecl: %s\n", node->data.var_decl.name);
        break;
    case AST_RETURN_STMT:
        printf("Return\n");
        break;
    case AST_LITERAL:
        printf("Literal: %lld\n", node->data.literal.value.int_value);
        break;
    case AST_IDENT:
        printf("Ident: %s\n", node->data.ident.name);
        break;
    default:
        printf("Node(type=%d)\n", node->type);
    }
}

/* Alias function for backward compatibility */
ASTNode* parser_parse(Parser *parser) {
    return parser_parse_program(parser);
}
