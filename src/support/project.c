#include "cnegative/project.h"

#include "cnegative/lexer.h"
#include "cnegative/parser.h"
#include "cnegative/token.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define CN_GETCWD _getcwd
#define CN_PATH_SEPARATOR '\\'
#else
#include <unistd.h>
#define CN_GETCWD getcwd
#define CN_PATH_SEPARATOR '/'
#endif

static FILE *cn_project_open_file(const char *path, const char *mode) {
#ifdef _WIN32
    FILE *file = NULL;
    errno_t error = fopen_s(&file, path, mode);
    if (error != 0) {
        return NULL;
    }
    return file;
#else
    return fopen(path, mode);
#endif
}

static cn_type_ref *cn_builtin_primitive_type(cn_allocator *allocator, cn_type_kind kind) {
    switch (kind) {
    case CN_TYPE_INT:
        return cn_type_create(allocator, CN_TYPE_INT, cn_sv_from_cstr("int"), NULL, 0);
    case CN_TYPE_U8:
        return cn_type_create(allocator, CN_TYPE_U8, cn_sv_from_cstr("u8"), NULL, 0);
    case CN_TYPE_BOOL:
        return cn_type_create(allocator, CN_TYPE_BOOL, cn_sv_from_cstr("bool"), NULL, 0);
    case CN_TYPE_STR:
        return cn_type_create(allocator, CN_TYPE_STR, cn_sv_from_cstr("str"), NULL, 0);
    case CN_TYPE_VOID:
        return cn_type_create(allocator, CN_TYPE_VOID, cn_sv_from_cstr("void"), NULL, 0);
    default:
        return cn_type_create(allocator, CN_TYPE_UNKNOWN, cn_sv_from_cstr("<unknown>"), NULL, 0);
    }
}

static cn_type_ref *cn_builtin_result_type(cn_allocator *allocator, cn_type_kind inner_kind) {
    return cn_type_create(
        allocator,
        CN_TYPE_RESULT,
        cn_sv_from_cstr("result"),
        cn_builtin_primitive_type(allocator, inner_kind),
        0
    );
}

static cn_type_ref *cn_builtin_named_type(cn_allocator *allocator, const char *module_name, const char *name) {
    cn_type_ref *type = cn_type_create(allocator, CN_TYPE_NAMED, cn_sv_from_cstr(name), NULL, 0);
    type->module_name = cn_sv_from_cstr(module_name);
    return type;
}

static cn_type_ref *cn_builtin_result_wrapped_type(cn_allocator *allocator, cn_type_ref *inner) {
    return cn_type_create(
        allocator,
        CN_TYPE_RESULT,
        cn_sv_from_cstr("result"),
        inner,
        0
    );
}

static cn_type_ref *cn_builtin_ptr_type(cn_allocator *allocator, cn_type_ref *inner) {
    return cn_type_create(
        allocator,
        CN_TYPE_PTR,
        cn_sv_from_cstr("ptr"),
        inner,
        0
    );
}

static cn_type_ref *cn_builtin_slice_type(cn_allocator *allocator, cn_type_ref *inner) {
    return cn_type_create(
        allocator,
        CN_TYPE_SLICE,
        cn_sv_from_cstr("slice"),
        inner,
        0
    );
}

static cn_function *cn_builtin_function_create(
    cn_allocator *allocator,
    const char *name,
    cn_type_ref *return_type
) {
    cn_function *function = cn_function_create(allocator, 0);
    function->is_public = true;
    function->is_builtin = true;
    function->name = cn_sv_from_cstr(name);
    function->return_type = return_type;
    return function;
}

static void cn_builtin_push_param(cn_allocator *allocator, cn_function *function, const char *name, cn_type_ref *type) {
    cn_param param;
    param.name = cn_sv_from_cstr(name);
    param.type = type;
    param.offset = 0;
    cn_param_list_push(allocator, &function->parameters, param);
}

static cn_struct_decl *cn_builtin_struct_create(cn_allocator *allocator, const char *name) {
    cn_struct_decl *struct_decl = cn_struct_decl_create(allocator, 0);
    struct_decl->is_public = true;
    struct_decl->name = cn_sv_from_cstr(name);
    return struct_decl;
}

static cn_expr *cn_builtin_int_expr(cn_allocator *allocator, int64_t value) {
    cn_expr *expression = cn_expr_create(allocator, CN_EXPR_INT, 0);
    expression->data.int_value = value;
    return expression;
}

static cn_const_decl *cn_builtin_const_int_create(cn_allocator *allocator, const char *name, int64_t value) {
    cn_const_decl *const_decl = cn_const_decl_create(allocator, 0);
    const_decl->is_public = true;
    const_decl->name = cn_sv_from_cstr(name);
    const_decl->type = cn_builtin_primitive_type(allocator, CN_TYPE_INT);
    const_decl->initializer = cn_builtin_int_expr(allocator, value);
    return const_decl;
}

static void cn_builtin_push_struct_field(
    cn_allocator *allocator,
    cn_struct_decl *struct_decl,
    const char *name,
    cn_type_ref *type
) {
    cn_struct_field field;
    field.name = cn_sv_from_cstr(name);
    field.type = type;
    field.offset = 0;
    cn_struct_field_list_push(allocator, &struct_decl->fields, field);
}

static void cn_builtin_source_init(cn_allocator *allocator, cn_source *source, const char *path) {
    source->path = CN_STRDUP(allocator, path);
    source->text = CN_STRDUP(allocator, "");
    source->length = 0;
    source->line_offsets = CN_CALLOC(allocator, size_t, 1);
    source->line_offsets[0] = 0;
    source->line_count = 1;
}

static bool cn_is_builtin_stdlib_module_name(cn_strview module_name) {
    return cn_sv_eq_cstr(module_name, "std.math") ||
           cn_sv_eq_cstr(module_name, "std.bytes") ||
           cn_sv_eq_cstr(module_name, "std.lines") ||
           cn_sv_eq_cstr(module_name, "std.strings") ||
           cn_sv_eq_cstr(module_name, "std.text") ||
           cn_sv_eq_cstr(module_name, "std.parse") ||
           cn_sv_eq_cstr(module_name, "std.fs") ||
           cn_sv_eq_cstr(module_name, "std.io") ||
           cn_sv_eq_cstr(module_name, "std.term") ||
           cn_sv_eq_cstr(module_name, "std.time") ||
           cn_sv_eq_cstr(module_name, "std.env") ||
           cn_sv_eq_cstr(module_name, "std.path") ||
           cn_sv_eq_cstr(module_name, "std.net") ||
#if defined(__linux__)
           cn_sv_eq_cstr(module_name, "std.x11") ||
#endif
           cn_sv_eq_cstr(module_name, "std.process");
}

