#include <assert.h>
#include <malloc.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t ExprIdx;

typedef enum : uint8_t {
    EXPR_VARIABLE,
    EXPR_CONSTANT,
    EXPR_FUNCTION,
    EXPR_ADD,
    EXPR_SUB,
    EXPR_MUL,
    EXPR_DIV,
} ExprTag;

typedef struct {
    const char *name;
    ExprIdx first_parameter;
    uint32_t parameters_len;
    ExprIdx body;
} ExprFunction;

typedef struct {
    ExprIdx lhs;
    ExprIdx rhs;
} ExprBinary;

typedef union {
    const char *variable;
    double constant;
    ExprFunction function;
    ExprBinary binary;
} ExprPayload;

typedef struct {
    ExprTag *tags;
    ExprPayload *payloads;
    size_t len;
    size_t capacity;
} Pool;

static ExprIdx pool_push_expr(Pool *pool, ExprTag tag, ExprPayload payload) {
    if (pool->len + 1 > pool->capacity) {
        size_t new_cap = pool->capacity ? pool->capacity * 2 : 4;

        pool->tags = realloc(pool->tags, sizeof(*pool->tags) * new_cap);

        pool->payloads =
            realloc(pool->payloads, sizeof(*pool->payloads) * new_cap);

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

static inline ExprIdx variable(Pool *pool, const char *variable) {
    return pool_push_expr(pool, EXPR_VARIABLE,
                          (ExprPayload){.variable = variable});
}

static inline ExprIdx constant(Pool *pool, double constant) {
    return pool_push_expr(pool, EXPR_CONSTANT,
                          (ExprPayload){.constant = constant});
}

static inline ExprIdx add(Pool *pool, ExprIdx lhs, ExprIdx rhs) {
    return pool_push_expr(pool, EXPR_ADD,
                          (ExprPayload){.binary = {
                                            .lhs = lhs,
                                            .rhs = rhs,
                                        }});
}

static inline ExprIdx sub(Pool *pool, ExprIdx lhs, ExprIdx rhs) {
    return pool_push_expr(pool, EXPR_SUB,
                          (ExprPayload){.binary = {
                                            .lhs = lhs,
                                            .rhs = rhs,
                                        }});
}

static inline ExprIdx mul(Pool *pool, ExprIdx lhs, ExprIdx rhs) {
    return pool_push_expr(pool, EXPR_MUL,
                          (ExprPayload){.binary = {
                                            .lhs = lhs,
                                            .rhs = rhs,
                                        }});
}

static inline ExprIdx divide(Pool *pool, ExprIdx lhs, ExprIdx rhs) {
    return pool_push_expr(pool, EXPR_DIV,
                          (ExprPayload){.binary = {
                                            .lhs = lhs,
                                            .rhs = rhs,
                                        }});
}

static ExprIdx function(Pool *pool, const char *name, const char **parameters,
                        size_t parameters_len, ExprIdx body) {
    assert(parameters_len != 0 &&
           "mathematical functions can not have 0 parameters");

    ExprIdx first_parameter = variable(pool, parameters[0]);

    for (size_t i = 1; i < parameters_len; i++) {
        variable(pool, parameters[i]);
    }

    return pool_push_expr(
        pool, EXPR_FUNCTION,
        (ExprPayload){.function = {.name = name,
                                   .first_parameter = first_parameter,
                                   .parameters_len = parameters_len,

                                   .body = body}});
}

static void display(Pool *pool, ExprIdx input) {
    switch (pool->tags[input]) {
    case EXPR_VARIABLE:
        printf("%s", pool->payloads[input].variable);
        break;

    case EXPR_CONSTANT:
        printf("%lg", pool->payloads[input].constant);
        break;

    case EXPR_FUNCTION: {
        ExprFunction function = pool->payloads[input].function;

        printf("%s(", function.name);

        printf("%s", pool->payloads[function.first_parameter].variable);

        for (size_t i = 1; i < function.parameters_len; i++) {
            printf(", %s",
                   pool->payloads[function.first_parameter + i].variable);
        }

        printf(") = ");

        display(pool, function.body);
        break;
    }

    case EXPR_ADD: {
        ExprBinary binary = pool->payloads[input].binary;
        display(pool, binary.lhs);
        printf(" + ");
        display(pool, binary.rhs);
        break;
    }

    case EXPR_SUB: {
        ExprBinary binary = pool->payloads[input].binary;
        display(pool, binary.lhs);
        printf(" - ");
        display(pool, binary.rhs);
        break;
    }

    case EXPR_MUL: {
        ExprBinary binary = pool->payloads[input].binary;
        display(pool, binary.lhs);
        printf(" * ");
        display(pool, binary.rhs);
        break;
    }

    case EXPR_DIV: {
        ExprBinary binary = pool->payloads[input].binary;
        display(pool, binary.lhs);
        printf(" / ");
        display(pool, binary.rhs);
        break;
    }

    default:
        printf("UNREACHABLE");
        break;
    }
}

static ExprIdx replace(Pool *pool, ExprIdx input, const char *variable,
                       ExprIdx argument) {
    switch (pool->tags[input]) {
    case EXPR_VARIABLE:
        if (strcmp(pool->payloads[input].variable, variable) == 0) {
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
                    .lhs = replace(pool, pool->payloads[input].binary.lhs,
                                   variable, argument),
                    .rhs = replace(pool, pool->payloads[input].binary.rhs,
                                   variable, argument),
                }});
    default:
        return input;
        break;
    }
}

