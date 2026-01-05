#include <assert.h>
#include <ctype.h>
#include <malloc.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "linenoise.c"

typedef uint32_t ExprIdx;

#define INVALID_EXPR_IDX (UINT32_MAX)

typedef enum : uint8_t {
    EXPR_VAR,
    EXPR_VAL,
    EXPR_ADD,
    EXPR_SUB,
    EXPR_MUL,
    EXPR_DIV,
    EXPR_POW,
    EXPR_NEG,
} ExprTag;

typedef struct {
    const char *name;
    uint32_t name_len;
} ExprVar;

typedef struct {
    ExprIdx lhs;
    ExprIdx rhs;
} ExprBinary;

typedef union {
    ExprVar var;
    double val;
    ExprIdx unary;
    ExprBinary binary;
} ExprPayload;

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
    TOK_POWER,
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

typedef struct {
    ExprTag *tags;
    ExprPayload *payloads;
    size_t len;
    size_t capacity;
} Pool;

static bool strings_equal(const char *lhs, uint32_t lhs_len, const char *rhs,
                          uint32_t rhs_len) {

    if (lhs == rhs)
        return true;

    if (lhs_len != rhs_len)
        return false;

    return strncmp(lhs, rhs, lhs_len) == 0;
}

static bool exprs_structurally_equal(Pool *pool, ExprIdx lhs, ExprIdx rhs) {
    if (lhs == rhs)
        return true;

    if (pool->tags[lhs] != pool->tags[rhs])
        return false;

    ExprPayload lhs_payload = pool->payloads[lhs];
    ExprPayload rhs_payload = pool->payloads[rhs];

    switch (pool->tags[lhs]) {
    case EXPR_VAR:

        return strings_equal(lhs_payload.var.name, lhs_payload.var.name_len,
                             rhs_payload.var.name, rhs_payload.var.name_len);
    case EXPR_VAL:
        return lhs_payload.val == rhs_payload.val;

    case EXPR_ADD:
    case EXPR_SUB:
    case EXPR_MUL:
    case EXPR_DIV:
    case EXPR_POW:
        return exprs_structurally_equal(pool, lhs_payload.binary.lhs,
                                        rhs_payload.binary.lhs) &&
               exprs_structurally_equal(pool, lhs_payload.binary.rhs,
                                        rhs_payload.binary.rhs);

    case EXPR_NEG:
        return exprs_structurally_equal(pool, lhs_payload.unary,
                                        rhs_payload.unary);
    }

    return false;
}

static inline bool token_is(const char *buffer, Token token,
                            const char *value) {
    return token.tag != TOK_INVALID &&
           strings_equal(buffer + token.range.start,
                         token.range.end - token.range.start, value,
                         strlen(value));
}

static bool is_binary_expr(Pool *pool, ExprIdx input) {
    ExprTag tag = pool->tags[input];

    return ((tag == EXPR_ADD) | (tag == EXPR_SUB) | (tag == EXPR_MUL) |
            (tag == EXPR_DIV) | (tag == EXPR_POW)) != 0;
}

static inline ExprIdx pool_push_expr(Pool *pool, ExprTag tag,
                                     ExprPayload payload) {
    if (pool->len + 1 > pool->capacity) {
        size_t new_cap = pool->capacity ? pool->capacity * 2 : 4;

        pool->tags =
            (ExprTag *)realloc(pool->tags, sizeof(*pool->tags) * new_cap);

        pool->payloads = (ExprPayload *)realloc(
            pool->payloads, sizeof(*pool->payloads) * new_cap);

        if (pool->tags == NULL || pool->payloads == NULL) {
            fprintf(stderr, "error: out of memory\n");

            exit(1);
        }

        pool->capacity = new_cap;
    }

    pool->tags[pool->len] = tag;
    pool->payloads[pool->len] = payload;

    return pool->len++;
}

static inline void pool_free(Pool *pool) {
    free(pool->tags);
    free(pool->payloads);
    pool->capacity = 0;
    pool->len = 0;
}

