#ifndef CNEGATIVE_AST_H
#define CNEGATIVE_AST_H

#include "cnegative/common.h"
#include "cnegative/memory.h"

typedef enum cn_type_kind {
    CN_TYPE_INT,
    CN_TYPE_BOOL,
    CN_TYPE_STR,
    CN_TYPE_VOID,
    CN_TYPE_RESULT,
    CN_TYPE_PTR,
    CN_TYPE_ARRAY,
    CN_TYPE_NAMED,
    CN_TYPE_UNKNOWN
} cn_type_kind;

typedef struct cn_type_ref {
    cn_type_kind kind;
    cn_strview module_name;
    cn_strview name;
    struct cn_type_ref *inner;
    size_t array_size;
    size_t offset;
} cn_type_ref;

typedef struct cn_expr cn_expr;
typedef struct cn_stmt cn_stmt;
typedef struct cn_block cn_block;

typedef struct cn_field_init {
    cn_strview name;
    cn_expr *value;
    size_t offset;
} cn_field_init;

typedef struct cn_field_init_list {
    cn_field_init *items;
    size_t count;
    size_t capacity;
} cn_field_init_list;

typedef struct cn_struct_field {
    cn_strview name;
    cn_type_ref *type;
    size_t offset;
} cn_struct_field;

typedef struct cn_struct_field_list {
    cn_struct_field *items;
    size_t count;
    size_t capacity;
} cn_struct_field_list;

typedef struct cn_struct_decl {
    bool is_public;
    cn_strview name;
    cn_struct_field_list fields;
    size_t offset;
} cn_struct_decl;

typedef struct cn_import_decl {
    cn_strview module_name;
    cn_strview alias;
    bool has_alias;
    size_t offset;
} cn_import_decl;

typedef enum cn_unary_op {
    CN_UNARY_NEGATE,
    CN_UNARY_NOT
} cn_unary_op;

typedef enum cn_binary_op {
    CN_BINARY_ADD,
    CN_BINARY_SUB,
    CN_BINARY_MUL,
    CN_BINARY_DIV,
    CN_BINARY_EQUAL,
    CN_BINARY_NOT_EQUAL,
    CN_BINARY_LESS,
    CN_BINARY_LESS_EQUAL,
    CN_BINARY_GREATER,
    CN_BINARY_GREATER_EQUAL,
    CN_BINARY_AND,
    CN_BINARY_OR
} cn_binary_op;

typedef enum cn_expr_kind {
    CN_EXPR_INT,
    CN_EXPR_BOOL,
    CN_EXPR_STRING,
    CN_EXPR_NAME,
    CN_EXPR_UNARY,
    CN_EXPR_BINARY,
    CN_EXPR_CALL,
    CN_EXPR_ARRAY_LITERAL,
    CN_EXPR_INDEX,
    CN_EXPR_FIELD,
    CN_EXPR_STRUCT_LITERAL,
    CN_EXPR_OK,
    CN_EXPR_ERR,
    CN_EXPR_ALLOC,
    CN_EXPR_ADDR,
    CN_EXPR_DEREF
} cn_expr_kind;

typedef struct cn_expr_list {
    cn_expr **items;
    size_t count;
    size_t capacity;
} cn_expr_list;

struct cn_expr {
    cn_expr_kind kind;
    size_t offset;
    union {
        int64_t int_value;
        bool bool_value;
        cn_strview string_value;
        cn_strview name;
        struct {
            cn_unary_op op;
            cn_expr *operand;
        } unary;
        struct {
            cn_binary_op op;
            cn_expr *left;
            cn_expr *right;
        } binary;
        struct {
            cn_expr *callee;
            cn_expr_list arguments;
        } call;
        struct {
            cn_expr_list items;
        } array_literal;
        struct {
            cn_expr *base;
            cn_expr *index;
        } index;
        struct {
            cn_expr *base;
            cn_strview field_name;
        } field;
        struct {
            cn_strview module_name;
            cn_strview type_name;
            cn_field_init_list fields;
        } struct_literal;
        struct {
            cn_expr *value;
        } ok_expr;
        struct {
            cn_type_ref *type;
        } alloc_expr;
        struct {
            cn_expr *target;
        } addr_expr;
        struct {
            cn_expr *target;
        } deref_expr;
    } data;
};

