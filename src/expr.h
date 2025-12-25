#pragma once

#include <stdint.h>

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
