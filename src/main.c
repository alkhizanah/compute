#include <assert.h>
#include <malloc.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "expr.h"
#include "pool.h"

static void display(Pool *pool, ExprIdx input) {
    switch (pool->tags[input]) {
    case EXPR_VAR:
        printf("%s", pool->payloads[input].var);
        break;

    case EXPR_VAL:
        printf("%lg", pool->payloads[input].val);
        break;

    case EXPR_FN: {
        ExprFn f = pool->payloads[input].fn;

        printf("%s(", f.name);

        printf("%s", pool->payloads[f.first_parameter].var);

        for (size_t i = 1; i < f.parameters_len; i++) {
            printf(", %s", pool->payloads[f.first_parameter + i].var);
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

    default:
        printf("UNREACHABLE");
        break;
    }
}

static void displayln(Pool *pool, ExprIdx input) {
    display(pool, input);
    printf("\n");
}

static ExprIdx replace(Pool *pool, ExprIdx input, const char *variable,
                       ExprIdx argument) {
    switch (pool->tags[input]) {
    case EXPR_VAR:
        if (strcmp(pool->payloads[input].var, variable) == 0) {
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
    ExprFn function = pool->payloads[function_index].fn;

    ExprIdx result = function.body;

    for (size_t i = 0; i < function.parameters_len; i++) {
        result = replace(pool, result,
                         pool->payloads[function.first_parameter + i].var,
                         pool_push_val(pool, arguments[i]));
    }

    return result;
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

    default:
        return input;
        break;
    }
}

int main() {
    Pool pool = {0};

    ExprIdx f = pool_push_fn(&pool, "f", (const char *[]){"x", "y"}, 2,
                             pool_push_add(&pool, pool_push_var(&pool, "x"),
                                           pool_push_var(&pool, "y")));

    displayln(&pool, f);

    ExprIdx e = call(&pool, f, (double[]){1.0, 2.0});

    displayln(&pool, e);

    e = simplify(&pool, e);

    displayln(&pool, e);
}