static ExprIdx call(Pool *pool, ExprIdx function_index, double *arguments) {
    ExprIdx result;

    ExprFunction function = pool->payloads[function_index].function;

    for (size_t i = 0; i < function.parameters_len; i++) {
        result = replace(pool, function.body,
                         pool->payloads[function.first_parameter + i].variable,
                         constant(pool, arguments[i]));
    }

    return result;
}

static ExprIdx simplify(Pool *pool, ExprIdx input) {
    switch (pool->tags[input]) {
    case EXPR_ADD: {
        ExprBinary binary = pool->payloads[input].binary;
        ExprIdx lhs = simplify(pool, binary.lhs);
        ExprIdx rhs = simplify(pool, binary.rhs);

        if (pool->tags[lhs] == EXPR_CONSTANT &&
            pool->tags[rhs] == EXPR_CONSTANT) {
            return constant(pool, pool->payloads[lhs].constant +
                                      pool->payloads[rhs].constant);
        } else if (lhs != binary.lhs || rhs != binary.rhs) {
            return add(pool, lhs, rhs);
        } else {
            return input;
        }
    }

    case EXPR_SUB: {
        ExprBinary binary = pool->payloads[input].binary;
        ExprIdx lhs = simplify(pool, binary.lhs);
        ExprIdx rhs = simplify(pool, binary.rhs);

        if (pool->tags[lhs] == EXPR_CONSTANT &&
            pool->tags[rhs] == EXPR_CONSTANT) {
            return constant(pool, pool->payloads[lhs].constant -
                                      pool->payloads[rhs].constant);
        } else if (lhs != binary.lhs || rhs != binary.rhs) {
            return sub(pool, lhs, rhs);
        } else {
            return input;
        }
    }

    case EXPR_MUL: {
        ExprBinary binary = pool->payloads[input].binary;
        ExprIdx lhs = simplify(pool, binary.lhs);
        ExprIdx rhs = simplify(pool, binary.rhs);

        if (pool->tags[lhs] == EXPR_CONSTANT &&
            pool->tags[rhs] == EXPR_CONSTANT) {
            return constant(pool, pool->payloads[lhs].constant *
                                      pool->payloads[rhs].constant);
        } else if (lhs != binary.lhs || rhs != binary.rhs) {
            return mul(pool, lhs, rhs);
        } else {
            return input;
        }
    }

    case EXPR_DIV: {
        ExprBinary binary = pool->payloads[input].binary;
        ExprIdx lhs = simplify(pool, binary.lhs);
        ExprIdx rhs = simplify(pool, binary.rhs);

        if (pool->tags[lhs] == EXPR_CONSTANT &&
            pool->tags[rhs] == EXPR_CONSTANT) {
            return constant(pool, pool->payloads[lhs].constant /
                                      pool->payloads[rhs].constant);
        } else if (lhs != binary.lhs || rhs != binary.rhs) {
            return divide(pool, lhs, rhs);
        } else {
            return input;
        }
    }

    default:
        return input;
        break;
    }
}

int main() {
    Pool pool = {0};

    ExprIdx f = function(&pool, "f", (const char *[]){"x"}, 1,
                         add(&pool, variable(&pool, "x"), constant(&pool, 2)));

    display(&pool, f);

    printf("\n");

    ExprIdx e = call(&pool, f, (double[]){67.0});

    display(&pool, e);

    printf("\n");

    e = simplify(&pool, e);

    display(&pool, e);

    printf("\n");
}
