#pragma once

#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <ctype.h>

#include "expr.h"
#include "pool.c"

typedef enum : uint8_t {
    TOK_EOF,
    TOK_INVALID,
    TOK_VAR,
    TOK_VAL,
    TOK_OPAREN,
    TOK_CPAREN,
    TOK_EQL,
    TOK_PLUS,
    TOK_MINUS,
    TOK_MULTIPLY,
    TOK_DIVIDE,
    TOK_COMMA,
} TokenTag;

typedef struct {
    uint32_t start;
    uint32_t end;
} Range;

typedef struct {
    TokenTag tag;
    Range range;
} Token;

typedef struct {
    const char *buffer;
    size_t index;
} Lexer;

static Token lexer_next(Lexer *lexer) {
    while (isspace(lexer->buffer[lexer->index]))
        lexer->index++;

    Token token = {.range = {lexer->index, lexer->index}};

    char character = lexer->buffer[lexer->index++];

    switch (character) {
    case 0:
        token.tag = TOK_EOF;
        break;

    case '(':
        token.tag = TOK_OPAREN;
        token.range.end = lexer->index;
        break;

    case ')':
        token.tag = TOK_CPAREN;
        token.range.end = lexer->index;
        break;

    case '=':
        token.tag = TOK_EQL;
        token.range.end = lexer->index;

        break;

    case '+':
        token.tag = TOK_PLUS;
        token.range.end = lexer->index;

        break;

    case '-':
        token.tag = TOK_MINUS;
        token.range.end = lexer->index;

        break;

    case '*':
        token.tag = TOK_MULTIPLY;
        token.range.end = lexer->index;

        break;

    case '/':
        token.tag = TOK_DIVIDE;
        token.range.end = lexer->index;

        break;

    case ',':
        token.tag = TOK_COMMA;
        token.range.end = lexer->index;

        break;

    default:
        if (isalpha(character) || character == '_') {
            while (isalnum(lexer->buffer[lexer->index]) ||
                   lexer->buffer[lexer->index] == '_')
                lexer->index++;

            token.tag = TOK_VAR;

            token.range.end = lexer->index;
        } else if (isdigit(character)) {
            token.tag = TOK_VAL;

            while (isalnum(lexer->buffer[lexer->index]) ||
                   lexer->buffer[lexer->index] == '.') {
                lexer->index++;
            }

            token.range.end = lexer->index;
        } else {
            token.tag = TOK_INVALID;
            token.range.end = lexer->index;
        }

        break;
    }

    return token;
}

static Token lexer_peek(Lexer *lexer) {
    size_t index = lexer->index;
    Token token = lexer_next(lexer);
    lexer->index = index;
    return token;
}

typedef enum : uint8_t {
    PR_LOWEST,
    PR_SUM,
    PR_PRODUCT,
    PR_PREFIX,
} Precedence;

static Precedence precedence_of(TokenTag token) {
    switch (token) {
    case TOK_PLUS:
    case TOK_MINUS:
        return PR_SUM;

    case TOK_MULTIPLY:
    case TOK_DIVIDE:
        return PR_PRODUCT;

    default:
        return PR_LOWEST;
    }
}

static ExprIdx parse_expr(Pool *pool, Lexer *lexer, Precedence precedence);

static ExprIdx parse_unary(Pool *pool, Lexer *lexer) {
    switch (lexer_peek(lexer).tag) {
    case TOK_MINUS: {
        lexer_next(lexer);

        ExprIdx u = parse_expr(pool, lexer, PR_PREFIX);

        if (u == INVALID_EXPR_IDX) {
            return INVALID_EXPR_IDX;
        }

        return pool_push_neg(pool, u);
    }

    case TOK_VAL: {
        Token val_token = lexer_next(lexer);

        double val = strtod(lexer->buffer + val_token.range.start, NULL);

        return pool_push_val(pool, val);
    }

    case TOK_VAR: {
        Token var_token = lexer_next(lexer);

        return pool_push_var(pool, lexer->buffer + var_token.range.start,
                             var_token.range.end - var_token.range.start);
    }

    case TOK_OPAREN:
        lexer_next(lexer);

        ExprIdx expr = parse_expr(pool, lexer, PR_LOWEST);

        if (lexer_peek(lexer).tag != TOK_CPAREN) {
            return expr;
        }

        lexer_next(lexer);

        return expr;

    default:
        return INVALID_EXPR_IDX;
    }
}

