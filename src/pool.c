#pragma once

#include <assert.h>
#include <malloc.h>
#include <stddef.h>
#include <stdlib.h>

#include "expr.h"

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
