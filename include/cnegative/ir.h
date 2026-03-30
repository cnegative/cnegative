#ifndef CNEGATIVE_IR_H
#define CNEGATIVE_IR_H

#include "cnegative/ast.h"
#include "cnegative/diagnostics.h"
#include "cnegative/project.h"

#include <stdio.h>

typedef enum cn_ir_type_kind {
    CN_IR_TYPE_INT,
    CN_IR_TYPE_U8,
    CN_IR_TYPE_BOOL,
    CN_IR_TYPE_STR,
    CN_IR_TYPE_VOID,
    CN_IR_TYPE_RESULT,
    CN_IR_TYPE_PTR,
    CN_IR_TYPE_ARRAY,
    CN_IR_TYPE_NAMED,
    CN_IR_TYPE_UNKNOWN
} cn_ir_type_kind;

typedef struct cn_ir_type {
    cn_ir_type_kind kind;
    cn_strview module_name;
    cn_strview name;
    struct cn_ir_type *inner;
    size_t array_size;
} cn_ir_type;

typedef enum cn_ir_unary_op {
    CN_IR_UNARY_NEGATE,
    CN_IR_UNARY_NOT
} cn_ir_unary_op;

typedef enum cn_ir_binary_op {
    CN_IR_BINARY_ADD,
    CN_IR_BINARY_SUB,
    CN_IR_BINARY_MUL,
    CN_IR_BINARY_DIV,
    CN_IR_BINARY_EQUAL,
    CN_IR_BINARY_NOT_EQUAL,
    CN_IR_BINARY_LESS,
    CN_IR_BINARY_LESS_EQUAL,
    CN_IR_BINARY_GREATER,
    CN_IR_BINARY_GREATER_EQUAL,
    CN_IR_BINARY_AND,
    CN_IR_BINARY_OR
} cn_ir_binary_op;

typedef enum cn_ir_call_target_kind {
    CN_IR_CALL_BUILTIN,
    CN_IR_CALL_LOCAL,
    CN_IR_CALL_MODULE
} cn_ir_call_target_kind;

typedef enum cn_ir_expr_kind {
    CN_IR_EXPR_INT,
    CN_IR_EXPR_BOOL,
    CN_IR_EXPR_STRING,
    CN_IR_EXPR_LOCAL,
    CN_IR_EXPR_UNARY,
    CN_IR_EXPR_BINARY,
    CN_IR_EXPR_CALL,
    CN_IR_EXPR_ARRAY_LITERAL,
    CN_IR_EXPR_INDEX,
    CN_IR_EXPR_FIELD,
    CN_IR_EXPR_STRUCT_LITERAL,
    CN_IR_EXPR_OK,
    CN_IR_EXPR_ERR,
    CN_IR_EXPR_ALLOC,
    CN_IR_EXPR_ADDR,
    CN_IR_EXPR_DEREF
} cn_ir_expr_kind;

typedef struct cn_ir_expr cn_ir_expr;
typedef struct cn_ir_stmt cn_ir_stmt;
typedef struct cn_ir_block cn_ir_block;
typedef struct cn_ir_const cn_ir_const;
typedef struct cn_ir_struct cn_ir_struct;
typedef struct cn_ir_function cn_ir_function;
typedef struct cn_ir_module cn_ir_module;

typedef struct cn_ir_expr_list {
    cn_ir_expr **items;
    size_t count;
    size_t capacity;
} cn_ir_expr_list;

typedef struct cn_ir_field_init {
    cn_strview name;
    cn_ir_expr *value;
} cn_ir_field_init;

typedef struct cn_ir_field_init_list {
    cn_ir_field_init *items;
    size_t count;
    size_t capacity;
} cn_ir_field_init_list;