static inline ExprIdx pool_push_var(Pool *pool, const char *var_name,
                                    uint32_t var_name_len) {
    return pool_push_expr(pool, EXPR_VAR,
                          (ExprPayload){.var = {
                                            .name = var_name,
                                            .name_len = var_name_len,
                                        }});
}

static inline ExprIdx pool_push_val(Pool *pool, double val) {
    return pool_push_expr(pool, EXPR_VAL, (ExprPayload){.val = val});
}

static inline ExprIdx pool_push_add(Pool *pool, ExprIdx lhs, ExprIdx rhs) {
    return pool_push_expr(pool, EXPR_ADD,
                          (ExprPayload){.binary = {
                                            .lhs = lhs,
                                            .rhs = rhs,
                                        }});
}

static inline ExprIdx pool_push_sub(Pool *pool, ExprIdx lhs, ExprIdx rhs) {
    return pool_push_expr(pool, EXPR_SUB,
                          (ExprPayload){.binary = {
                                            .lhs = lhs,
                                            .rhs = rhs,
                                        }});
}

static inline ExprIdx pool_push_mul(Pool *pool, ExprIdx lhs, ExprIdx rhs) {
    return pool_push_expr(pool, EXPR_MUL,
                          (ExprPayload){.binary = {
                                            .lhs = lhs,
                                            .rhs = rhs,
                                        }});
}

static inline ExprIdx pool_push_div(Pool *pool, ExprIdx lhs, ExprIdx rhs) {
    return pool_push_expr(pool, EXPR_DIV,
                          (ExprPayload){.binary = {
                                            .lhs = lhs,
                                            .rhs = rhs,
                                        }});
}

static inline ExprIdx pool_push_pow(Pool *pool, ExprIdx lhs, ExprIdx rhs) {
    return pool_push_expr(pool, EXPR_POW,
                          (ExprPayload){.binary = {
                                            .lhs = lhs,
                                            .rhs = rhs,
                                        }});
}

