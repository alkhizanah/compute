#include <assert.h>
#include <ctype.h>
#include <malloc.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t ExprIdx;

#define INVALID_EXPR_IDX (UINT32_MAX)

typedef enum : uint8_t {
    EXPR_VAR,
    EXPR_VAL,
    EXPR_FN,
    EXPR_ADD,
    EXPR_SUB,
    EXPR_MUL,
    EXPR_DIV,
    EXPR_NEG,
} ExprTag;

typedef struct {
    const char *name;
    uint32_t name_len;
} ExprVar;

typedef struct {
    const char *name;
    uint32_t name_len;
    ExprIdx first_parameter;
    uint32_t parameters_len;
    ExprIdx body;
} ExprFn;

typedef struct {
    ExprIdx lhs;
    ExprIdx rhs;
} ExprBinary;

typedef union {
    ExprVar var;
    double val;
    ExprFn fn;
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

static inline void pool_reset(Pool *pool) { pool->len = 0; }

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

static bool strings_equal(const char *lhs, uint32_t lhs_len, const char *rhs,
                          uint32_t rhs_len) {

    if (lhs == rhs)
        return true;

    if (lhs_len != rhs_len)
        return false;

    return strncmp(lhs, rhs, lhs_len) == 0;
}

// This doesn't mean that they do not simplify into eachother, meaning (2 + 2)
// != 4 structurally
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

    case EXPR_FN:
        if (lhs_payload.fn.parameters_len != rhs_payload.fn.parameters_len)
            return false;

        return exprs_structurally_equal(pool, lhs_payload.fn.body,
                                        rhs_payload.fn.body);

    case EXPR_ADD:
    case EXPR_SUB:
    case EXPR_MUL:
    case EXPR_DIV:
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

static ExprIdx simplify(Pool *pool, ExprIdx input) {
    switch (pool->tags[input]) {
    case EXPR_ADD: {
        ExprBinary binary = pool->payloads[input].binary;
        ExprIdx lhs = simplify(pool, binary.lhs);
        ExprIdx rhs = simplify(pool, binary.rhs);

        if (pool->tags[lhs] == EXPR_VAL && pool->tags[rhs] == EXPR_VAL) {
            return pool_push_val(pool, pool->payloads[lhs].val +
                                           pool->payloads[rhs].val);
        } else if (pool->tags[lhs] == EXPR_SUB &&
                   exprs_structurally_equal(
                       pool, pool->payloads[lhs].binary.rhs, rhs)) {
            return pool->payloads[lhs].binary.lhs;
        } else if (lhs != binary.lhs || rhs != binary.rhs) {
            return pool_push_add(pool, lhs, rhs);
        } else {
            return input;
        }
    }

    case EXPR_SUB: {
        ExprBinary binary = pool->payloads[input].binary;
        ExprIdx lhs = simplify(pool, binary.lhs);
        ExprIdx rhs = simplify(pool, binary.rhs);

        if (pool->tags[lhs] == EXPR_VAL && pool->tags[rhs] == EXPR_VAL) {
            return pool_push_val(pool, pool->payloads[lhs].val -
                                           pool->payloads[rhs].val);
        } else if (lhs != binary.lhs || rhs != binary.rhs) {
            return pool_push_sub(pool, lhs, rhs);
        } else {
            return input;
        }
    }

    case EXPR_MUL: {
        ExprBinary binary = pool->payloads[input].binary;
        ExprIdx lhs = simplify(pool, binary.lhs);
        ExprIdx rhs = simplify(pool, binary.rhs);

        if (pool->tags[lhs] == EXPR_VAL && pool->tags[rhs] == EXPR_VAL) {
            return pool_push_val(pool, pool->payloads[lhs].val *
                                           pool->payloads[rhs].val);
        } else if (lhs != binary.lhs || rhs != binary.rhs) {
            return pool_push_mul(pool, lhs, rhs);
        } else {
            return input;
        }
    }

    case EXPR_DIV: {
        ExprBinary binary = pool->payloads[input].binary;
        ExprIdx lhs = simplify(pool, binary.lhs);
        ExprIdx rhs = simplify(pool, binary.rhs);

        if (pool->tags[lhs] == EXPR_VAL && pool->tags[rhs] == EXPR_VAL) {
            return pool_push_val(pool, pool->payloads[lhs].val /
                                           pool->payloads[rhs].val);
        } else if (lhs != binary.lhs || rhs != binary.rhs) {
            return pool_push_div(pool, lhs, rhs);
        } else {
            return input;
        }
    }

    case EXPR_NEG: {
        ExprIdx unary = simplify(pool, pool->payloads[input].unary);

        if (pool->tags[unary] == EXPR_VAL) {
            return pool_push_val(pool, -pool->payloads[unary].val);
        } else if (unary != pool->payloads[input].unary) {
            return pool_push_neg(pool, unary);
        } else {
            return input;
        }
    }

    case EXPR_FN: {
        ExprFn fn = pool->payloads[input].fn;

        return pool_push_expr(
            pool, EXPR_FN,
            (ExprPayload){.fn = {
                              .name = fn.name,
                              .name_len = fn.name_len,
                              .first_parameter = fn.first_parameter,
                              .parameters_len = fn.parameters_len,
                              .body = simplify(pool, fn.body),
                          }});
    }

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

    case EXPR_FN: {
        ExprFn f = pool->payloads[input].fn;

        printf("%.*s(", (int)f.name_len, f.name);

        printf("%.*s", (int)pool->payloads[f.first_parameter].var.name_len,
               pool->payloads[f.first_parameter].var.name);

        for (size_t i = 1; i < f.parameters_len; i++) {
            printf(", %.*s",
                   (int)pool->payloads[f.first_parameter + i].var.name_len,
                   pool->payloads[f.first_parameter + i].var.name);
        }

        printf(") = ");

        display(pool, f.body);
        break;
    }

    case EXPR_ADD: {
        ExprBinary b = pool->payloads[input].binary;
        printf("(");
        display(pool, b.lhs);
        printf(" + ");
        display(pool, b.rhs);
        printf(")");
        break;
    }

    case EXPR_SUB: {
        ExprBinary b = pool->payloads[input].binary;
        printf("(");
        display(pool, b.lhs);
        printf(" - ");
        display(pool, b.rhs);
        printf(")");
        break;
    }

    case EXPR_MUL: {
        ExprBinary b = pool->payloads[input].binary;
        printf("(");
        display(pool, b.lhs);
        printf(" * ");
        display(pool, b.rhs);
        printf(")");
        break;
    }

    case EXPR_DIV: {
        ExprBinary b = pool->payloads[input].binary;
        printf("(");
        display(pool, b.lhs);
        printf(" / ");
        display(pool, b.rhs);
        printf(")");
        break;
    }

    case EXPR_NEG: {
        ExprIdx u = pool->payloads[input].unary;
        printf("-(");
        display(pool, u);
        printf(")");
        break;
    }

    default:
        printf("UNREACHABLE");
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

static ExprIdx call(Pool *pool, ExprIdx function_index, double *arguments) {
    ExprFn function = pool->payloads[function_index].fn;

    ExprIdx result = function.body;

    for (size_t i = 0; i < function.parameters_len; i++) {
        result = replace(pool, result,
                         pool->payloads[function.first_parameter + i].var,
                         pool_push_val(pool, arguments[i]));
    }

    return result;
}

int main() {
    Pool pool = {0};

    ExprIdx f = parse(&pool, &(Lexer){
                                 .buffer = "f(x, y) = x - y + y",
                             });

    if (f == INVALID_EXPR_IDX) {
        fprintf(stderr, "error: invalid expression");

        return 1;
    }

    displayln(&pool, f);
    displayln(&pool, simplify(&pool, f));
}