struct cn_ir_expr {
    cn_ir_expr_kind kind;
    cn_ir_type *type;
    size_t offset;
    union {
        int64_t int_value;
        bool bool_value;
        cn_strview string_value;
        cn_strview local_name;
        struct {
            cn_ir_unary_op op;
            cn_ir_expr *operand;
        } unary;
        struct {
            cn_ir_binary_op op;
            cn_ir_expr *left;
            cn_ir_expr *right;
        } binary;
        struct {
            cn_ir_call_target_kind target_kind;
            cn_strview module_name;
            cn_strview function_name;
            cn_ir_expr_list arguments;
        } call;
        struct {
            cn_ir_expr_list items;
        } array_literal;
        struct {
            cn_ir_expr *base;
            cn_ir_expr *index;
        } index;
        struct {
            cn_ir_expr *base;
            cn_strview field_name;
        } field;
        struct {
            cn_strview module_name;
            cn_strview type_name;
            cn_ir_field_init_list fields;
        } struct_literal;
        struct {
            cn_ir_expr *value;
        } ok_expr;
        struct {
            cn_ir_type *alloc_type;
        } alloc_expr;
        struct {
            cn_ir_expr *target;
        } addr_expr;
        struct {
            cn_ir_expr *target;
        } deref_expr;
    } data;
};

typedef enum cn_ir_stmt_kind {
    CN_IR_STMT_LET,
    CN_IR_STMT_ASSIGN,
    CN_IR_STMT_RETURN,
    CN_IR_STMT_EXPR,
    CN_IR_STMT_IF,
    CN_IR_STMT_WHILE,
    CN_IR_STMT_LOOP,
    CN_IR_STMT_FOR,
    CN_IR_STMT_FREE
} cn_ir_stmt_kind;

typedef struct cn_ir_stmt_list {
    cn_ir_stmt **items;
    size_t count;
    size_t capacity;
} cn_ir_stmt_list;

struct cn_ir_block {
    cn_ir_stmt_list statements;
    size_t offset;
};

struct cn_ir_stmt {
    cn_ir_stmt_kind kind;
    size_t offset;
    union {
        struct {
            cn_strview name;
            bool is_mutable;
            cn_ir_type *type;
            cn_ir_expr *initializer;
        } let_stmt;
        struct {
            cn_ir_expr *target;
            cn_ir_expr *value;
        } assign_stmt;
        struct {
            cn_ir_expr *value;
        } return_stmt;
        struct {
            cn_ir_expr *value;
        } expr_stmt;
        struct {
            cn_ir_expr *condition;
            cn_ir_block *then_block;
            cn_ir_block *else_block;
        } if_stmt;
        struct {
            cn_ir_expr *condition;
            cn_ir_block *body;
        } while_stmt;
        struct {
            cn_ir_block *body;
        } loop_stmt;
        struct {
            cn_strview name;
            cn_ir_type *type;
            cn_ir_expr *start;
            cn_ir_expr *end;
            cn_ir_block *body;
        } for_stmt;
        struct {
            cn_ir_expr *value;
        } free_stmt;
    } data;
};

typedef struct cn_ir_param {
    cn_strview name;
    cn_ir_type *type;
    size_t offset;
} cn_ir_param;

typedef struct cn_ir_param_list {
    cn_ir_param *items;
    size_t count;
    size_t capacity;
} cn_ir_param_list;

typedef struct cn_ir_struct_field {
    cn_strview name;
    cn_ir_type *type;
    size_t offset;
} cn_ir_struct_field;

typedef struct cn_ir_struct_field_list {
    cn_ir_struct_field *items;
    size_t count;
    size_t capacity;
} cn_ir_struct_field_list;

struct cn_ir_const {
    bool is_public;
    cn_strview module_name;
    cn_strview name;
    cn_ir_type *type;
    cn_ir_expr *initializer;
    size_t offset;
};

struct cn_ir_struct {
    bool is_public;
    cn_strview module_name;
    cn_strview name;
    cn_ir_struct_field_list fields;
    size_t offset;
};

struct cn_ir_function {
    bool is_public;
    cn_strview module_name;
    cn_strview name;
    cn_ir_type *return_type;
    cn_ir_param_list parameters;
    cn_ir_block *body;
    size_t offset;
};

typedef struct cn_ir_struct_ptr_list {
    cn_ir_struct **items;
    size_t count;
    size_t capacity;
} cn_ir_struct_ptr_list;