static ExprIdx parse_binary_op(Pool *pool, Lexer *lexer, ExprIdx lhs,
                               ExprTag op) {
    Token op_token = lexer_next(lexer);

    ExprIdx rhs = parse_expr(pool, lexer, precedence_of(op_token.tag));

    if (rhs == INVALID_EXPR_IDX) {
        return INVALID_EXPR_IDX;
    }

    return pool_push_expr(pool, op,
                          (ExprPayload){.binary = {
                                            .lhs = lhs,
                                            .rhs = rhs,
                                        }});
}

static ExprIdx parse_binary(Pool *pool, Lexer *lexer, ExprIdx lhs) {
    switch (lexer_peek(lexer).tag) {
    case TOK_PLUS:
        return parse_binary_op(pool, lexer, lhs, EXPR_ADD);
    case TOK_MINUS:
        return parse_binary_op(pool, lexer, lhs, EXPR_SUB);
    case TOK_MULTIPLY:
        return parse_binary_op(pool, lexer, lhs, EXPR_MUL);
    case TOK_DIVIDE:
        return parse_binary_op(pool, lexer, lhs, EXPR_DIV);
    default:
        return INVALID_EXPR_IDX;
    }
}

static ExprIdx parse_expr(Pool *pool, Lexer *lexer, Precedence precedence) {
    ExprIdx lhs = parse_unary(pool, lexer);

    while (precedence_of(lexer_peek(lexer).tag) > precedence) {
        if (lhs == INVALID_EXPR_IDX)
            return INVALID_EXPR_IDX;

        lhs = parse_binary(pool, lexer, lhs);
    }

    return lhs;
}

static ExprIdx parse_fn(Pool *pool, Lexer *lexer, Token fn_name_token) {
    assert(lexer_next(lexer).tag == TOK_OPAREN);

    Token first_parameter_token = lexer_next(lexer);

    if (first_parameter_token.tag != TOK_VAR) {
        return INVALID_EXPR_IDX;
    }

    ExprIdx first_parameter = pool_push_var(
        pool, lexer->buffer + first_parameter_token.range.start,
        first_parameter_token.range.end - first_parameter_token.range.start);

    uint32_t parameters_len = 1;

    while (lexer_peek(lexer).tag == TOK_COMMA) {
        lexer_next(lexer);

        Token parameter_token = lexer_next(lexer);

        if (parameter_token.tag != TOK_VAR) {
            return INVALID_EXPR_IDX;
        }

        pool_push_var(pool, lexer->buffer + parameter_token.range.start,
                      parameter_token.range.end - parameter_token.range.start);

        parameters_len++;
    }

    if (lexer_next(lexer).tag != TOK_CPAREN) {
        return INVALID_EXPR_IDX;
    }

    if (lexer_next(lexer).tag != TOK_EQL) {
        return INVALID_EXPR_IDX;
    }

    ExprIdx body = parse_expr(pool, lexer, PR_LOWEST);

    if (body == INVALID_EXPR_IDX) {
        return INVALID_EXPR_IDX;
    }

    return pool_push_expr(
        pool, EXPR_FN,
        (ExprPayload){
            .fn = {
                .name = lexer->buffer + fn_name_token.range.start,
                .name_len = fn_name_token.range.end - fn_name_token.range.start,
                .first_parameter = first_parameter,
                .parameters_len = parameters_len,
                .body = body,
            }});
}

ExprIdx parse(Pool *pool, Lexer *lexer) {
    if (lexer_peek(lexer).tag == TOK_VAR) {
        size_t index = lexer->index;

        Token fn_name = lexer_next(lexer);

        if (lexer_peek(lexer).tag == TOK_OPAREN) {
            return parse_fn(pool, lexer, fn_name);
        } else {
            lexer->index = index;
        }
    }

    return parse_expr(pool, lexer, PR_LOWEST);
}