static inline ExprIdx pool_push_neg(Pool *pool, ExprIdx unary) {
    return pool_push_expr(pool, EXPR_NEG, (ExprPayload){.unary = unary});
}

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

    case '^':
        token.tag = TOK_POWER;
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
            while (isalpha(lexer->buffer[lexer->index]))
                lexer->index++;

            if (lexer->buffer[lexer->index] == '_') {
                lexer->index++;

                while (isalnum(lexer->buffer[lexer->index]))
                    lexer->index++;
            }

            token.tag = TOK_VAR;

            token.range.end = lexer->index;
        } else if (isdigit(character)) {
            token.tag = TOK_VAL;

            while (isdigit(lexer->buffer[lexer->index]) ||
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
    PR_POWER,
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

    case TOK_POWER:
        return PR_POWER;

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

        if (lexer_peek(lexer).tag == TOK_VAR) {
            return pool_push_mul(pool, pool_push_val(pool, val),
                                 parse_unary(pool, lexer));
        } else {
            return pool_push_val(pool, val);
        }
    }

    case TOK_VAR: {
        Token var_token = lexer_next(lexer);

        if (lexer_peek(lexer).tag == TOK_VAR ||
            lexer_peek(lexer).tag == TOK_VAL) {
            return pool_push_mul(
                pool,
                pool_push_var(pool, lexer->buffer + var_token.range.start,
                              var_token.range.end - var_token.range.start),
                parse_unary(pool, lexer));
        } else {
            return pool_push_var(pool, lexer->buffer + var_token.range.start,
                                 var_token.range.end - var_token.range.start);
        }
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
    case TOK_POWER:
        return parse_binary_op(pool, lexer, lhs, EXPR_POW);
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

ExprIdx parse(Pool *pool, Lexer *lexer) {
    return parse_expr(pool, lexer, PR_LOWEST);
}

static ExprIdx simplify(Pool *pool, ExprIdx input);

static ExprIdx simplify_add(Pool *pool, ExprIdx input) {
    ExprIdx lhs = simplify(pool, pool->payloads[input].binary.lhs);
    ExprIdx rhs = simplify(pool, pool->payloads[input].binary.rhs);

    if (pool->tags[lhs] == EXPR_VAL) {
        if (pool->payloads[lhs].val == 0) {
            // 0 + x = x
            return rhs;
        } else if (pool->tags[rhs] == EXPR_VAL) {
            return pool_push_val(pool, pool->payloads[lhs].val +
                                           pool->payloads[rhs].val);
        }
    } else if (pool->tags[lhs] == EXPR_SUB) {
        ExprBinary b = pool->payloads[lhs].binary;

        if (exprs_structurally_equal(pool, b.lhs, rhs)) {
            // (x - y) + x = 2x - y
            return pool_push_sub(
                pool, pool_push_mul(pool, pool_push_val(pool, 2), rhs), b.rhs);
        } else if (exprs_structurally_equal(pool, b.rhs, rhs)) {
            // (y - x) + x = y
            return b.lhs;
        }
    } else if (pool->tags[rhs] == EXPR_SUB) {
        ExprBinary b = pool->payloads[rhs].binary;

        if (exprs_structurally_equal(pool, b.lhs, lhs)) {
            // x + (x - y) = 2x - y
            return pool_push_sub(
                pool, pool_push_mul(pool, pool_push_val(pool, 2), lhs), b.rhs);
        } else if (exprs_structurally_equal(pool, b.rhs, lhs)) {
            // x + (y - x)  = y
            return b.lhs;
        }
    } else if (pool->tags[lhs] == EXPR_MUL) {
        ExprBinary b = pool->payloads[lhs].binary;

        if (exprs_structurally_equal(pool, b.rhs, rhs)) {
            // (a * x) + x = (a + 1) * x
            return pool_push_mul(
                pool, pool_push_add(pool, b.lhs, pool_push_val(pool, 1)), rhs);
        } else if (exprs_structurally_equal(pool, b.lhs, rhs)) {
            // (x * a) + x = (a + 1) * x
            return pool_push_mul(
                pool, pool_push_add(pool, b.rhs, pool_push_val(pool, 1)), rhs);
        }
    } else if (pool->tags[rhs] == EXPR_MUL) {
        ExprBinary b = pool->payloads[rhs].binary;

        if (exprs_structurally_equal(pool, b.rhs, lhs)) {
            // x + (a * x) = (a + 1) * x
            return pool_push_mul(
                pool, pool_push_add(pool, b.lhs, pool_push_val(pool, 1)), lhs);
        } else if (exprs_structurally_equal(pool, b.lhs, lhs)) {
            // x + (x * a) = (a + 1) * x
            return pool_push_mul(
                pool, pool_push_add(pool, b.rhs, pool_push_val(pool, 1)), lhs);
        }
    } else if (pool->tags[rhs] == EXPR_VAL && pool->payloads[rhs].val == 0) {
        // x + 0 = x
        return lhs;
    } else if (pool->tags[rhs] == EXPR_NEG) {
        return pool_push_sub(pool, lhs, pool->payloads[rhs].unary);
    }

    if (exprs_structurally_equal(pool, lhs, rhs)) {
        // x + x = 2x
        return pool_push_mul(pool, pool_push_val(pool, 2), rhs);
    } else if (lhs != pool->payloads[input].binary.lhs ||
               rhs != pool->payloads[input].binary.rhs) {
        return pool_push_add(pool, lhs, rhs);
    } else {
        return input;
    }
}

static ExprIdx simplify_sub(Pool *pool, ExprIdx input) {
    ExprIdx lhs = simplify(pool, pool->payloads[input].binary.lhs);
    ExprIdx rhs = simplify(pool, pool->payloads[input].binary.rhs);

    if (pool->tags[lhs] == EXPR_VAL) {
        if (pool->payloads[lhs].val == 0) {
            // 0 - x = -x
            return pool_push_neg(pool, rhs);
        } else if (pool->tags[rhs] == EXPR_VAL) {
            return pool_push_val(pool, pool->payloads[lhs].val -
                                           pool->payloads[rhs].val);
        }
    } else if (pool->tags[lhs] == EXPR_ADD) {
        ExprBinary b = pool->payloads[lhs].binary;

        if (exprs_structurally_equal(pool, b.lhs, rhs)) {
            // (x + y) - x = y
            return b.rhs;
        } else if (exprs_structurally_equal(pool, b.rhs, rhs)) {
            // (y + x) - x = y
            return b.lhs;
        }
    } else if (pool->tags[rhs] == EXPR_ADD) {
        ExprBinary b = pool->payloads[rhs].binary;

        if (exprs_structurally_equal(pool, b.lhs, lhs)) {
            // x - (x + y) = -y
            return pool_push_neg(pool, b.rhs);
        } else if (exprs_structurally_equal(pool, b.rhs, lhs)) {
            // x - (y + x) = -y
            return pool_push_neg(pool, b.lhs);
        }
    } else if (pool->tags[lhs] == EXPR_MUL) {
        ExprBinary b = pool->payloads[lhs].binary;

        if (exprs_structurally_equal(pool, b.rhs, rhs)) {
            // (a * x) - x = (a - 1) * x
            return pool_push_mul(
                pool, pool_push_sub(pool, b.lhs, pool_push_val(pool, 1)), rhs);
        } else if (exprs_structurally_equal(pool, b.lhs, rhs)) {
            // (x * a) - x = (a - 1) * x
            return pool_push_mul(
                pool, pool_push_sub(pool, b.rhs, pool_push_val(pool, 1)), rhs);
        }
    } else if (pool->tags[rhs] == EXPR_MUL) {
        ExprBinary b = pool->payloads[rhs].binary;

        if (exprs_structurally_equal(pool, b.rhs, lhs)) {
            // x - (a * x) = (a - 1) * x
            return pool_push_mul(
                pool, pool_push_sub(pool, pool_push_val(pool, 1), b.lhs), lhs);
        } else if (pool->tags[b.rhs] == EXPR_VAL &&
                   exprs_structurally_equal(pool, b.lhs, lhs)) {
            // x - (x * a) = (1 - a) * x
            return pool_push_mul(
                pool, pool_push_sub(pool, pool_push_val(pool, 1), b.rhs), lhs);
        }
    } else if (pool->tags[rhs] == EXPR_VAL && pool->payloads[rhs].val == 0) {
        // x - 0 = x
        return lhs;
    } else if (pool->tags[rhs] == EXPR_NEG) {
        return pool_push_add(pool, lhs, pool->payloads[rhs].unary);
    }

    if (exprs_structurally_equal(pool, lhs, rhs)) {
        // x - x = 0
        return pool_push_val(pool, 0);
    } else if (lhs != pool->payloads[input].binary.lhs ||
               rhs != pool->payloads[input].binary.rhs) {
        return pool_push_sub(pool, lhs, rhs);
    } else {
        return input;
    }
}

static ExprIdx simplify_mul(Pool *pool, ExprIdx input) {
    ExprIdx lhs = simplify(pool, pool->payloads[input].binary.lhs);
    ExprIdx rhs = simplify(pool, pool->payloads[input].binary.rhs);

    if (pool->tags[lhs] == EXPR_VAL) {
        if (pool->payloads[lhs].val == 0) {
            // 0 * x = 0
            return lhs;
        } else if (pool->payloads[lhs].val == 1) {
            // 1 * x = x
            return rhs;
        } else if (pool->payloads[lhs].val == -1) {
            // -1 * x = -x
            return pool_push_neg(pool, rhs);
        } else if (pool->tags[rhs] == EXPR_VAL) {
            return pool_push_val(pool, pool->payloads[lhs].val *
                                           pool->payloads[rhs].val);
        }
    } else if (pool->tags[rhs] == EXPR_VAL) {
        if (pool->payloads[rhs].val == 0) {
            // x * 0 = 0
            return rhs;
        } else if (pool->payloads[rhs].val == 1) {
            // x * 1 = x
            return lhs;
        } else if (pool->payloads[rhs].val == -1) {
            // x * -1 = -x
            return pool_push_neg(pool, lhs);
        }
    } else if (pool->tags[lhs] == EXPR_POW) {
        ExprBinary b = pool->payloads[lhs].binary;

        if (exprs_structurally_equal(pool, b.lhs, rhs)) {
            // (x ^ a) * x = x ^ (a + 1)
            return pool_push_pow(
                pool, rhs, pool_push_add(pool, b.rhs, pool_push_val(pool, 1)));
        }
    } else if (pool->tags[rhs] == EXPR_POW) {
        ExprBinary b = pool->payloads[rhs].binary;

        if (exprs_structurally_equal(pool, b.lhs, lhs)) {
            // x * (x ^ a) = x ^ (a + 1)
            return pool_push_pow(
                pool, lhs, pool_push_add(pool, b.rhs, pool_push_val(pool, 1)));
        }
    }

    if (exprs_structurally_equal(pool, lhs, rhs)) {
        // x * x = x ^ 2
        return pool_push_pow(pool, lhs, pool_push_val(pool, 2));
    } else if (lhs != pool->payloads[input].binary.lhs ||
               rhs != pool->payloads[input].binary.rhs) {
        return pool_push_mul(pool, lhs, rhs);
    } else {
        return input;
    }
}

static ExprIdx simplify_div(Pool *pool, ExprIdx input) {
    ExprBinary binary = pool->payloads[input].binary;

    ExprIdx lhs = simplify(pool, binary.lhs);
    ExprIdx rhs = simplify(pool, binary.rhs);

    if (exprs_structurally_equal(pool, lhs, rhs)) {
        return pool_push_val(pool, 1);
    }

    if (pool->tags[rhs] == EXPR_VAL) {
        if (pool->payloads[rhs].val == 1) {
            // x / 1 = x
            return lhs;
        } else if (pool->tags[lhs] == EXPR_VAL) {
            return pool_push_val(pool, pool->payloads[lhs].val /
                                           pool->payloads[rhs].val);
        }
    } else if (pool->tags[lhs] == EXPR_MUL) {
        ExprBinary b = pool->payloads[lhs].binary;

        if (exprs_structurally_equal(pool, b.lhs, rhs)) {
            // (x * a) / a = x
            return b.rhs;
        } else if (exprs_structurally_equal(pool, b.rhs, rhs)) {
            // (a * x) / a = x
            return b.lhs;
        }
    } else if (pool->tags[rhs] == EXPR_MUL) {
        ExprBinary b = pool->payloads[rhs].binary;

        if (exprs_structurally_equal(pool, b.lhs, lhs)) {
            // a / (x * a) = 1 / x
            return pool_push_div(pool, pool_push_val(pool, 1), b.rhs);
        } else if (exprs_structurally_equal(pool, b.rhs, lhs)) {
            // a / (a * x) = 1 / x
            return pool_push_div(pool, pool_push_val(pool, 1), b.lhs);
        }
    }

    if (lhs != binary.lhs || rhs != binary.rhs) {
        return pool_push_div(pool, lhs, rhs);
    } else {
        return input;
    }
}

static ExprIdx simplify_pow(Pool *pool, ExprIdx input) {
    ExprBinary binary = pool->payloads[input].binary;

    ExprIdx lhs = simplify(pool, binary.lhs);
    ExprIdx rhs = simplify(pool, binary.rhs);

    if (pool->tags[lhs] == EXPR_VAL) {
        if (pool->payloads[lhs].val == 0) {
            // 0 ^ x = 0
            return lhs;
        } else if (pool->payloads[lhs].val == 1) {
            // 1 ^ x = 1
            return lhs;
        } else if (pool->tags[rhs] == EXPR_VAL) {
            return pool_push_val(
                pool, pow(pool->payloads[lhs].val, pool->payloads[rhs].val));
        }
    } else if (pool->tags[rhs] == EXPR_VAL) {
        if (pool->payloads[rhs].val == 0) {
            // x ^ 0 = 1
            return pool_push_val(pool, 1);
        } else if (pool->payloads[rhs].val == 1) {
            // x ^ 1 = x
            return lhs;
        }
    }

    if (lhs != binary.lhs || rhs != binary.rhs) {
        return pool_push_pow(pool, lhs, rhs);
    } else {
        return input;
    }
}

static ExprIdx simplify_neg(Pool *pool, ExprIdx input) {
    ExprIdx unary = simplify(pool, pool->payloads[input].unary);

    if (pool->tags[unary] == EXPR_VAL) {
        return pool_push_val(pool, -pool->payloads[unary].val);
    } else if (unary != pool->payloads[input].unary) {
        return pool_push_neg(pool, unary);
    } else {
        return input;
    }
}

static ExprIdx simplify(Pool *pool, ExprIdx input) {
    switch (pool->tags[input]) {
    case EXPR_ADD:
        return simplify_add(pool, input);

    case EXPR_SUB:
        return simplify_sub(pool, input);

    case EXPR_MUL:
        return simplify_mul(pool, input);

    case EXPR_DIV:
        return simplify_div(pool, input);

    case EXPR_POW:
        return simplify_pow(pool, input);

    case EXPR_NEG:
        return simplify_neg(pool, input);

    default:
        return input;
        break;
    }
}

static void display(Pool *pool, ExprIdx input) {
    switch (pool->tags[input]) {
    case EXPR_VAR:
        printf("%.*s", (int)pool->payloads[input].var.name_len,
               pool->payloads[input].var.name);
        break;

    case EXPR_VAL:
        printf("%lg", pool->payloads[input].val);
        break;

    case EXPR_ADD: {
        ExprBinary b = pool->payloads[input].binary;

        if (pool->tags[b.lhs] == EXPR_MUL || pool->tags[b.lhs] == EXPR_DIV) {
            printf("(");
            display(pool, b.lhs);
            printf(")");
        } else {
            display(pool, b.lhs);
        }

        printf(" + ");

        if (pool->tags[b.rhs] == EXPR_MUL || pool->tags[b.rhs] == EXPR_DIV) {
            printf("(");
            display(pool, b.rhs);
            printf(")");
        } else {
            display(pool, b.rhs);
        }

        break;
    }

    case EXPR_SUB: {
        ExprBinary b = pool->payloads[input].binary;

        if (pool->tags[b.lhs] == EXPR_MUL || pool->tags[b.lhs] == EXPR_DIV) {
            printf("(");
            display(pool, b.lhs);
            printf(")");
        } else {
            display(pool, b.lhs);
        }

        printf(" - ");

        if (is_binary_expr(pool, b.rhs)) {
            printf("(");
            display(pool, b.rhs);
            printf(")");
        } else {
            display(pool, b.rhs);
        }

        break;
    }

    case EXPR_MUL: {
        ExprBinary b = pool->payloads[input].binary;

        if (is_binary_expr(pool, b.lhs)) {
            printf("(");
            display(pool, b.lhs);
            printf(")");
        } else {
            display(pool, b.lhs);
        }

        printf(" * ");

        if (is_binary_expr(pool, b.rhs) && pool->tags[b.rhs] != EXPR_MUL) {
            printf("(");
            display(pool, b.rhs);
            printf(")");
        } else {
            display(pool, b.rhs);
        }

        break;
    }

    case EXPR_DIV: {
        ExprBinary b = pool->payloads[input].binary;

        if (is_binary_expr(pool, b.lhs)) {
            printf("(");
            display(pool, b.lhs);
            printf(")");
        } else {
            display(pool, b.lhs);
        }

        printf(" / ");

        if (is_binary_expr(pool, b.rhs)) {
            printf("(");
            display(pool, b.rhs);
            printf(")");
        } else {
            display(pool, b.rhs);
        }

        break;
    }

    case EXPR_POW: {
        ExprBinary b = pool->payloads[input].binary;

        if (is_binary_expr(pool, b.lhs)) {
            printf("(");
            display(pool, b.lhs);
            printf(")");
        } else {
            display(pool, b.lhs);
        }

        printf(" ^ ");

        if (is_binary_expr(pool, b.rhs)) {
            printf("(");
            display(pool, b.rhs);
            printf(")");
        } else {
            display(pool, b.rhs);
        }

        break;
    }

    case EXPR_NEG: {
        ExprIdx u = pool->payloads[input].unary;

        printf("-");

        if (is_binary_expr(pool, u)) {
            printf("(");
            display(pool, u);
            printf(")");
        } else {
            display(pool, u);
        }

        break;
    }

    default:
        assert(false && "UNREACHABLE");
        break;
    }
}

static void displayln(Pool *pool, ExprIdx input) {
    display(pool, input);
    printf("\n");
}

static ExprIdx replace(Pool *pool, ExprIdx input, ExprVar var,
                       ExprIdx argument) {
    switch (pool->tags[input]) {
    case EXPR_VAR:
        if (strings_equal(pool->payloads[input].var.name,
                          pool->payloads[input].var.name_len, var.name,
                          var.name_len)) {
            return argument;
        } else {
            return input;
        }

    case EXPR_ADD:
    case EXPR_SUB:
    case EXPR_MUL:
    case EXPR_DIV:
    case EXPR_POW:
        return pool_push_expr(
            pool, pool->tags[input],
            (ExprPayload){
                .binary = {
                    .lhs = replace(pool, pool->payloads[input].binary.lhs, var,
                                   argument),
                    .rhs = replace(pool, pool->payloads[input].binary.rhs, var,
                                   argument),
                }});

    default:
        return input;
        break;
    }
}

static ExprIdx replace_multiple(Pool *pool, ExprIdx input, ExprVar *vars,
                                ExprIdx *arguments, size_t len) {
    for (size_t i = 0; i < len; i++) {
        input = replace(pool, input, vars[i], arguments[i]);
    }

    return input;
}

void completion(const char *buf, linenoiseCompletions *lc) {
    (void)buf;
    (void)lc;
}

char *hints(const char *buf, int *color, int *bold) {
    (void)buf;
    (void)color;
    (void)bold;
    return NULL;
}

const char *find_history_file(void) {
#ifdef _WIN32
    const char *home_dir = getenv("USERPROFILE");
#else
    const char *home_dir = getenv("HOME");
#endif

    const char *history_file_name = ".compute_history";

    if (home_dir != NULL) {
        size_t home_dir_len = strlen(home_dir);

        char *history_file_path = malloc(
            sizeof(char) * (strlen(home_dir) + 1 + strlen(history_file_name)));

        strcpy(history_file_path, home_dir);
#ifdef _WIN32
        strcpy(history_file_path + home_dir_len, "\\");

#else
        strcpy(history_file_path + home_dir_len, "/");
#endif
        strcpy(history_file_path + home_dir_len + 1, history_file_name);

        return history_file_path;
    }

    return history_file_name;
}

int main() {
    Lexer lexer;

    Pool pool = {0};

    linenoiseSetCompletionCallback(completion);
    linenoiseSetHintsCallback(hints);

    const char *history_file_path = find_history_file();

    linenoiseHistoryLoad(history_file_path);

    while (true) {
        const char *line = linenoise(":> ");

        if (line == NULL) {
            if (errno == EAGAIN)
                continue;
            else if (errno == ENOENT)
                break;
        }

        linenoiseHistoryAdd(line);

        lexer.buffer = line;
        lexer.index = 0;

        // Cleanse the pool if it has elements more than 4096, to not leak
        // too much memory, otherwise reuse the capacity
        if (pool.capacity > 4096) {
            pool_free(&pool);
        } else {
            pool.len = 0;
        }

        if (lexer.buffer[lexer.index] == ':') {
            lexer.index++;

            Token command = lexer_next(&lexer);

            if (token_is(lexer.buffer, command, "bind")) {
                printf("todo: binding variables is unimplemented yet\n");
            } else {
                printf("error: unrecognized command: %.*s\n",
                       (int)(command.range.end - command.range.start),
                       lexer.buffer + command.range.start);
            }

            continue;
        }

        while (lexer_peek(&lexer).tag != TOK_EOF) {
            ExprIdx expr = parse(&pool, &lexer);

            if (expr == INVALID_EXPR_IDX)
                break;

            displayln(&pool, expr);

            ExprIdx next_expr = simplify(&pool, expr);

            while (next_expr != expr) {
                printf("= ");

                displayln(&pool, next_expr);

                expr = next_expr;
                next_expr = simplify(&pool, expr);
            }

            if (lexer_peek(&lexer).tag != TOK_EOF) {
                printf("\n");
            }
        }
    }

    linenoiseHistorySave(history_file_path);
}
