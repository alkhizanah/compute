#pragma once

#include <stdint.h>

typedef uint32_t ExprIdx;

typedef enum : uint8_t {
    EXPR_VAR,
    EXPR_VAL,
    EXPR_FN,
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
} ExprFn;

typedef struct {
    ExprIdx lhs;
    ExprIdx rhs;
} ExprBinary;

typedef union {
    const char *var;
    double val;
    ExprFn fn;
    ExprBinary binary;
} ExprPayload;