typedef enum cn_stmt_kind {
    CN_STMT_LET,
    CN_STMT_ASSIGN,
    CN_STMT_RETURN,
    CN_STMT_EXPR,
    CN_STMT_IF,
    CN_STMT_WHILE,
    CN_STMT_LOOP,
    CN_STMT_FOR,
    CN_STMT_FREE
} cn_stmt_kind;

typedef struct cn_stmt_list {
    cn_stmt **items;
    size_t count;
    size_t capacity;
} cn_stmt_list;

struct cn_block {
    cn_stmt_list statements;
    size_t offset;
};

struct cn_stmt {
    cn_stmt_kind kind;
    size_t offset;
    union {
        struct {
            cn_strview name;
            bool is_mutable;
            cn_type_ref *type;
            cn_expr *initializer;
        } let_stmt;
        struct {
            cn_expr *target;
            cn_expr *value;
        } assign_stmt;
        struct {
            cn_expr *value;
        } return_stmt;
        struct {
            cn_expr *value;
        } expr_stmt;
        struct {
            cn_expr *condition;
            cn_block *then_block;
            cn_block *else_block;
        } if_stmt;
        struct {
            cn_expr *condition;
            cn_block *body;
        } while_stmt;
        struct {
            cn_block *body;
        } loop_stmt;
        struct {
            cn_strview name;
            cn_type_ref *type;
            cn_expr *start;
            cn_expr *end;
            cn_block *body;
        } for_stmt;
        struct {
            cn_expr *value;
        } free_stmt;
    } data;
};

typedef struct cn_param {
    cn_strview name;
    cn_type_ref *type;
    size_t offset;
} cn_param;

typedef struct cn_param_list {
    cn_param *items;
    size_t count;
    size_t capacity;
} cn_param_list;

typedef struct cn_function {
    bool is_public;
    cn_strview name;
    cn_type_ref *return_type;
    cn_param_list parameters;
    cn_block *body;
    size_t offset;
} cn_function;

typedef struct cn_program {
    cn_allocator *allocator;
    cn_import_decl *imports;
    size_t import_count;
    size_t import_capacity;
    cn_struct_decl **structs;
    size_t struct_count;
    size_t struct_capacity;
    cn_function **functions;
    size_t function_count;
    size_t function_capacity;
} cn_program;

cn_program *cn_program_create(cn_allocator *allocator);
void cn_program_destroy(cn_allocator *allocator, cn_program *program);
cn_type_ref *cn_type_create(cn_allocator *allocator, cn_type_kind kind, cn_strview name, cn_type_ref *inner, size_t offset);
cn_expr *cn_expr_create(cn_allocator *allocator, cn_expr_kind kind, size_t offset);
cn_stmt *cn_stmt_create(cn_allocator *allocator, cn_stmt_kind kind, size_t offset);
cn_block *cn_block_create(cn_allocator *allocator, size_t offset);
cn_function *cn_function_create(cn_allocator *allocator, size_t offset);
cn_struct_decl *cn_struct_decl_create(cn_allocator *allocator, size_t offset);

bool cn_program_push_import(cn_program *program, cn_import_decl import_decl);
bool cn_program_push_struct(cn_program *program, cn_struct_decl *struct_decl);
bool cn_program_push_function(cn_program *program, cn_function *function);
bool cn_param_list_push(cn_allocator *allocator, cn_param_list *list, cn_param param);
bool cn_stmt_list_push(cn_allocator *allocator, cn_stmt_list *list, cn_stmt *statement);
bool cn_expr_list_push(cn_allocator *allocator, cn_expr_list *list, cn_expr *expression);
bool cn_field_init_list_push(cn_allocator *allocator, cn_field_init_list *list, cn_field_init field_init);
bool cn_struct_field_list_push(cn_allocator *allocator, cn_struct_field_list *list, cn_struct_field field);

bool cn_type_equal(const cn_type_ref *left, const cn_type_ref *right);
void cn_type_describe(const cn_type_ref *type, char *buffer, size_t buffer_size);

#endif
