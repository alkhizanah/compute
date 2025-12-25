#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "expr.h"
#include "parse.c"
#include "pool.c"

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