static cn_program *cn_builtin_stdlib_program(cn_allocator *allocator, const char *module_name) {
    cn_program *program = cn_program_create(allocator);

    if (strcmp(module_name, "std.math") == 0) {
        cn_function *abs = cn_builtin_function_create(allocator, "abs", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, abs, "value", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, abs);

        cn_function *sign = cn_builtin_function_create(allocator, "sign", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, sign, "value", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, sign);

        cn_function *square = cn_builtin_function_create(allocator, "square", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, square, "value", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, square);

        cn_function *cube = cn_builtin_function_create(allocator, "cube", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, cube, "value", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, cube);

        cn_function *is_even = cn_builtin_function_create(allocator, "is_even", cn_builtin_primitive_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(allocator, is_even, "value", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, is_even);

        cn_function *is_odd = cn_builtin_function_create(allocator, "is_odd", cn_builtin_primitive_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(allocator, is_odd, "value", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, is_odd);

        cn_function *min = cn_builtin_function_create(allocator, "min", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, min, "left", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, min, "right", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, min);

        cn_function *max = cn_builtin_function_create(allocator, "max", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, max, "left", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, max, "right", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, max);

        cn_function *clamp = cn_builtin_function_create(allocator, "clamp", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, clamp, "value", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, clamp, "lower", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, clamp, "upper", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, clamp);

        cn_function *gcd = cn_builtin_function_create(allocator, "gcd", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, gcd, "left", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, gcd, "right", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, gcd);

        cn_function *lcm = cn_builtin_function_create(allocator, "lcm", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, lcm, "left", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, lcm, "right", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, lcm);

        cn_function *distance = cn_builtin_function_create(allocator, "distance", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, distance, "left", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, distance, "right", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, distance);

        cn_function *between = cn_builtin_function_create(allocator, "between", cn_builtin_primitive_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(allocator, between, "value", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, between, "lower", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, between, "upper", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, between);
        return program;
    }

    if (strcmp(module_name, "std.bytes") == 0) {
        cn_struct_decl *buffer = cn_builtin_struct_create(allocator, "Buffer");
        cn_builtin_push_struct_field(allocator, buffer, "data", cn_builtin_ptr_type(allocator, cn_builtin_primitive_type(allocator, CN_TYPE_U8)));
        cn_builtin_push_struct_field(allocator, buffer, "length", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_struct_field(allocator, buffer, "capacity", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_struct(program, buffer);

        cn_function *new_buffer = cn_builtin_function_create(
            allocator,
            "new",
            cn_builtin_result_wrapped_type(
                allocator,
                cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.bytes", "Buffer"))
            )
        );
        cn_program_push_function(program, new_buffer);

        cn_function *with_capacity = cn_builtin_function_create(
            allocator,
            "with_capacity",
            cn_builtin_result_wrapped_type(
                allocator,
                cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.bytes", "Buffer"))
            )
        );
        cn_builtin_push_param(allocator, with_capacity, "capacity", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, with_capacity);

        cn_function *free_buffer = cn_builtin_function_create(allocator, "release", cn_builtin_result_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(
            allocator,
            free_buffer,
            "buffer",
            cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.bytes", "Buffer"))
        );
        cn_program_push_function(program, free_buffer);

        cn_function *clear = cn_builtin_function_create(allocator, "clear", cn_builtin_result_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(
            allocator,
            clear,
            "buffer",
            cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.bytes", "Buffer"))
        );
        cn_program_push_function(program, clear);

        cn_function *length = cn_builtin_function_create(allocator, "length", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(
            allocator,
            length,
            "buffer",
            cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.bytes", "Buffer"))
        );
        cn_program_push_function(program, length);

        cn_function *capacity = cn_builtin_function_create(allocator, "capacity", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(
            allocator,
            capacity,
            "buffer",
            cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.bytes", "Buffer"))
        );
        cn_program_push_function(program, capacity);

        cn_function *push = cn_builtin_function_create(allocator, "push", cn_builtin_result_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(
            allocator,
            push,
            "buffer",
            cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.bytes", "Buffer"))
        );
        cn_builtin_push_param(allocator, push, "value", cn_builtin_primitive_type(allocator, CN_TYPE_U8));
        cn_program_push_function(program, push);

        cn_function *append = cn_builtin_function_create(allocator, "append", cn_builtin_result_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(
            allocator,
            append,
            "buffer",
            cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.bytes", "Buffer"))
        );
        cn_builtin_push_param(
            allocator,
            append,
            "values",
            cn_builtin_slice_type(allocator, cn_builtin_primitive_type(allocator, CN_TYPE_U8))
        );
        cn_program_push_function(program, append);

        cn_function *get = cn_builtin_function_create(allocator, "get", cn_builtin_result_type(allocator, CN_TYPE_U8));
        cn_builtin_push_param(
            allocator,
            get,
            "buffer",
            cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.bytes", "Buffer"))
        );
        cn_builtin_push_param(allocator, get, "index", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, get);

        cn_function *set = cn_builtin_function_create(allocator, "set", cn_builtin_result_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(
            allocator,
            set,
            "buffer",
            cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.bytes", "Buffer"))
        );
        cn_builtin_push_param(allocator, set, "index", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, set, "value", cn_builtin_primitive_type(allocator, CN_TYPE_U8));
        cn_program_push_function(program, set);

        cn_function *slice = cn_builtin_function_create(
            allocator,
            "view",
            cn_builtin_slice_type(allocator, cn_builtin_primitive_type(allocator, CN_TYPE_U8))
        );
        cn_builtin_push_param(
            allocator,
            slice,
            "buffer",
            cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.bytes", "Buffer"))
        );
        cn_program_push_function(program, slice);
        return program;
    }

    if (strcmp(module_name, "std.lines") == 0) {
        cn_struct_decl *buffer = cn_builtin_struct_create(allocator, "Buffer");
        cn_builtin_push_struct_field(allocator, buffer, "data", cn_builtin_ptr_type(allocator, cn_builtin_primitive_type(allocator, CN_TYPE_STR)));
        cn_builtin_push_struct_field(allocator, buffer, "length", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_struct_field(allocator, buffer, "capacity", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_struct(program, buffer);

        cn_function *new_buffer = cn_builtin_function_create(
            allocator,
            "new",
            cn_builtin_result_wrapped_type(
                allocator,
                cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.lines", "Buffer"))
            )
        );
        cn_program_push_function(program, new_buffer);

        cn_function *with_capacity = cn_builtin_function_create(
            allocator,
            "with_capacity",
            cn_builtin_result_wrapped_type(
                allocator,
                cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.lines", "Buffer"))
            )
        );
        cn_builtin_push_param(allocator, with_capacity, "capacity", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, with_capacity);

        cn_function *release = cn_builtin_function_create(allocator, "release", cn_builtin_result_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(
            allocator,
            release,
            "buffer",
            cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.lines", "Buffer"))
        );
        cn_program_push_function(program, release);

        cn_function *clear = cn_builtin_function_create(allocator, "clear", cn_builtin_result_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(
            allocator,
            clear,
            "buffer",
            cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.lines", "Buffer"))
        );
        cn_program_push_function(program, clear);

        cn_function *length = cn_builtin_function_create(allocator, "length", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(
            allocator,
            length,
            "buffer",
            cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.lines", "Buffer"))
        );
        cn_program_push_function(program, length);

        cn_function *capacity = cn_builtin_function_create(allocator, "capacity", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(
            allocator,
            capacity,
            "buffer",
            cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.lines", "Buffer"))
        );
        cn_program_push_function(program, capacity);

        cn_function *get = cn_builtin_function_create(allocator, "get", cn_builtin_result_type(allocator, CN_TYPE_STR));
        cn_builtin_push_param(
            allocator,
            get,
            "buffer",
            cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.lines", "Buffer"))
        );
        cn_builtin_push_param(allocator, get, "index", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, get);

        cn_function *set = cn_builtin_function_create(allocator, "set", cn_builtin_result_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(
            allocator,
            set,
            "buffer",
            cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.lines", "Buffer"))
        );
        cn_builtin_push_param(allocator, set, "index", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, set, "value", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, set);

        cn_function *push = cn_builtin_function_create(allocator, "push", cn_builtin_result_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(
            allocator,
            push,
            "buffer",
            cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.lines", "Buffer"))
        );
        cn_builtin_push_param(allocator, push, "value", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, push);

        cn_function *insert = cn_builtin_function_create(allocator, "insert", cn_builtin_result_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(
            allocator,
            insert,
            "buffer",
            cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.lines", "Buffer"))
        );
        cn_builtin_push_param(allocator, insert, "index", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, insert, "value", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, insert);

        cn_function *remove = cn_builtin_function_create(allocator, "remove", cn_builtin_result_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(
            allocator,
            remove,
            "buffer",
            cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.lines", "Buffer"))
        );
        cn_builtin_push_param(allocator, remove, "index", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, remove);

        return program;
    }

    if (strcmp(module_name, "std.strings") == 0) {
        cn_function *length = cn_builtin_function_create(allocator, "len", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, length, "value", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, length);

        cn_function *copy = cn_builtin_function_create(allocator, "copy", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_builtin_push_param(allocator, copy, "value", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, copy);

        cn_function *concat = cn_builtin_function_create(allocator, "concat", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_builtin_push_param(allocator, concat, "left", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_builtin_push_param(allocator, concat, "right", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, concat);

        cn_function *eq = cn_builtin_function_create(allocator, "eq", cn_builtin_primitive_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(allocator, eq, "left", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_builtin_push_param(allocator, eq, "right", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, eq);

        cn_function *starts_with = cn_builtin_function_create(allocator, "starts_with", cn_builtin_primitive_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(allocator, starts_with, "value", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_builtin_push_param(allocator, starts_with, "prefix", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, starts_with);

        cn_function *ends_with = cn_builtin_function_create(allocator, "ends_with", cn_builtin_primitive_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(allocator, ends_with, "value", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_builtin_push_param(allocator, ends_with, "suffix", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, ends_with);
        return program;
    }

    if (strcmp(module_name, "std.text") == 0) {
        cn_struct_decl *builder = cn_builtin_struct_create(allocator, "Builder");
        cn_builtin_push_struct_field(allocator, builder, "data", cn_builtin_ptr_type(allocator, cn_builtin_primitive_type(allocator, CN_TYPE_U8)));
        cn_builtin_push_struct_field(allocator, builder, "length", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_struct_field(allocator, builder, "capacity", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_struct(program, builder);

        cn_function *new_builder = cn_builtin_function_create(
            allocator,
            "new",
            cn_builtin_result_wrapped_type(
                allocator,
                cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.text", "Builder"))
            )
        );
        cn_program_push_function(program, new_builder);

        cn_function *with_capacity = cn_builtin_function_create(
            allocator,
            "with_capacity",
            cn_builtin_result_wrapped_type(
                allocator,
                cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.text", "Builder"))
            )
        );
        cn_builtin_push_param(allocator, with_capacity, "capacity", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, with_capacity);

        cn_function *free_builder = cn_builtin_function_create(allocator, "release", cn_builtin_result_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(
            allocator,
            free_builder,
            "builder",
            cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.text", "Builder"))
        );
        cn_program_push_function(program, free_builder);

        cn_function *clear = cn_builtin_function_create(allocator, "clear", cn_builtin_result_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(
            allocator,
            clear,
            "builder",
            cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.text", "Builder"))
        );
        cn_program_push_function(program, clear);

        cn_function *length = cn_builtin_function_create(allocator, "length", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(
            allocator,
            length,
            "builder",
            cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.text", "Builder"))
        );
        cn_program_push_function(program, length);

        cn_function *capacity = cn_builtin_function_create(allocator, "capacity", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(
            allocator,
            capacity,
            "builder",
            cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.text", "Builder"))
        );
        cn_program_push_function(program, capacity);

        cn_function *append = cn_builtin_function_create(allocator, "append", cn_builtin_result_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(
            allocator,
            append,
            "builder",
            cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.text", "Builder"))
        );
        cn_builtin_push_param(allocator, append, "value", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, append);

        cn_function *push_byte = cn_builtin_function_create(allocator, "push_byte", cn_builtin_result_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(
            allocator,
            push_byte,
            "builder",
            cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.text", "Builder"))
        );
        cn_builtin_push_param(allocator, push_byte, "value", cn_builtin_primitive_type(allocator, CN_TYPE_U8));
        cn_program_push_function(program, push_byte);

        cn_function *build = cn_builtin_function_create(allocator, "build", cn_builtin_result_type(allocator, CN_TYPE_STR));
        cn_builtin_push_param(
            allocator,
            build,
            "builder",
            cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.text", "Builder"))
        );
        cn_program_push_function(program, build);

        cn_function *slice = cn_builtin_function_create(
            allocator,
            "view",
            cn_builtin_slice_type(allocator, cn_builtin_primitive_type(allocator, CN_TYPE_U8))
        );
        cn_builtin_push_param(
            allocator,
            slice,
            "builder",
            cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.text", "Builder"))
        );
        cn_program_push_function(program, slice);
        return program;
    }

    if (strcmp(module_name, "std.parse") == 0) {
        cn_function *to_int = cn_builtin_function_create(allocator, "to_int", cn_builtin_result_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, to_int, "value", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, to_int);

        cn_function *to_bool = cn_builtin_function_create(allocator, "to_bool", cn_builtin_result_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(allocator, to_bool, "value", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, to_bool);
        return program;
    }

    if (strcmp(module_name, "std.fs") == 0) {
        cn_function *exists = cn_builtin_function_create(allocator, "exists", cn_builtin_primitive_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(allocator, exists, "path", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, exists);

        cn_function *cwd = cn_builtin_function_create(allocator, "cwd", cn_builtin_result_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, cwd);

        cn_function *create_dir = cn_builtin_function_create(allocator, "create_dir", cn_builtin_result_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(allocator, create_dir, "path", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, create_dir);

        cn_function *remove_dir = cn_builtin_function_create(allocator, "remove_dir", cn_builtin_result_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(allocator, remove_dir, "path", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, remove_dir);

        cn_function *read_text = cn_builtin_function_create(allocator, "read_text", cn_builtin_result_type(allocator, CN_TYPE_STR));
        cn_builtin_push_param(allocator, read_text, "path", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, read_text);

        cn_function *file_size = cn_builtin_function_create(allocator, "file_size", cn_builtin_result_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, file_size, "path", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, file_size);

        cn_function *copy = cn_builtin_function_create(allocator, "copy", cn_builtin_result_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(allocator, copy, "from_path", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_builtin_push_param(allocator, copy, "to_path", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, copy);

        cn_function *write_text = cn_builtin_function_create(allocator, "write_text", cn_builtin_result_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(allocator, write_text, "path", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_builtin_push_param(allocator, write_text, "data", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, write_text);

        cn_function *append_text = cn_builtin_function_create(allocator, "append_text", cn_builtin_result_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(allocator, append_text, "path", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_builtin_push_param(allocator, append_text, "data", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, append_text);

        cn_function *rename = cn_builtin_function_create(allocator, "rename", cn_builtin_result_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(allocator, rename, "from_path", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_builtin_push_param(allocator, rename, "to_path", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, rename);

        cn_function *move = cn_builtin_function_create(allocator, "move", cn_builtin_result_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(allocator, move, "from_path", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_builtin_push_param(allocator, move, "to_path", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, move);

        cn_function *remove = cn_builtin_function_create(allocator, "remove", cn_builtin_result_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(allocator, remove, "path", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, remove);
        return program;
    }

    if (strcmp(module_name, "std.io") == 0) {
        cn_function *write = cn_builtin_function_create(allocator, "write", cn_builtin_primitive_type(allocator, CN_TYPE_VOID));
        cn_builtin_push_param(allocator, write, "value", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, write);

        cn_function *write_line = cn_builtin_function_create(allocator, "write_line", cn_builtin_primitive_type(allocator, CN_TYPE_VOID));
        cn_builtin_push_param(allocator, write_line, "value", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, write_line);

        cn_function *read_line = cn_builtin_function_create(allocator, "read_line", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, read_line);
        return program;
    }

    if (strcmp(module_name, "std.term") == 0) {
        cn_struct_decl *cell = cn_builtin_struct_create(allocator, "Cell");
        cn_builtin_push_struct_field(allocator, cell, "code", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_struct_field(allocator, cell, "fg", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_struct_field(allocator, cell, "bg", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_struct_field(allocator, cell, "attrs", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_struct_field(allocator, cell, "wide", cn_builtin_primitive_type(allocator, CN_TYPE_BOOL));
        cn_program_push_struct(program, cell);

        cn_struct_decl *buffer = cn_builtin_struct_create(allocator, "Buffer");
        cn_builtin_push_struct_field(allocator, buffer, "width", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_struct_field(allocator, buffer, "height", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_struct_field(
            allocator,
            buffer,
            "cells",
            cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.term", "Cell"))
        );
        cn_program_push_struct(program, buffer);

        cn_struct_decl *clip = cn_builtin_struct_create(allocator, "Clip");
        cn_builtin_push_struct_field(allocator, clip, "row", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_struct_field(allocator, clip, "column", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_struct_field(allocator, clip, "height", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_struct_field(allocator, clip, "width", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_struct(program, clip);

        cn_struct_decl *event = cn_builtin_struct_create(allocator, "Event");
        cn_builtin_push_struct_field(allocator, event, "kind", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_struct_field(allocator, event, "code", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_struct_field(allocator, event, "modifiers", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_struct_field(allocator, event, "row", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_struct_field(allocator, event, "column", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_struct(program, event);

        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "EVENT_KEY", 1));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "EVENT_MOUSE", 2));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "EVENT_RESIZE", 3));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "EVENT_PASTE", 4));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "COLOR_DEFAULT", -1));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "COLOR_BLACK", 0));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "COLOR_RED", 1));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "COLOR_GREEN", 2));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "COLOR_YELLOW", 3));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "COLOR_BLUE", 4));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "COLOR_MAGENTA", 5));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "COLOR_CYAN", 6));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "COLOR_WHITE", 7));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "COLOR_BRIGHT_BLACK", 8));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "COLOR_BRIGHT_RED", 9));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "COLOR_BRIGHT_GREEN", 10));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "COLOR_BRIGHT_YELLOW", 11));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "COLOR_BRIGHT_BLUE", 12));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "COLOR_BRIGHT_MAGENTA", 13));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "COLOR_BRIGHT_CYAN", 14));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "COLOR_BRIGHT_WHITE", 15));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "ATTR_NONE", 0));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "ATTR_BOLD", 1));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "ATTR_DIM", 2));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "ATTR_ITALIC", 4));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "ATTR_UNDERLINE", 8));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "ATTR_BLINK", 16));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "ATTR_REVERSE", 32));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "ATTR_STRIKETHROUGH", 64));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "MOD_SHIFT", 1));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "MOD_ALT", 2));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "MOD_CTRL", 4));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "MOUSE_LEFT", 1));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "MOUSE_MIDDLE", 2));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "MOUSE_RIGHT", 3));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "MOUSE_RELEASE", 4));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "MOUSE_MOVE", 5));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "MOUSE_SCROLL_UP", 6));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "MOUSE_SCROLL_DOWN", 7));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "KEY_UNKNOWN", 0));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "KEY_ESCAPE", 256));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "KEY_ENTER", 257));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "KEY_TAB", 258));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "KEY_BACKSPACE", 259));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "KEY_UP", 260));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "KEY_DOWN", 261));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "KEY_LEFT", 262));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "KEY_RIGHT", 263));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "KEY_HOME", 264));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "KEY_END", 265));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "KEY_PAGE_UP", 266));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "KEY_PAGE_DOWN", 267));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "KEY_INSERT", 268));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "KEY_DELETE", 269));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "KEY_F1", 290));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "KEY_F2", 291));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "KEY_F3", 292));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "KEY_F4", 293));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "KEY_F5", 294));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "KEY_F6", 295));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "KEY_F7", 296));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "KEY_F8", 297));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "KEY_F9", 298));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "KEY_F10", 299));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "KEY_F11", 300));
        cn_program_push_const(program, cn_builtin_const_int_create(allocator, "KEY_F12", 301));

        cn_function *is_tty = cn_builtin_function_create(allocator, "is_tty", cn_builtin_primitive_type(allocator, CN_TYPE_BOOL));
        cn_program_push_function(program, is_tty);

        cn_function *columns = cn_builtin_function_create(allocator, "columns", cn_builtin_result_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, columns);

        cn_function *rows = cn_builtin_function_create(allocator, "rows", cn_builtin_result_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, rows);

        cn_function *term_name = cn_builtin_function_create(allocator, "term_name", cn_builtin_result_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, term_name);

        cn_function *supports_truecolor = cn_builtin_function_create(allocator, "supports_truecolor", cn_builtin_primitive_type(allocator, CN_TYPE_BOOL));
        cn_program_push_function(program, supports_truecolor);

        cn_function *supports_256color = cn_builtin_function_create(allocator, "supports_256color", cn_builtin_primitive_type(allocator, CN_TYPE_BOOL));
        cn_program_push_function(program, supports_256color);

        cn_function *supports_unicode = cn_builtin_function_create(allocator, "supports_unicode", cn_builtin_primitive_type(allocator, CN_TYPE_BOOL));
        cn_program_push_function(program, supports_unicode);

        cn_function *supports_mouse = cn_builtin_function_create(allocator, "supports_mouse", cn_builtin_primitive_type(allocator, CN_TYPE_BOOL));
        cn_program_push_function(program, supports_mouse);

        cn_function *read_byte = cn_builtin_function_create(allocator, "read_byte", cn_builtin_result_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, read_byte);

        cn_function *read_byte_timeout = cn_builtin_function_create(allocator, "read_byte_timeout", cn_builtin_result_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, read_byte_timeout, "timeout_ms", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, read_byte_timeout);

        cn_function *read_event = cn_builtin_function_create(
            allocator,
            "read_event",
            cn_builtin_result_wrapped_type(allocator, cn_builtin_named_type(allocator, "std.term", "Event"))
        );
        cn_program_push_function(program, read_event);

        cn_function *read_event_timeout = cn_builtin_function_create(
            allocator,
            "read_event_timeout",
            cn_builtin_result_wrapped_type(allocator, cn_builtin_named_type(allocator, "std.term", "Event"))
        );
        cn_builtin_push_param(allocator, read_event_timeout, "timeout_ms", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, read_event_timeout);

        cn_function *read_paste = cn_builtin_function_create(allocator, "read_paste", cn_builtin_result_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, read_paste);

        cn_function *rgb = cn_builtin_function_create(allocator, "rgb", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, rgb, "red", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, rgb, "green", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, rgb, "blue", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, rgb);

        cn_function *codepoint_width = cn_builtin_function_create(allocator, "codepoint_width", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, codepoint_width, "code", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, codepoint_width);

        cn_function *string_width = cn_builtin_function_create(allocator, "string_width", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, string_width, "value", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, string_width);

        cn_function *decode_codepoint = cn_builtin_function_create(allocator, "decode_codepoint", cn_builtin_result_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, decode_codepoint, "value", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_builtin_push_param(allocator, decode_codepoint, "offset", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, decode_codepoint);

        cn_function *next_codepoint_offset = cn_builtin_function_create(allocator, "next_codepoint_offset", cn_builtin_result_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, next_codepoint_offset, "value", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_builtin_push_param(allocator, next_codepoint_offset, "offset", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, next_codepoint_offset);

        cn_function *set_style = cn_builtin_function_create(allocator, "set_style", cn_builtin_primitive_type(allocator, CN_TYPE_VOID));
        cn_builtin_push_param(allocator, set_style, "fg", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, set_style, "bg", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, set_style, "attrs", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, set_style);

        cn_function *reset_style = cn_builtin_function_create(allocator, "reset_style", cn_builtin_primitive_type(allocator, CN_TYPE_VOID));
        cn_program_push_function(program, reset_style);

        cn_function *enable_mouse = cn_builtin_function_create(allocator, "enable_mouse", cn_builtin_primitive_type(allocator, CN_TYPE_VOID));
        cn_program_push_function(program, enable_mouse);

        cn_function *disable_mouse = cn_builtin_function_create(allocator, "disable_mouse", cn_builtin_primitive_type(allocator, CN_TYPE_VOID));
        cn_program_push_function(program, disable_mouse);

        cn_function *enable_bracketed_paste = cn_builtin_function_create(allocator, "enable_bracketed_paste", cn_builtin_primitive_type(allocator, CN_TYPE_VOID));
        cn_program_push_function(program, enable_bracketed_paste);

        cn_function *disable_bracketed_paste = cn_builtin_function_create(allocator, "disable_bracketed_paste", cn_builtin_primitive_type(allocator, CN_TYPE_VOID));
        cn_program_push_function(program, disable_bracketed_paste);

        cn_function *buffer_new = cn_builtin_function_create(
            allocator,
            "buffer_new",
            cn_builtin_result_wrapped_type(
                allocator,
                cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.term", "Buffer"))
            )
        );
        cn_builtin_push_param(allocator, buffer_new, "width", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, buffer_new, "height", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, buffer_new);

        cn_function *buffer_resize = cn_builtin_function_create(allocator, "buffer_resize", cn_builtin_result_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(
            allocator,
            buffer_resize,
            "buffer",
            cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.term", "Buffer"))
        );
        cn_builtin_push_param(allocator, buffer_resize, "width", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, buffer_resize, "height", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, buffer_resize, "fill", cn_builtin_named_type(allocator, "std.term", "Cell"));
        cn_program_push_function(program, buffer_resize);

        cn_function *buffer_free = cn_builtin_function_create(allocator, "buffer_free", cn_builtin_result_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(
            allocator,
            buffer_free,
            "buffer",
            cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.term", "Buffer"))
        );
        cn_program_push_function(program, buffer_free);

        cn_function *buffer_clear = cn_builtin_function_create(allocator, "buffer_clear", cn_builtin_result_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(
            allocator,
            buffer_clear,
            "buffer",
            cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.term", "Buffer"))
        );
        cn_builtin_push_param(allocator, buffer_clear, "fill", cn_builtin_named_type(allocator, "std.term", "Cell"));
        cn_program_push_function(program, buffer_clear);

        cn_function *buffer_set = cn_builtin_function_create(allocator, "buffer_set", cn_builtin_result_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(
            allocator,
            buffer_set,
            "buffer",
            cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.term", "Buffer"))
        );
        cn_builtin_push_param(allocator, buffer_set, "row", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, buffer_set, "column", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, buffer_set, "value", cn_builtin_named_type(allocator, "std.term", "Cell"));
        cn_program_push_function(program, buffer_set);

        cn_function *buffer_get = cn_builtin_function_create(
            allocator,
            "buffer_get",
            cn_builtin_result_wrapped_type(allocator, cn_builtin_named_type(allocator, "std.term", "Cell"))
        );
        cn_builtin_push_param(
            allocator,
            buffer_get,
            "buffer",
            cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.term", "Buffer"))
        );
        cn_builtin_push_param(allocator, buffer_get, "row", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, buffer_get, "column", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, buffer_get);

        cn_function *render_diff = cn_builtin_function_create(allocator, "render_diff", cn_builtin_result_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(
            allocator,
            render_diff,
            "back",
            cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.term", "Buffer"))
        );
        cn_builtin_push_param(
            allocator,
            render_diff,
            "front",
            cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.term", "Buffer"))
        );
        cn_program_push_function(program, render_diff);

        cn_function *render_diff_clip = cn_builtin_function_create(allocator, "render_diff_clip", cn_builtin_result_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(
            allocator,
            render_diff_clip,
            "back",
            cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.term", "Buffer"))
        );
        cn_builtin_push_param(
            allocator,
            render_diff_clip,
            "front",
            cn_builtin_ptr_type(allocator, cn_builtin_named_type(allocator, "std.term", "Buffer"))
        );
        cn_builtin_push_param(allocator, render_diff_clip, "clip", cn_builtin_named_type(allocator, "std.term", "Clip"));
        cn_program_push_function(program, render_diff_clip);

        cn_function *write = cn_builtin_function_create(allocator, "write", cn_builtin_primitive_type(allocator, CN_TYPE_VOID));
        cn_builtin_push_param(allocator, write, "value", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, write);

        cn_function *flush = cn_builtin_function_create(allocator, "flush", cn_builtin_primitive_type(allocator, CN_TYPE_VOID));
        cn_program_push_function(program, flush);

        cn_function *clear = cn_builtin_function_create(allocator, "clear", cn_builtin_primitive_type(allocator, CN_TYPE_VOID));
        cn_program_push_function(program, clear);

        cn_function *clear_line = cn_builtin_function_create(allocator, "clear_line", cn_builtin_primitive_type(allocator, CN_TYPE_VOID));
        cn_program_push_function(program, clear_line);

        cn_function *clear_line_left = cn_builtin_function_create(allocator, "clear_line_left", cn_builtin_primitive_type(allocator, CN_TYPE_VOID));
        cn_program_push_function(program, clear_line_left);

        cn_function *clear_line_right = cn_builtin_function_create(allocator, "clear_line_right", cn_builtin_primitive_type(allocator, CN_TYPE_VOID));
        cn_program_push_function(program, clear_line_right);

        cn_function *move_cursor = cn_builtin_function_create(allocator, "move_cursor", cn_builtin_primitive_type(allocator, CN_TYPE_VOID));
        cn_builtin_push_param(allocator, move_cursor, "row", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, move_cursor, "column", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, move_cursor);

        cn_function *save_cursor = cn_builtin_function_create(allocator, "save_cursor", cn_builtin_primitive_type(allocator, CN_TYPE_VOID));
        cn_program_push_function(program, save_cursor);

        cn_function *restore_cursor = cn_builtin_function_create(allocator, "restore_cursor", cn_builtin_primitive_type(allocator, CN_TYPE_VOID));
        cn_program_push_function(program, restore_cursor);

        cn_function *hide_cursor = cn_builtin_function_create(allocator, "hide_cursor", cn_builtin_primitive_type(allocator, CN_TYPE_VOID));
        cn_program_push_function(program, hide_cursor);

        cn_function *show_cursor = cn_builtin_function_create(allocator, "show_cursor", cn_builtin_primitive_type(allocator, CN_TYPE_VOID));
        cn_program_push_function(program, show_cursor);

        cn_function *enter_alt_screen = cn_builtin_function_create(allocator, "enter_alt_screen", cn_builtin_primitive_type(allocator, CN_TYPE_VOID));
        cn_program_push_function(program, enter_alt_screen);

        cn_function *leave_alt_screen = cn_builtin_function_create(allocator, "leave_alt_screen", cn_builtin_primitive_type(allocator, CN_TYPE_VOID));
        cn_program_push_function(program, leave_alt_screen);

        cn_function *set_scroll_region = cn_builtin_function_create(allocator, "set_scroll_region", cn_builtin_primitive_type(allocator, CN_TYPE_VOID));
        cn_builtin_push_param(allocator, set_scroll_region, "top", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, set_scroll_region, "bottom", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, set_scroll_region);

        cn_function *reset_scroll_region = cn_builtin_function_create(allocator, "reset_scroll_region", cn_builtin_primitive_type(allocator, CN_TYPE_VOID));
        cn_program_push_function(program, reset_scroll_region);

        cn_function *enter_raw_mode = cn_builtin_function_create(allocator, "enter_raw_mode", cn_builtin_result_type(allocator, CN_TYPE_BOOL));
        cn_program_push_function(program, enter_raw_mode);

        cn_function *leave_raw_mode = cn_builtin_function_create(allocator, "leave_raw_mode", cn_builtin_result_type(allocator, CN_TYPE_BOOL));
        cn_program_push_function(program, leave_raw_mode);
        return program;
    }

    if (strcmp(module_name, "std.time") == 0) {
        cn_function *now_ms = cn_builtin_function_create(allocator, "now_ms", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, now_ms);

        cn_function *sleep_ms = cn_builtin_function_create(allocator, "sleep_ms", cn_builtin_primitive_type(allocator, CN_TYPE_VOID));
        cn_builtin_push_param(allocator, sleep_ms, "duration_ms", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, sleep_ms);
        return program;
    }

    if (strcmp(module_name, "std.env") == 0) {
        cn_function *has = cn_builtin_function_create(allocator, "has", cn_builtin_primitive_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(allocator, has, "name", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, has);

        cn_function *get = cn_builtin_function_create(allocator, "get", cn_builtin_result_type(allocator, CN_TYPE_STR));
        cn_builtin_push_param(allocator, get, "name", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, get);
        return program;
    }

    if (strcmp(module_name, "std.path") == 0) {
        cn_function *join = cn_builtin_function_create(allocator, "join", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_builtin_push_param(allocator, join, "left", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_builtin_push_param(allocator, join, "right", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, join);

        cn_function *file_name = cn_builtin_function_create(allocator, "file_name", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_builtin_push_param(allocator, file_name, "path", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, file_name);

        cn_function *stem = cn_builtin_function_create(allocator, "stem", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_builtin_push_param(allocator, stem, "path", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, stem);

        cn_function *extension = cn_builtin_function_create(allocator, "extension", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_builtin_push_param(allocator, extension, "path", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, extension);

        cn_function *is_absolute = cn_builtin_function_create(allocator, "is_absolute", cn_builtin_primitive_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(allocator, is_absolute, "path", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, is_absolute);

        cn_function *parent = cn_builtin_function_create(allocator, "parent", cn_builtin_result_type(allocator, CN_TYPE_STR));
        cn_builtin_push_param(allocator, parent, "path", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, parent);
        return program;
    }

    if (strcmp(module_name, "std.net") == 0) {
        cn_struct_decl *udp_packet = cn_builtin_struct_create(allocator, "UdpPacket");
        cn_builtin_push_struct_field(allocator, udp_packet, "host", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_builtin_push_struct_field(allocator, udp_packet, "port", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_struct_field(allocator, udp_packet, "data", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_struct(program, udp_packet);

        cn_function *is_ipv4 = cn_builtin_function_create(allocator, "is_ipv4", cn_builtin_primitive_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(allocator, is_ipv4, "value", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, is_ipv4);

        cn_function *join_host_port = cn_builtin_function_create(allocator, "join_host_port", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_builtin_push_param(allocator, join_host_port, "host", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_builtin_push_param(allocator, join_host_port, "port", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, join_host_port);

        cn_function *tcp_connect = cn_builtin_function_create(allocator, "tcp_connect", cn_builtin_result_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, tcp_connect, "host", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_builtin_push_param(allocator, tcp_connect, "port", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, tcp_connect);

        cn_function *tcp_listen = cn_builtin_function_create(allocator, "tcp_listen", cn_builtin_result_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, tcp_listen, "host", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_builtin_push_param(allocator, tcp_listen, "port", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, tcp_listen);

        cn_function *accept = cn_builtin_function_create(allocator, "accept", cn_builtin_result_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, accept, "listener", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, accept);

        cn_function *send = cn_builtin_function_create(allocator, "send", cn_builtin_result_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, send, "socket", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, send, "data", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, send);

        cn_function *recv = cn_builtin_function_create(allocator, "recv", cn_builtin_result_type(allocator, CN_TYPE_STR));
        cn_builtin_push_param(allocator, recv, "socket", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, recv, "max_bytes", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, recv);

        cn_function *udp_bind = cn_builtin_function_create(allocator, "udp_bind", cn_builtin_result_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, udp_bind, "host", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_builtin_push_param(allocator, udp_bind, "port", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, udp_bind);

        cn_function *udp_send_to = cn_builtin_function_create(allocator, "udp_send_to", cn_builtin_result_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, udp_send_to, "socket", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, udp_send_to, "host", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_builtin_push_param(allocator, udp_send_to, "port", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, udp_send_to, "data", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, udp_send_to);

        cn_function *udp_recv_from = cn_builtin_function_create(
            allocator,
            "udp_recv_from",
            cn_builtin_result_wrapped_type(allocator, cn_builtin_named_type(allocator, "std.net", "UdpPacket"))
        );
        cn_builtin_push_param(allocator, udp_recv_from, "socket", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, udp_recv_from, "max_bytes", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, udp_recv_from);

        cn_function *close = cn_builtin_function_create(allocator, "close", cn_builtin_result_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(allocator, close, "socket", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, close);
        return program;
    }

#if defined(__linux__)
    if (strcmp(module_name, "std.x11") == 0) {
        cn_function *open_window = cn_builtin_function_create(allocator, "open_window", cn_builtin_result_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, open_window, "title", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_builtin_push_param(allocator, open_window, "width", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_builtin_push_param(allocator, open_window, "height", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, open_window);

        cn_function *pump = cn_builtin_function_create(allocator, "pump", cn_builtin_result_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(allocator, pump, "handle", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, pump);

        cn_function *close = cn_builtin_function_create(allocator, "close", cn_builtin_result_type(allocator, CN_TYPE_BOOL));
        cn_builtin_push_param(allocator, close, "handle", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, close);
        return program;
    }
#endif

    if (strcmp(module_name, "std.process") == 0) {
        cn_function *platform = cn_builtin_function_create(allocator, "platform", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, platform);

        cn_function *arch = cn_builtin_function_create(allocator, "arch", cn_builtin_primitive_type(allocator, CN_TYPE_STR));
        cn_program_push_function(program, arch);

        cn_function *exit = cn_builtin_function_create(allocator, "exit", cn_builtin_primitive_type(allocator, CN_TYPE_VOID));
        cn_builtin_push_param(allocator, exit, "status", cn_builtin_primitive_type(allocator, CN_TYPE_INT));
        cn_program_push_function(program, exit);
        return program;
    }

    cn_program_destroy(allocator, program);
    return NULL;
}

static void cn_project_reserve_modules(cn_project *project, size_t required) {
    if (project->module_capacity >= required) {
        return;
    }

    size_t new_capacity = project->module_capacity == 0 ? 8 : project->module_capacity * 2;
    while (new_capacity < required) {
        new_capacity *= 2;
    }

    project->modules = CN_REALLOC(project->allocator, project->modules, cn_module *, new_capacity);
    project->module_capacity = new_capacity;
}

static cn_module *cn_project_find_module_by_path_mut(cn_project *project, const char *path) {
    for (size_t i = 0; i < project->module_count; ++i) {
        if (strcmp(project->modules[i]->path, path) == 0) {
            return project->modules[i];
        }
    }
    return NULL;
}

const cn_module *cn_project_find_module_by_name(const cn_project *project, cn_strview name) {
    for (size_t i = 0; i < project->module_count; ++i) {
        if (cn_sv_eq_cstr(name, project->modules[i]->name)) {
            return project->modules[i];
        }
    }
    return NULL;
}

static bool cn_path_is_separator(char value) {
    return value == '/' || value == '\\';
}

static const char *cn_path_last_separator(const char *path) {
    const char *result = NULL;
    for (const char *cursor = path; *cursor != '\0'; ++cursor) {
        if (cn_path_is_separator(*cursor)) {
            result = cursor;
        }
    }
    return result;
}

static bool cn_path_is_absolute(const char *path) {
    if (path == NULL || path[0] == '\0') {
        return false;
    }

#ifdef _WIN32
    if (path[1] != '\0' && cn_path_is_separator(path[0]) && cn_path_is_separator(path[1])) {
        return true;
    }

    if (path[0] != '\0' &&
        path[1] != '\0' &&
        path[2] != '\0' &&
        ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
        path[1] == ':' &&
        cn_path_is_separator(path[2])) {
        return true;
    }
#endif

    return cn_path_is_separator(path[0]);
}

static void cn_path_normalize_separators(char *path) {
#ifdef _WIN32
    for (size_t i = 0; path[i] != '\0'; ++i) {
        if (cn_path_is_separator(path[i])) {
            path[i] = CN_PATH_SEPARATOR;
        }
    }
#else
    CN_UNUSED(path);
#endif
}

static char *cn_make_absolute_path(cn_allocator *allocator, const char *path) {
    if (cn_path_is_absolute(path)) {
        char *absolute = CN_STRDUP(allocator, path);
        cn_path_normalize_separators(absolute);
        return absolute;
    }

    char cwd[4096];
    if (CN_GETCWD(cwd, sizeof(cwd)) == NULL) {
        return NULL;
    }

    size_t length = strlen(cwd) + 1 + strlen(path) + 1;
    char *absolute = (char *)cn_alloc_impl(allocator, length, __FILE__, __LINE__);
    snprintf(absolute, length, "%s%c%s", cwd, CN_PATH_SEPARATOR, path);
    cn_path_normalize_separators(absolute);
    return absolute;
}

static char *cn_path_directory(cn_allocator *allocator, const char *path) {
    const char *slash = cn_path_last_separator(path);
    if (slash == NULL) {
        return CN_STRDUP(allocator, ".");
    }

    size_t length = (size_t)(slash - path);
    if (length == 0) {
        length = 1;
    }
    return CN_STRNDUP(allocator, path, length);
}

static char *cn_path_module_name(cn_allocator *allocator, const char *path) {
    const char *name = cn_path_last_separator(path);
    if (name == NULL) {
        name = path;
    } else {
        name += 1;
    }

    size_t length = strlen(name);
    if (length > 5 && strcmp(name + length - 5, ".cneg") == 0) {
        length -= 5;
    } else if (length > 3 && strcmp(name + length - 3, ".cn") == 0) {
        length -= 3;
    }

    return CN_STRNDUP(allocator, name, length);
}

static bool cn_file_exists(const char *path) {
    FILE *file = cn_project_open_file(path, "rb");
    if (file == NULL) {
        return false;
    }

    fclose(file);
    return true;
}

static char *cn_path_build_import_path(
    cn_allocator *allocator,
    const cn_module *module,
    cn_strview import_name,
    const char *extension
) {
    size_t extension_length = strlen(extension);
    size_t length = strlen(module->directory) + 1 + import_name.length + extension_length + 1;
    char *path = (char *)cn_alloc_impl(allocator, length, __FILE__, __LINE__);
    snprintf(path, length, "%s%c%.*s%s", module->directory, CN_PATH_SEPARATOR, (int)import_name.length, import_name.data, extension);
    char *import_start = path + strlen(module->directory) + 1;
    for (size_t index = 0; index < import_name.length; ++index) {
        char *cursor = import_start + index;
        if (*cursor == '.') {
            *cursor = CN_PATH_SEPARATOR;
        }
    }
    return path;
}

static char *cn_path_resolve_import(cn_allocator *allocator, const cn_module *module, cn_strview import_name) {
    char *preferred = cn_path_build_import_path(allocator, module, import_name, ".cneg");
    if (cn_file_exists(preferred)) {
        return preferred;
    }

    char *legacy = cn_path_build_import_path(allocator, module, import_name, ".cn");
    if (cn_file_exists(legacy)) {
        CN_FREE(allocator, preferred);
        return legacy;
    }

    CN_FREE(allocator, legacy);
    return preferred;
}

static cn_module *cn_module_create(cn_allocator *allocator, const char *resolved_path) {
    cn_module *module = CN_ALLOC(allocator, cn_module);
    module->is_builtin_stdlib = false;
    module->name = cn_path_module_name(allocator, resolved_path);
    module->path = CN_STRDUP(allocator, resolved_path);
    module->directory = cn_path_directory(allocator, resolved_path);
    module->source.path = NULL;
    module->source.text = NULL;
    module->source.length = 0;
    module->source.line_offsets = NULL;
    module->source.line_count = 0;
    module->program = NULL;
    module->imports = NULL;
    module->import_count = 0;
    module->loading = false;
    return module;
}

static cn_module *cn_builtin_module_create(cn_allocator *allocator, cn_strview module_name) {
    char path_buffer[128];
    snprintf(path_buffer, sizeof(path_buffer), "<stdlib:%.*s>", (int)module_name.length, module_name.data);

    cn_module *module = CN_ALLOC(allocator, cn_module);
    module->is_builtin_stdlib = true;
    module->name = CN_STRNDUP(allocator, module_name.data, module_name.length);
    module->path = CN_STRDUP(allocator, path_buffer);
    module->directory = CN_STRDUP(allocator, "<stdlib>");
    cn_builtin_source_init(allocator, &module->source, module->path);
    module->program = cn_builtin_stdlib_program(allocator, module->name);
    module->imports = NULL;
    module->import_count = 0;
    module->loading = false;
    return module;
}

static void cn_project_push_module(cn_project *project, cn_module *module) {
    cn_project_reserve_modules(project, project->module_count + 1);
    project->modules[project->module_count++] = module;
}

static cn_module *cn_project_load_builtin_module(cn_project *project, cn_strview module_name) {
    for (size_t i = 0; i < project->module_count; ++i) {
        if (project->modules[i]->is_builtin_stdlib && cn_sv_eq_cstr(module_name, project->modules[i]->name)) {
            return project->modules[i];
        }
    }

    cn_module *module = cn_builtin_module_create(project->allocator, module_name);
    cn_project_push_module(project, module);
    return module;
}

static cn_module *cn_project_load_module(
    cn_project *project,
    const char *path,
    cn_diag_bag *diagnostics,
    const cn_source *import_source,
    size_t import_offset
) {
    char *resolved_path = cn_make_absolute_path(project->allocator, path);
    if (resolved_path == NULL) {
        cn_diag_bag_set_source(diagnostics, import_source);
        cn_diag_emit(
            diagnostics,
            CN_DIAG_ERROR,
            "E3017",
            import_offset,
            "could not resolve module path '%s'",
            path
        );
        return NULL;
    }

    cn_module *existing = cn_project_find_module_by_path_mut(project, resolved_path);
    if (existing != NULL) {
        CN_FREE(project->allocator, resolved_path);
        if (existing->loading) {
            cn_diag_bag_set_source(diagnostics, import_source);
            cn_diag_emit(
                diagnostics,
                CN_DIAG_ERROR,
                "E3018",
                import_offset,
                "cyclic import involving module '%s'",
                existing->name
            );
        }
        return existing;
    }

    cn_module *module = cn_module_create(project->allocator, resolved_path);
    CN_FREE(project->allocator, resolved_path);
    module->loading = true;
    cn_project_push_module(project, module);

    char source_error[256];
    if (!cn_source_load(project->allocator, module->path, &module->source, source_error, sizeof(source_error))) {
        cn_diag_bag_set_source(diagnostics, import_source);
        cn_diag_emit(
            diagnostics,
            CN_DIAG_ERROR,
            "E3017",
            import_offset,
            "%s",
            source_error
        );
        module->loading = false;
        return module;
    }

    cn_diag_bag_set_source(diagnostics, &module->source);

    cn_token_buffer tokens;
    cn_token_buffer_init(&tokens, project->allocator);

    cn_lexer lexer;
    cn_lexer_init(&lexer, &module->source, diagnostics);
    cn_lexer_run(&lexer, &tokens);

    if (!cn_diag_has_error(diagnostics)) {
        cn_parser parser;
        cn_parser_init(&parser, project->allocator, &tokens, diagnostics);
        module->program = cn_parse_program(&parser);
    }

    cn_token_buffer_destroy(&tokens);

    if (module->program != NULL) {
        module->import_count = module->program->import_count;
        if (module->import_count > 0) {
            module->imports = CN_CALLOC(project->allocator, cn_module *, module->import_count);
        }

        for (size_t i = 0; i < module->import_count; ++i) {
            if (cn_is_builtin_stdlib_module_name(module->program->imports[i].module_name)) {
                module->imports[i] = cn_project_load_builtin_module(project, module->program->imports[i].module_name);
                continue;
            }

            char *child_path = cn_path_resolve_import(project->allocator, module, module->program->imports[i].module_name);
            module->imports[i] = cn_project_load_module(
                project,
                child_path,
                diagnostics,
                &module->source,
                module->program->imports[i].offset
            );
            CN_FREE(project->allocator, child_path);
        }
    }

    module->loading = false;
    return module;
}

cn_project *cn_project_load(cn_allocator *allocator, const char *entry_path, cn_diag_bag *diagnostics) {
    cn_project *project = CN_ALLOC(allocator, cn_project);
    project->allocator = allocator;
    project->modules = NULL;
    project->module_count = 0;
    project->module_capacity = 0;
    project->root = NULL;

    project->root = cn_project_load_module(project, entry_path, diagnostics, NULL, 0);
    return project;
}

void cn_project_destroy(cn_allocator *allocator, cn_project *project) {
    if (project == NULL) {
        return;
    }

    for (size_t i = 0; i < project->module_count; ++i) {
        cn_module *module = project->modules[i];
        if (module->program != NULL) {
            cn_program_destroy(allocator, module->program);
        }
        if (module->source.path != NULL) {
            cn_source_destroy(allocator, &module->source);
        }
        CN_FREE(allocator, module->imports);
        CN_FREE(allocator, module->name);
        CN_FREE(allocator, module->path);
        CN_FREE(allocator, module->directory);
        CN_FREE(allocator, module);
    }

    CN_FREE(allocator, project->modules);
    CN_FREE(allocator, project);
}