typedef struct cn_ir_const_ptr_list {
    cn_ir_const **items;
    size_t count;
    size_t capacity;
} cn_ir_const_ptr_list;

typedef struct cn_ir_function_ptr_list {
    cn_ir_function **items;
    size_t count;
    size_t capacity;
} cn_ir_function_ptr_list;

struct cn_ir_module {
    cn_strview name;
    cn_strview path;
    const cn_source *source;
    cn_ir_const_ptr_list consts;
    cn_ir_struct_ptr_list structs;
    cn_ir_function_ptr_list functions;
};

typedef struct cn_ir_module_ptr_list {
    cn_ir_module **items;
    size_t count;
    size_t capacity;
} cn_ir_module_ptr_list;

typedef struct cn_ir_program {
    cn_allocator *allocator;
    cn_ir_module_ptr_list modules;
} cn_ir_program;

cn_ir_type *cn_ir_type_create(
    cn_allocator *allocator,
    cn_ir_type_kind kind,
    cn_strview module_name,
    cn_strview name,
    cn_ir_type *inner,
    size_t array_size
);
cn_ir_type *cn_ir_type_clone(cn_allocator *allocator, const cn_ir_type *type);
cn_ir_type *cn_ir_type_from_ast(cn_allocator *allocator, const cn_type_ref *type);
void cn_ir_type_destroy(cn_allocator *allocator, cn_ir_type *type);
void cn_ir_type_describe(const cn_ir_type *type, char *buffer, size_t buffer_size);

cn_ir_program *cn_ir_program_create(cn_allocator *allocator);
cn_ir_module *cn_ir_module_create(cn_allocator *allocator, cn_strview name, cn_strview path, const cn_source *source);
cn_ir_const *cn_ir_const_create(cn_allocator *allocator, cn_strview module_name, cn_strview name, bool is_public, size_t offset);
cn_ir_struct *cn_ir_struct_create(cn_allocator *allocator, cn_strview module_name, cn_strview name, bool is_public, size_t offset);
cn_ir_function *cn_ir_function_create(cn_allocator *allocator, cn_strview module_name, cn_strview name, bool is_public, size_t offset);
cn_ir_block *cn_ir_block_create(cn_allocator *allocator, size_t offset);
cn_ir_stmt *cn_ir_stmt_create(cn_allocator *allocator, cn_ir_stmt_kind kind, size_t offset);
cn_ir_expr *cn_ir_expr_create(cn_allocator *allocator, cn_ir_expr_kind kind, size_t offset);

bool cn_ir_program_push_module(cn_ir_program *program, cn_ir_module *module);
bool cn_ir_module_push_const(cn_ir_module *module, cn_allocator *allocator, cn_ir_const *const_decl);
bool cn_ir_module_push_struct(cn_ir_module *module, cn_allocator *allocator, cn_ir_struct *struct_decl);
bool cn_ir_module_push_function(cn_ir_module *module, cn_allocator *allocator, cn_ir_function *function);
bool cn_ir_param_list_push(cn_allocator *allocator, cn_ir_param_list *list, cn_ir_param param);
bool cn_ir_stmt_list_push(cn_allocator *allocator, cn_ir_stmt_list *list, cn_ir_stmt *statement);
bool cn_ir_expr_list_push(cn_allocator *allocator, cn_ir_expr_list *list, cn_ir_expr *expression);
bool cn_ir_field_init_list_push(cn_allocator *allocator, cn_ir_field_init_list *list, cn_ir_field_init field_init);
bool cn_ir_struct_field_list_push(cn_allocator *allocator, cn_ir_struct_field_list *list, cn_ir_struct_field field);

void cn_ir_program_destroy(cn_allocator *allocator, cn_ir_program *program);
void cn_ir_program_dump(const cn_ir_program *program, FILE *stream);

bool cn_ir_lower_project(cn_allocator *allocator, const cn_project *project, cn_diag_bag *diagnostics, cn_ir_program **out_program);
void cn_ir_optimize_program(cn_allocator *allocator, cn_ir_program *program);

#endif
