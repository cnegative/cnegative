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
           cn_sv_eq_cstr(module_name, "std.strings") ||
           cn_sv_eq_cstr(module_name, "std.parse") ||
           cn_sv_eq_cstr(module_name, "std.fs") ||
           cn_sv_eq_cstr(module_name, "std.io") ||
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
