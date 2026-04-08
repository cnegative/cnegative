#include "cnegative/backend.h"
#include "cnegative/llvm_runtime.h"

#include <stdio.h>
#include <string.h>

typedef struct cn_llvm_binding {
    cn_strview name;
    const cn_ir_type *type;
    int alloca_id;
    struct cn_llvm_binding *next;
} cn_llvm_binding;

typedef struct cn_llvm_scope {
    cn_llvm_binding *bindings;
    struct cn_llvm_scope *parent;
} cn_llvm_scope;

typedef struct cn_llvm_value {
    const cn_ir_type *type;
    bool is_valid;
    bool is_void;
    bool is_constant;
    union {
        int64_t int_value;
        bool bool_value;
        int reg_id;
    } data;
} cn_llvm_value;

typedef struct cn_llvm_address {
    const cn_ir_type *type;
    bool is_valid;
    int pointer_reg_id;
} cn_llvm_address;

typedef struct cn_llvm_string_entry {
    cn_strview value;
    int global_id;
    struct cn_llvm_string_entry *next;
} cn_llvm_string_entry;

typedef struct cn_llvm_emit_ctx {
    cn_allocator *allocator;
    cn_diag_bag *diagnostics;
    const cn_ir_program *program;
    FILE *stream;
    cn_llvm_string_entry *strings;
    int next_string_id;
} cn_llvm_emit_ctx;

typedef struct cn_llvm_function_ctx {
    cn_llvm_emit_ctx *emit;
    const cn_ir_function *function;
    int next_temp;
    int next_label;
    bool current_block_terminated;
} cn_llvm_function_ctx;

static const cn_ir_type g_llvm_bool_type = {CN_IR_TYPE_BOOL, {NULL, 0}, {NULL, 0}, NULL, 0};
static const cn_ir_type g_llvm_int_type = {CN_IR_TYPE_INT, {NULL, 0}, {NULL, 0}, NULL, 0};
static const cn_ir_type g_llvm_str_type = {CN_IR_TYPE_STR, {NULL, 0}, {NULL, 0}, NULL, 0};

static void cn_llvm_emit_type(FILE *stream, const cn_ir_type *type);
static cn_llvm_value cn_llvm_lower_expression(cn_llvm_function_ctx *ctx, cn_llvm_scope *scope, const cn_ir_expr *expression);
static cn_llvm_address cn_llvm_lower_address(
    cn_llvm_function_ctx *ctx,
    cn_llvm_scope *scope,
    const cn_ir_expr *expression,
    bool require_addressable
);
static bool cn_llvm_validate_expression(cn_llvm_emit_ctx *ctx, const cn_ir_expr *expression);
static bool cn_llvm_validate_block(cn_llvm_emit_ctx *ctx, const cn_ir_block *block);

static const char *cn_llvm_host_target_triple(void) {
#if defined(__x86_64__) || defined(_M_X64)
#if defined(_WIN32)
#if defined(__MINGW32__) || defined(__MINGW64__)
    return "x86_64-w64-windows-gnu";
#else
    return "x86_64-pc-windows-msvc";
#endif
#elif defined(__APPLE__)
    return "x86_64-apple-darwin";
#elif defined(__linux__)
    return "x86_64-pc-linux-gnu";
#else
    return NULL;
#endif
#elif defined(__aarch64__) || defined(_M_ARM64)
#if defined(__MINGW32__) || defined(__MINGW64__)
    return "aarch64-w64-windows-gnu";
#elif defined(_WIN32)
    return "aarch64-pc-windows-msvc";
#elif defined(__APPLE__)
    return "aarch64-apple-darwin";
#elif defined(__linux__)
    return "aarch64-pc-linux-gnu";
#else
    return NULL;
#endif
#else
    return NULL;
#endif
}

static void cn_llvm_scope_release(cn_llvm_emit_ctx *ctx, cn_llvm_scope *scope) {
    cn_llvm_binding *binding = scope->bindings;
    while (binding != NULL) {
        cn_llvm_binding *next = binding->next;
        CN_FREE(ctx->allocator, binding);
        binding = next;
    }
    scope->bindings = NULL;
}

static const cn_llvm_binding *cn_llvm_scope_lookup(const cn_llvm_scope *scope, cn_strview name) {
    for (const cn_llvm_scope *cursor = scope; cursor != NULL; cursor = cursor->parent) {
        for (const cn_llvm_binding *binding = cursor->bindings; binding != NULL; binding = binding->next) {
            if (cn_sv_eq(binding->name, name)) {
                return binding;
            }
        }
    }
    return NULL;
}

static void cn_llvm_scope_define(cn_llvm_emit_ctx *ctx, cn_llvm_scope *scope, cn_strview name, const cn_ir_type *type, int alloca_id) {
    cn_llvm_binding *binding = CN_ALLOC(ctx->allocator, cn_llvm_binding);
    binding->name = name;
    binding->type = type;
    binding->alloca_id = alloca_id;
    binding->next = scope->bindings;
    scope->bindings = binding;
}

static void cn_llvm_emit_unsupported_type(cn_diag_bag *diagnostics, size_t offset, const cn_ir_type *type) {
    char type_name[128];
    cn_ir_type_describe(type, type_name, sizeof(type_name));
    cn_diag_emit(
        diagnostics,
        CN_DIAG_ERROR,
        "E3021",
        offset,
        "llvm ir backend does not support type '%s' yet",
        type_name
    );
}

static void cn_llvm_emit_unsupported_feature(cn_diag_bag *diagnostics, size_t offset, const char *feature_name) {
    cn_diag_emit(
        diagnostics,
        CN_DIAG_ERROR,
        "E3021",
        offset,
        "llvm ir backend does not support %s yet",
        feature_name
    );
}

static const cn_ir_struct *cn_llvm_find_struct(const cn_ir_program *program, cn_strview module_name, cn_strview struct_name) {
    for (size_t module_index = 0; module_index < program->modules.count; ++module_index) {
        const cn_ir_module *module = program->modules.items[module_index];
        if (!cn_sv_eq(module->name, module_name)) {
            continue;
        }

        for (size_t struct_index = 0; struct_index < module->structs.count; ++struct_index) {
            if (cn_sv_eq(module->structs.items[struct_index]->name, struct_name)) {
                return module->structs.items[struct_index];
            }
        }
    }

    return NULL;
}

static bool cn_llvm_is_runtime_owned_struct(cn_strview module_name, cn_strview struct_name) {
    return (cn_sv_eq_cstr(module_name, "std.bytes") && cn_sv_eq_cstr(struct_name, "Buffer")) ||
           (cn_sv_eq_cstr(module_name, "std.lines") && cn_sv_eq_cstr(struct_name, "Buffer")) ||
           (cn_sv_eq_cstr(module_name, "std.net") && cn_sv_eq_cstr(struct_name, "UdpPacket")) ||
           (cn_sv_eq_cstr(module_name, "std.text") && cn_sv_eq_cstr(struct_name, "Builder")) ||
           (cn_sv_eq_cstr(module_name, "std.term") && cn_sv_eq_cstr(struct_name, "Clip")) ||
           (cn_sv_eq_cstr(module_name, "std.term") && cn_sv_eq_cstr(struct_name, "Cell")) ||
           (cn_sv_eq_cstr(module_name, "std.term") && cn_sv_eq_cstr(struct_name, "Buffer")) ||
           (cn_sv_eq_cstr(module_name, "std.term") && cn_sv_eq_cstr(struct_name, "Event"));
}

static bool cn_llvm_type_is_void_like(const cn_ir_type *type) {
    return type == NULL || type->kind == CN_IR_TYPE_VOID || type->kind == CN_IR_TYPE_UNKNOWN;
}

static void cn_llvm_emit_identifier_part(FILE *stream, cn_strview value) {
    for (size_t i = 0; i < value.length; ++i) {
        unsigned char ch = (unsigned char)value.data[i];
        bool is_alpha = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
        bool is_digit = ch >= '0' && ch <= '9';
        if (is_alpha || is_digit || ch == '_') {
            fputc((int)ch, stream);
        } else {
            fprintf(stream, "_x%02X_", ch);
        }
    }
}

static const char *cn_llvm_compare_prefix(cn_ir_binary_op op, cn_ir_type_kind kind) {
    switch (op) {
    case CN_IR_BINARY_LESS:
        return kind == CN_IR_TYPE_U8 ? " = icmp ult " : " = icmp slt ";
    case CN_IR_BINARY_LESS_EQUAL:
        return kind == CN_IR_TYPE_U8 ? " = icmp ule " : " = icmp sle ";
    case CN_IR_BINARY_GREATER:
        return kind == CN_IR_TYPE_U8 ? " = icmp ugt " : " = icmp sgt ";
    case CN_IR_BINARY_GREATER_EQUAL:
        return kind == CN_IR_TYPE_U8 ? " = icmp uge " : " = icmp sge ";
    default:
        return NULL;
    }
}

static bool cn_llvm_validate_type(cn_llvm_emit_ctx *ctx, const cn_ir_type *type, size_t offset) {
    if (type == NULL) {
        return false;
    }

    switch (type->kind) {
    case CN_IR_TYPE_INT:
    case CN_IR_TYPE_U8:
    case CN_IR_TYPE_BOOL:
    case CN_IR_TYPE_STR:
    case CN_IR_TYPE_VOID:
        return true;
    case CN_IR_TYPE_RESULT:
    case CN_IR_TYPE_PTR:
    case CN_IR_TYPE_SLICE:
    case CN_IR_TYPE_ARRAY:
        if (cn_llvm_type_is_void_like(type->inner)) {
            cn_llvm_emit_unsupported_type(ctx->diagnostics, offset, type);
            return false;
        }
        return cn_llvm_validate_type(ctx, type->inner, offset);
    case CN_IR_TYPE_NAMED:
        if (cn_llvm_find_struct(ctx->program, type->module_name, type->name) == NULL) {
            cn_llvm_emit_unsupported_type(ctx->diagnostics, offset, type);
            return false;
        }
        return true;
    case CN_IR_TYPE_UNKNOWN:
        cn_llvm_emit_unsupported_type(ctx->diagnostics, offset, type);
        return false;
    }

    return false;
}

static bool cn_llvm_type_supports_equality(cn_llvm_emit_ctx *ctx, const cn_ir_type *type) {
    switch (type->kind) {
    case CN_IR_TYPE_INT:
    case CN_IR_TYPE_U8:
    case CN_IR_TYPE_BOOL:
    case CN_IR_TYPE_PTR:
    case CN_IR_TYPE_STR:
        return true;
    case CN_IR_TYPE_RESULT:
    case CN_IR_TYPE_SLICE:
    case CN_IR_TYPE_ARRAY:
        return type->inner != NULL && cn_llvm_type_supports_equality(ctx, type->inner);
    case CN_IR_TYPE_NAMED: {
        const cn_ir_struct *struct_decl = cn_llvm_find_struct(ctx->program, type->module_name, type->name);
        if (struct_decl == NULL) {
            return false;
        }

        for (size_t i = 0; i < struct_decl->fields.count; ++i) {
            if (!cn_llvm_type_supports_equality(ctx, struct_decl->fields.items[i].type)) {
                return false;
            }
        }
        return true;
    }
    case CN_IR_TYPE_VOID:
    case CN_IR_TYPE_UNKNOWN:
        return false;
    }

    return false;
}

static bool cn_llvm_type_supports_print(const cn_ir_type *type) {
    return type->kind == CN_IR_TYPE_INT ||
           type->kind == CN_IR_TYPE_U8 ||
           type->kind == CN_IR_TYPE_BOOL ||
           type->kind == CN_IR_TYPE_STR;
}

static bool cn_llvm_call_matches(const cn_ir_expr *expression, const char *module_name, const char *function_name) {
    if (expression->data.call.target_kind != CN_IR_CALL_BUILTIN) {
        return false;
    }

    if (!cn_sv_eq_cstr(expression->data.call.function_name, function_name)) {
        return false;
    }

    if (module_name == NULL) {
        return expression->data.call.module_name.length == 0;
    }

    return cn_sv_eq_cstr(expression->data.call.module_name, module_name);
}

static bool cn_llvm_validate_builtin_arguments(cn_llvm_emit_ctx *ctx, const cn_ir_expr *expression, size_t expected_count, const char *feature_name) {
    if (expression->data.call.arguments.count != expected_count) {
        cn_llvm_emit_unsupported_feature(ctx->diagnostics, expression->offset, feature_name);
        return false;
    }

    for (size_t i = 0; i < expression->data.call.arguments.count; ++i) {
        if (!cn_llvm_validate_expression(ctx, expression->data.call.arguments.items[i])) {
            return false;
        }
    }

    return true;
}

static bool cn_llvm_validate_call(cn_llvm_emit_ctx *ctx, const cn_ir_expr *expression) {
    if (!cn_llvm_validate_type(ctx, expression->type, expression->offset)) {
        return false;
    }

    if (expression->data.call.target_kind == CN_IR_CALL_BUILTIN) {
        if (cn_llvm_call_matches(expression, NULL, "print")) {
            if (!cn_llvm_validate_builtin_arguments(ctx, expression, 1, "unexpected builtin print arity")) {
                return false;
            }

            if (!cn_llvm_type_supports_print(expression->data.call.arguments.items[0]->type)) {
                cn_llvm_emit_unsupported_feature(ctx->diagnostics, expression->offset, "print on non-display values");
                return false;
            }
            return true;
        }

        if (cn_llvm_call_matches(expression, NULL, "input")) {
            if (!cn_llvm_validate_builtin_arguments(ctx, expression, 0, "unexpected builtin input arity")) {
                return false;
            }
            return true;
        }

        if (cn_llvm_call_matches(expression, "std.io", "read_line") ||
            cn_llvm_call_matches(expression, "std.bytes", "new") ||
            cn_llvm_call_matches(expression, "std.lines", "new") ||
            cn_llvm_call_matches(expression, "std.term", "is_tty") ||
            cn_llvm_call_matches(expression, "std.term", "columns") ||
            cn_llvm_call_matches(expression, "std.term", "rows") ||
            cn_llvm_call_matches(expression, "std.term", "term_name") ||
            cn_llvm_call_matches(expression, "std.term", "supports_truecolor") ||
            cn_llvm_call_matches(expression, "std.term", "supports_256color") ||
            cn_llvm_call_matches(expression, "std.term", "supports_unicode") ||
            cn_llvm_call_matches(expression, "std.term", "supports_mouse") ||
            cn_llvm_call_matches(expression, "std.term", "read_byte") ||
            cn_llvm_call_matches(expression, "std.term", "read_event") ||
            cn_llvm_call_matches(expression, "std.term", "read_paste") ||
            cn_llvm_call_matches(expression, "std.term", "reset_style") ||
            cn_llvm_call_matches(expression, "std.term", "enable_mouse") ||
            cn_llvm_call_matches(expression, "std.term", "disable_mouse") ||
            cn_llvm_call_matches(expression, "std.term", "enable_bracketed_paste") ||
            cn_llvm_call_matches(expression, "std.term", "disable_bracketed_paste") ||
            cn_llvm_call_matches(expression, "std.term", "flush") ||
            cn_llvm_call_matches(expression, "std.term", "clear") ||
            cn_llvm_call_matches(expression, "std.term", "clear_line") ||
            cn_llvm_call_matches(expression, "std.term", "clear_line_left") ||
            cn_llvm_call_matches(expression, "std.term", "clear_line_right") ||
            cn_llvm_call_matches(expression, "std.term", "save_cursor") ||
            cn_llvm_call_matches(expression, "std.term", "restore_cursor") ||
            cn_llvm_call_matches(expression, "std.term", "hide_cursor") ||
            cn_llvm_call_matches(expression, "std.term", "show_cursor") ||
            cn_llvm_call_matches(expression, "std.term", "enter_alt_screen") ||
            cn_llvm_call_matches(expression, "std.term", "leave_alt_screen") ||
            cn_llvm_call_matches(expression, "std.term", "reset_scroll_region") ||
            cn_llvm_call_matches(expression, "std.term", "enter_raw_mode") ||
            cn_llvm_call_matches(expression, "std.term", "leave_raw_mode") ||
            cn_llvm_call_matches(expression, "std.process", "platform") ||
            cn_llvm_call_matches(expression, "std.process", "arch") ||
            cn_llvm_call_matches(expression, "std.text", "new") ||
            cn_llvm_call_matches(expression, "std.time", "now_ms") ||
            cn_llvm_call_matches(expression, "std.fs", "cwd")) {
            return cn_llvm_validate_builtin_arguments(ctx, expression, 0, "unexpected builtin zero-argument stdlib arity");
        }

        if (cn_llvm_call_matches(expression, NULL, "str_copy") ||
            cn_llvm_call_matches(expression, "std.strings", "copy")) {
            return cn_llvm_validate_builtin_arguments(ctx, expression, 1, "unexpected builtin string-copy arity");
        }

        if (cn_llvm_call_matches(expression, NULL, "str_concat") ||
            cn_llvm_call_matches(expression, "std.strings", "concat")) {
            return cn_llvm_validate_builtin_arguments(ctx, expression, 2, "unexpected builtin string-concat arity");
        }

        if (cn_llvm_call_matches(expression, "std.math", "abs") ||
            cn_llvm_call_matches(expression, "std.math", "sign") ||
            cn_llvm_call_matches(expression, "std.math", "square") ||
            cn_llvm_call_matches(expression, "std.math", "cube") ||
            cn_llvm_call_matches(expression, "std.math", "is_even") ||
            cn_llvm_call_matches(expression, "std.math", "is_odd") ||
            cn_llvm_call_matches(expression, "std.bytes", "with_capacity") ||
            cn_llvm_call_matches(expression, "std.bytes", "release") ||
            cn_llvm_call_matches(expression, "std.bytes", "clear") ||
            cn_llvm_call_matches(expression, "std.bytes", "length") ||
            cn_llvm_call_matches(expression, "std.bytes", "capacity") ||
            cn_llvm_call_matches(expression, "std.bytes", "view") ||
            cn_llvm_call_matches(expression, "std.ipc", "stdin_close") ||
            cn_llvm_call_matches(expression, "std.ipc", "wait") ||
            cn_llvm_call_matches(expression, "std.ipc", "kill") ||
            cn_llvm_call_matches(expression, "std.ipc", "release") ||
            cn_llvm_call_matches(expression, "std.lines", "with_capacity") ||
            cn_llvm_call_matches(expression, "std.lines", "release") ||
            cn_llvm_call_matches(expression, "std.lines", "clear") ||
            cn_llvm_call_matches(expression, "std.lines", "length") ||
            cn_llvm_call_matches(expression, "std.lines", "capacity") ||
            cn_llvm_call_matches(expression, "std.x11", "pump") ||
            cn_llvm_call_matches(expression, "std.x11", "close") ||
            cn_llvm_call_matches(expression, "std.strings", "len") ||
            cn_llvm_call_matches(expression, "std.parse", "to_int") ||
            cn_llvm_call_matches(expression, "std.parse", "to_bool") ||
            cn_llvm_call_matches(expression, "std.net", "is_ipv4") ||
            cn_llvm_call_matches(expression, "std.net", "accept") ||
            cn_llvm_call_matches(expression, "std.net", "close") ||
            cn_llvm_call_matches(expression, "std.env", "has") ||
            cn_llvm_call_matches(expression, "std.env", "get") ||
            cn_llvm_call_matches(expression, "std.term", "read_byte_timeout") ||
            cn_llvm_call_matches(expression, "std.term", "read_event_timeout") ||
            cn_llvm_call_matches(expression, "std.term", "codepoint_width") ||
            cn_llvm_call_matches(expression, "std.term", "string_width") ||
            cn_llvm_call_matches(expression, "std.fs", "exists") ||
            cn_llvm_call_matches(expression, "std.fs", "create_dir") ||
            cn_llvm_call_matches(expression, "std.fs", "remove_dir") ||
            cn_llvm_call_matches(expression, "std.fs", "read_text") ||
            cn_llvm_call_matches(expression, "std.fs", "file_size") ||
            cn_llvm_call_matches(expression, "std.fs", "remove") ||
            cn_llvm_call_matches(expression, "std.io", "write") ||
            cn_llvm_call_matches(expression, "std.io", "write_line") ||
            cn_llvm_call_matches(expression, "std.term", "write") ||
            cn_llvm_call_matches(expression, "std.term", "buffer_free") ||
            cn_llvm_call_matches(expression, "std.process", "exit") ||
            cn_llvm_call_matches(expression, "std.time", "sleep_ms") ||
            cn_llvm_call_matches(expression, "std.text", "with_capacity") ||
            cn_llvm_call_matches(expression, "std.text", "release") ||
            cn_llvm_call_matches(expression, "std.text", "clear") ||
            cn_llvm_call_matches(expression, "std.text", "length") ||
            cn_llvm_call_matches(expression, "std.text", "capacity") ||
            cn_llvm_call_matches(expression, "std.text", "build") ||
            cn_llvm_call_matches(expression, "std.text", "view") ||
            cn_llvm_call_matches(expression, "std.path", "stem") ||
            cn_llvm_call_matches(expression, "std.path", "extension") ||
            cn_llvm_call_matches(expression, "std.path", "is_absolute") ||
            cn_llvm_call_matches(expression, "std.path", "file_name") ||
            cn_llvm_call_matches(expression, "std.path", "parent")) {
            return cn_llvm_validate_builtin_arguments(ctx, expression, 1, "unexpected builtin stdlib arity");
        }

        if (cn_llvm_call_matches(expression, "std.math", "min") ||
            cn_llvm_call_matches(expression, "std.math", "max") ||
            cn_llvm_call_matches(expression, "std.math", "gcd") ||
            cn_llvm_call_matches(expression, "std.math", "lcm") ||
            cn_llvm_call_matches(expression, "std.math", "distance") ||
            cn_llvm_call_matches(expression, "std.ipc", "spawn") ||
            cn_llvm_call_matches(expression, "std.bytes", "push") ||
            cn_llvm_call_matches(expression, "std.bytes", "append") ||
            cn_llvm_call_matches(expression, "std.bytes", "get") ||
            cn_llvm_call_matches(expression, "std.ipc", "stdin_write") ||
            cn_llvm_call_matches(expression, "std.ipc", "stdin_write_line") ||
            cn_llvm_call_matches(expression, "std.ipc", "stdout_read") ||
            cn_llvm_call_matches(expression, "std.ipc", "stdout_read_line") ||
            cn_llvm_call_matches(expression, "std.ipc", "stderr_read") ||
            cn_llvm_call_matches(expression, "std.ipc", "stderr_read_line") ||
            cn_llvm_call_matches(expression, "std.lines", "get") ||
            cn_llvm_call_matches(expression, "std.lines", "push") ||
            cn_llvm_call_matches(expression, "std.lines", "remove") ||
            cn_llvm_call_matches(expression, "std.strings", "eq") ||
            cn_llvm_call_matches(expression, "std.strings", "starts_with") ||
            cn_llvm_call_matches(expression, "std.strings", "ends_with") ||
            cn_llvm_call_matches(expression, "std.net", "tcp_connect") ||
            cn_llvm_call_matches(expression, "std.net", "tcp_listen") ||
            cn_llvm_call_matches(expression, "std.net", "udp_bind") ||
            cn_llvm_call_matches(expression, "std.net", "udp_recv_from") ||
            cn_llvm_call_matches(expression, "std.term", "decode_codepoint") ||
            cn_llvm_call_matches(expression, "std.term", "next_codepoint_offset") ||
            cn_llvm_call_matches(expression, "std.term", "buffer_new") ||
            cn_llvm_call_matches(expression, "std.term", "buffer_clear") ||
            cn_llvm_call_matches(expression, "std.term", "render_diff") ||
            cn_llvm_call_matches(expression, "std.fs", "copy") ||
            cn_llvm_call_matches(expression, "std.fs", "write_text") ||
            cn_llvm_call_matches(expression, "std.fs", "append_text") ||
            cn_llvm_call_matches(expression, "std.fs", "rename") ||
            cn_llvm_call_matches(expression, "std.fs", "move") ||
            cn_llvm_call_matches(expression, "std.net", "join_host_port") ||
            cn_llvm_call_matches(expression, "std.net", "send") ||
            cn_llvm_call_matches(expression, "std.net", "recv") ||
            cn_llvm_call_matches(expression, "std.text", "append") ||
            cn_llvm_call_matches(expression, "std.text", "push_byte") ||
            cn_llvm_call_matches(expression, "std.path", "join")) {
            return cn_llvm_validate_builtin_arguments(ctx, expression, 2, "unexpected builtin stdlib arity");
        }

        if (cn_llvm_call_matches(expression, "std.math", "clamp") ||
            cn_llvm_call_matches(expression, "std.math", "between") ||
            cn_llvm_call_matches(expression, "std.bytes", "set") ||
            cn_llvm_call_matches(expression, "std.ipc", "request_line") ||
            cn_llvm_call_matches(expression, "std.lines", "set") ||
            cn_llvm_call_matches(expression, "std.lines", "insert") ||
            cn_llvm_call_matches(expression, "std.term", "set_style") ||
            cn_llvm_call_matches(expression, "std.term", "rgb") ||
            cn_llvm_call_matches(expression, "std.term", "buffer_get") ||
            cn_llvm_call_matches(expression, "std.term", "render_diff_clip") ||
            cn_llvm_call_matches(expression, "std.x11", "open_window")) {
            return cn_llvm_validate_builtin_arguments(ctx, expression, 3, "unexpected builtin stdlib arity");
        }

        if (cn_llvm_call_matches(expression, "std.term", "move_cursor")) {
            return cn_llvm_validate_builtin_arguments(ctx, expression, 2, "unexpected builtin stdlib arity");
        }

        if (cn_llvm_call_matches(expression, "std.term", "set_scroll_region")) {
            return cn_llvm_validate_builtin_arguments(ctx, expression, 2, "unexpected builtin stdlib arity");
        }

        if (cn_llvm_call_matches(expression, "std.net", "udp_send_to")) {
            return cn_llvm_validate_builtin_arguments(ctx, expression, 4, "unexpected builtin stdlib arity");
        }

        if (cn_llvm_call_matches(expression, "std.term", "buffer_set")) {
            return cn_llvm_validate_builtin_arguments(ctx, expression, 4, "unexpected builtin stdlib arity");
        }

        if (cn_llvm_call_matches(expression, "std.term", "buffer_resize")) {
            return cn_llvm_validate_builtin_arguments(ctx, expression, 4, "unexpected builtin stdlib arity");
        }

        cn_llvm_emit_unsupported_feature(ctx->diagnostics, expression->offset, "unknown builtin call target");
        return false;
    }

    for (size_t i = 0; i < expression->data.call.arguments.count; ++i) {
        if (!cn_llvm_validate_expression(ctx, expression->data.call.arguments.items[i])) {
            return false;
        }
    }

    return true;
}

static bool cn_llvm_validate_expression(cn_llvm_emit_ctx *ctx, const cn_ir_expr *expression) {
    if (expression == NULL) {
        return false;
    }

    if (!cn_llvm_validate_type(ctx, expression->type, expression->offset)) {
        return false;
    }

    switch (expression->kind) {
    case CN_IR_EXPR_INT:
    case CN_IR_EXPR_BOOL:
    case CN_IR_EXPR_STRING:
    case CN_IR_EXPR_LOCAL:
    case CN_IR_EXPR_ERR:
        return true;
    case CN_IR_EXPR_UNARY:
        return cn_llvm_validate_expression(ctx, expression->data.unary.operand);
    case CN_IR_EXPR_BINARY:
        if (!cn_llvm_validate_expression(ctx, expression->data.binary.left) ||
            !cn_llvm_validate_expression(ctx, expression->data.binary.right)) {
            return false;
        }

        if ((expression->data.binary.op == CN_IR_BINARY_EQUAL || expression->data.binary.op == CN_IR_BINARY_NOT_EQUAL) &&
            !cn_llvm_type_supports_equality(ctx, expression->data.binary.left->type)) {
            cn_llvm_emit_unsupported_feature(ctx->diagnostics, expression->offset, "equality on aggregate values");
            return false;
        }
        return true;
    case CN_IR_EXPR_IF:
        return cn_llvm_validate_expression(ctx, expression->data.if_expr.condition) &&
               cn_llvm_validate_expression(ctx, expression->data.if_expr.then_expr) &&
               cn_llvm_validate_expression(ctx, expression->data.if_expr.else_expr);
    case CN_IR_EXPR_CALL:
        return cn_llvm_validate_call(ctx, expression);
    case CN_IR_EXPR_ARRAY_LITERAL:
        for (size_t i = 0; i < expression->data.array_literal.items.count; ++i) {
            if (!cn_llvm_validate_expression(ctx, expression->data.array_literal.items.items[i])) {
                return false;
            }
        }
        return true;
    case CN_IR_EXPR_SLICE_FROM_ARRAY:
        return cn_llvm_validate_expression(ctx, expression->data.slice_from_array.base);
    case CN_IR_EXPR_INDEX:
        return cn_llvm_validate_expression(ctx, expression->data.index.base) &&
               cn_llvm_validate_expression(ctx, expression->data.index.index);
    case CN_IR_EXPR_SLICE_VIEW:
        return cn_llvm_validate_expression(ctx, expression->data.slice_view.base) &&
               (expression->data.slice_view.start == NULL ||
                cn_llvm_validate_expression(ctx, expression->data.slice_view.start)) &&
               (expression->data.slice_view.end == NULL ||
                cn_llvm_validate_expression(ctx, expression->data.slice_view.end));
    case CN_IR_EXPR_FIELD:
        return cn_llvm_validate_expression(ctx, expression->data.field.base);
    case CN_IR_EXPR_STRUCT_LITERAL:
        for (size_t i = 0; i < expression->data.struct_literal.fields.count; ++i) {
            if (!cn_llvm_validate_expression(ctx, expression->data.struct_literal.fields.items[i].value)) {
                return false;
            }
        }
        return true;
    case CN_IR_EXPR_OK:
        return cn_llvm_validate_expression(ctx, expression->data.ok_expr.value);
    case CN_IR_EXPR_ALLOC:
        return cn_llvm_validate_type(ctx, expression->data.alloc_expr.alloc_type, expression->offset);
    case CN_IR_EXPR_ADDR:
        return cn_llvm_validate_expression(ctx, expression->data.addr_expr.target);
    case CN_IR_EXPR_DEREF:
        return cn_llvm_validate_expression(ctx, expression->data.deref_expr.target);
    }

    return false;
}

static bool cn_llvm_validate_statement(cn_llvm_emit_ctx *ctx, const cn_ir_stmt *statement) {
    switch (statement->kind) {
    case CN_IR_STMT_LET:
        return cn_llvm_validate_type(ctx, statement->data.let_stmt.type, statement->offset) &&
               cn_llvm_validate_expression(ctx, statement->data.let_stmt.initializer);
    case CN_IR_STMT_ASSIGN:
        return cn_llvm_validate_expression(ctx, statement->data.assign_stmt.target) &&
               cn_llvm_validate_expression(ctx, statement->data.assign_stmt.value);
    case CN_IR_STMT_RETURN:
        if (statement->data.return_stmt.value == NULL) {
            return true;
        }
        return cn_llvm_validate_expression(ctx, statement->data.return_stmt.value);
    case CN_IR_STMT_EXPR:
        return cn_llvm_validate_expression(ctx, statement->data.expr_stmt.value);
    case CN_IR_STMT_IF:
        return cn_llvm_validate_expression(ctx, statement->data.if_stmt.condition) &&
               cn_llvm_validate_block(ctx, statement->data.if_stmt.then_block) &&
               (statement->data.if_stmt.else_block == NULL ||
                cn_llvm_validate_block(ctx, statement->data.if_stmt.else_block));
    case CN_IR_STMT_WHILE:
        return cn_llvm_validate_expression(ctx, statement->data.while_stmt.condition) &&
               cn_llvm_validate_block(ctx, statement->data.while_stmt.body);
    case CN_IR_STMT_LOOP:
        return cn_llvm_validate_block(ctx, statement->data.loop_stmt.body);
    case CN_IR_STMT_FOR:
        return cn_llvm_validate_type(ctx, statement->data.for_stmt.type, statement->offset) &&
               cn_llvm_validate_expression(ctx, statement->data.for_stmt.start) &&
               cn_llvm_validate_expression(ctx, statement->data.for_stmt.end) &&
               cn_llvm_validate_block(ctx, statement->data.for_stmt.body);
    case CN_IR_STMT_FREE:
        return cn_llvm_validate_expression(ctx, statement->data.free_stmt.value);
    }

    return false;
}

static bool cn_llvm_validate_block(cn_llvm_emit_ctx *ctx, const cn_ir_block *block) {
    for (size_t i = 0; i < block->statements.count; ++i) {
        if (!cn_llvm_validate_statement(ctx, block->statements.items[i])) {
            return false;
        }
    }
    return true;
}

static bool cn_llvm_validate_function(cn_llvm_emit_ctx *ctx, const cn_ir_function *function) {
    if (!cn_llvm_validate_type(ctx, function->return_type, function->offset)) {
        return false;
    }

    for (size_t i = 0; i < function->parameters.count; ++i) {
        if (!cn_llvm_validate_type(ctx, function->parameters.items[i].type, function->parameters.items[i].offset)) {
            return false;
        }
    }

    return cn_llvm_validate_block(ctx, function->body);
}

static bool cn_llvm_validate_struct(cn_llvm_emit_ctx *ctx, const cn_ir_struct *struct_decl) {
    for (size_t i = 0; i < struct_decl->fields.count; ++i) {
        if (!cn_llvm_validate_type(ctx, struct_decl->fields.items[i].type, struct_decl->fields.items[i].offset)) {
            return false;
        }
    }
    return true;
}

static bool cn_llvm_validate_module(cn_llvm_emit_ctx *ctx, const cn_ir_module *module, size_t fallback_offset) {
    CN_UNUSED(fallback_offset);

    for (size_t i = 0; i < module->structs.count; ++i) {
        if (!cn_llvm_validate_struct(ctx, module->structs.items[i])) {
            return false;
        }
    }

    for (size_t i = 0; i < module->functions.count; ++i) {
        if (!cn_llvm_validate_function(ctx, module->functions.items[i])) {
            return false;
        }
    }

    return true;
}

static cn_llvm_value cn_llvm_invalid_value(const cn_ir_type *type) {
    cn_llvm_value value;
    value.type = type;
    value.is_valid = false;
    value.is_void = false;
    value.is_constant = false;
    value.data.int_value = 0;
    return value;
}

static cn_llvm_address cn_llvm_invalid_address(const cn_ir_type *type) {
    cn_llvm_address address;
    address.type = type;
    address.is_valid = false;
    address.pointer_reg_id = -1;
    return address;
}

static cn_llvm_value cn_llvm_constant_int(const cn_ir_type *type, int64_t value) {
    cn_llvm_value result;
    result.type = type;
    result.is_valid = true;
    result.is_void = false;
    result.is_constant = true;
    result.data.int_value = value;
    return result;
}

static cn_llvm_value cn_llvm_constant_bool(const cn_ir_type *type, bool value) {
    cn_llvm_value result;
    result.type = type;
    result.is_valid = true;
    result.is_void = false;
    result.is_constant = true;
    result.data.bool_value = value;
    return result;
}

static cn_llvm_value cn_llvm_register_value(const cn_ir_type *type, int reg_id) {
    cn_llvm_value result;
    result.type = type;
    result.is_valid = true;
    result.is_void = false;
    result.is_constant = false;
    result.data.reg_id = reg_id;
    return result;
}

static cn_llvm_address cn_llvm_register_address(const cn_ir_type *type, int pointer_reg_id) {
    cn_llvm_address result;
    result.type = type;
    result.is_valid = true;
    result.pointer_reg_id = pointer_reg_id;
    return result;
}

static cn_llvm_value cn_llvm_void_value(const cn_ir_type *type) {
    cn_llvm_value result;
    result.type = type;
    result.is_valid = true;
    result.is_void = true;
    result.is_constant = false;
    result.data.int_value = 0;
    return result;
}

static int cn_llvm_next_temp(cn_llvm_function_ctx *ctx) {
    int value = ctx->next_temp;
    ctx->next_temp += 1;
    return value;
}

static int cn_llvm_next_label(cn_llvm_function_ctx *ctx) {
    int value = ctx->next_label;
    ctx->next_label += 1;
    return value;
}

static void cn_llvm_emit_reg(FILE *stream, int reg_id) {
    fprintf(stream, "%%t%d", reg_id);
}

static void cn_llvm_emit_label_name(FILE *stream, int label_id) {
    fprintf(stream, "l%d", label_id);
}

static void cn_llvm_emit_value_ref(FILE *stream, cn_llvm_value value) {
    if (value.is_constant) {
        if (value.type->kind == CN_IR_TYPE_BOOL) {
            fputs(value.data.bool_value ? "1" : "0", stream);
        } else {
            fprintf(stream, "%lld", (long long)value.data.int_value);
        }
        return;
    }

    cn_llvm_emit_reg(stream, value.data.reg_id);
}

static void cn_llvm_emit_indent(FILE *stream) {
    fputs("  ", stream);
}

static void cn_llvm_emit_type(FILE *stream, const cn_ir_type *type) {
    switch (type->kind) {
    case CN_IR_TYPE_INT:
        fputs("i64", stream);
        break;
    case CN_IR_TYPE_U8:
        fputs("i8", stream);
        break;
    case CN_IR_TYPE_BOOL:
        fputs("i1", stream);
        break;
    case CN_IR_TYPE_STR:
    case CN_IR_TYPE_PTR:
        fputs("ptr", stream);
        break;
    case CN_IR_TYPE_SLICE:
        fputs("{ ptr, i64 }", stream);
        break;
    case CN_IR_TYPE_VOID:
        fputs("void", stream);
        break;
    case CN_IR_TYPE_RESULT:
        fputs("{ i1, ", stream);
        cn_llvm_emit_type(stream, type->inner);
        fputs(" }", stream);
        break;
    case CN_IR_TYPE_ARRAY:
        fprintf(stream, "[%zu x ", type->array_size);
        cn_llvm_emit_type(stream, type->inner);
        fputc(']', stream);
        break;
    case CN_IR_TYPE_NAMED:
        fputs("%cn_", stream);
        cn_llvm_emit_identifier_part(stream, type->module_name);
        fputs("__", stream);
        cn_llvm_emit_identifier_part(stream, type->name);
        break;
    case CN_IR_TYPE_UNKNOWN:
        fputs("<unknown>", stream);
        break;
    }
}

static void cn_llvm_emit_terminated(cn_llvm_function_ctx *ctx) {
    ctx->current_block_terminated = true;
}

static void cn_llvm_emit_label(cn_llvm_function_ctx *ctx, int label_id) {
    cn_llvm_emit_label_name(ctx->emit->stream, label_id);
    fputs(":\n", ctx->emit->stream);
    ctx->current_block_terminated = false;
}

static void cn_llvm_begin_unreachable_block_if_needed(cn_llvm_function_ctx *ctx) {
    if (!ctx->current_block_terminated) {
        return;
    }

    cn_llvm_emit_label(ctx, cn_llvm_next_label(ctx));
}

static int cn_llvm_emit_alloca(cn_llvm_function_ctx *ctx, const cn_ir_type *type) {
    int reg_id = cn_llvm_next_temp(ctx);
    cn_llvm_emit_indent(ctx->emit->stream);
    cn_llvm_emit_reg(ctx->emit->stream, reg_id);
    fputs(" = alloca ", ctx->emit->stream);
    cn_llvm_emit_type(ctx->emit->stream, type);
    fputc('\n', ctx->emit->stream);
    return reg_id;
}

static void cn_llvm_emit_store(cn_llvm_function_ctx *ctx, cn_llvm_value value, int pointer_reg_id) {
    cn_llvm_emit_indent(ctx->emit->stream);
    fputs("store ", ctx->emit->stream);
    cn_llvm_emit_type(ctx->emit->stream, value.type);
    fputc(' ', ctx->emit->stream);
    cn_llvm_emit_value_ref(ctx->emit->stream, value);
    fputs(", ptr ", ctx->emit->stream);
    cn_llvm_emit_reg(ctx->emit->stream, pointer_reg_id);
    fputc('\n', ctx->emit->stream);
}

static cn_llvm_value cn_llvm_emit_load(cn_llvm_function_ctx *ctx, const cn_ir_type *type, int pointer_reg_id) {
    int reg_id = cn_llvm_next_temp(ctx);
    cn_llvm_emit_indent(ctx->emit->stream);
    cn_llvm_emit_reg(ctx->emit->stream, reg_id);
    fputs(" = load ", ctx->emit->stream);
    cn_llvm_emit_type(ctx->emit->stream, type);
    fputs(", ptr ", ctx->emit->stream);
    cn_llvm_emit_reg(ctx->emit->stream, pointer_reg_id);
    fputc('\n', ctx->emit->stream);
    return cn_llvm_register_value(type, reg_id);
}

static void cn_llvm_emit_br(cn_llvm_function_ctx *ctx, int label_id) {
    cn_llvm_emit_indent(ctx->emit->stream);
    fputs("br label %", ctx->emit->stream);
    cn_llvm_emit_label_name(ctx->emit->stream, label_id);
    fputc('\n', ctx->emit->stream);
    cn_llvm_emit_terminated(ctx);
}

static void cn_llvm_emit_cond_br(cn_llvm_function_ctx *ctx, cn_llvm_value condition, int then_label, int else_label) {
    cn_llvm_emit_indent(ctx->emit->stream);
    fputs("br i1 ", ctx->emit->stream);
    cn_llvm_emit_value_ref(ctx->emit->stream, condition);
    fputs(", label %", ctx->emit->stream);
    cn_llvm_emit_label_name(ctx->emit->stream, then_label);
    fputs(", label %", ctx->emit->stream);
    cn_llvm_emit_label_name(ctx->emit->stream, else_label);
    fputc('\n', ctx->emit->stream);
    cn_llvm_emit_terminated(ctx);
}

static void cn_llvm_emit_ret(cn_llvm_function_ctx *ctx, cn_llvm_value value) {
    cn_llvm_emit_indent(ctx->emit->stream);
    if (value.is_void) {
        fputs("ret void\n", ctx->emit->stream);
    } else {
        fputs("ret ", ctx->emit->stream);
        cn_llvm_emit_type(ctx->emit->stream, value.type);
        fputc(' ', ctx->emit->stream);
        cn_llvm_emit_value_ref(ctx->emit->stream, value);
        fputc('\n', ctx->emit->stream);
    }
    cn_llvm_emit_terminated(ctx);
}

static void cn_llvm_emit_unreachable(cn_llvm_function_ctx *ctx) {
    cn_llvm_emit_indent(ctx->emit->stream);
    fputs("unreachable\n", ctx->emit->stream);
    cn_llvm_emit_terminated(ctx);
}

static void cn_llvm_emit_function_symbol(FILE *stream, cn_strview module_name, cn_strview function_name) {
    fputs("@cn_", stream);
    cn_llvm_emit_identifier_part(stream, module_name);
    fputs("__", stream);
    cn_llvm_emit_identifier_part(stream, function_name);
}

static int cn_llvm_emit_struct_field_gep(
    cn_llvm_function_ctx *ctx,
    const cn_ir_type *base_type,
    int base_pointer_reg_id,
    size_t field_index
) {
    int reg_id = cn_llvm_next_temp(ctx);
    cn_llvm_emit_indent(ctx->emit->stream);
    cn_llvm_emit_reg(ctx->emit->stream, reg_id);
    fputs(" = getelementptr inbounds ", ctx->emit->stream);
    cn_llvm_emit_type(ctx->emit->stream, base_type);
    fputs(", ptr ", ctx->emit->stream);
    cn_llvm_emit_reg(ctx->emit->stream, base_pointer_reg_id);
    fprintf(ctx->emit->stream, ", i32 0, i32 %zu\n", field_index);
    return reg_id;
}

static int cn_llvm_emit_array_element_gep(
    cn_llvm_function_ctx *ctx,
    const cn_ir_type *array_type,
    int base_pointer_reg_id,
    cn_llvm_value index
) {
    int reg_id = cn_llvm_next_temp(ctx);
    cn_llvm_emit_indent(ctx->emit->stream);
    cn_llvm_emit_reg(ctx->emit->stream, reg_id);
    fputs(" = getelementptr inbounds ", ctx->emit->stream);
    cn_llvm_emit_type(ctx->emit->stream, array_type);
    fputs(", ptr ", ctx->emit->stream);
    cn_llvm_emit_reg(ctx->emit->stream, base_pointer_reg_id);
    fputs(", i64 0, i64 ", ctx->emit->stream);
    cn_llvm_emit_value_ref(ctx->emit->stream, index);
    fputc('\n', ctx->emit->stream);
    return reg_id;
}

static int cn_llvm_emit_pointer_element_gep(
    cn_llvm_function_ctx *ctx,
    const cn_ir_type *element_type,
    cn_llvm_value base_pointer,
    cn_llvm_value index
) {
    int reg_id = cn_llvm_next_temp(ctx);
    cn_llvm_emit_indent(ctx->emit->stream);
    cn_llvm_emit_reg(ctx->emit->stream, reg_id);
    fputs(" = getelementptr inbounds ", ctx->emit->stream);
    cn_llvm_emit_type(ctx->emit->stream, element_type);
    fputs(", ptr ", ctx->emit->stream);
    cn_llvm_emit_value_ref(ctx->emit->stream, base_pointer);
    fputs(", i64 ", ctx->emit->stream);
    cn_llvm_emit_value_ref(ctx->emit->stream, index);
    fputc('\n', ctx->emit->stream);
    return reg_id;
}

static int cn_llvm_emit_insertvalue(
    cn_llvm_function_ctx *ctx,
    const cn_ir_type *aggregate_type,
    bool aggregate_is_zero,
    int aggregate_reg_id,
    cn_llvm_value element,
    size_t index
) {
    int reg_id = cn_llvm_next_temp(ctx);
    cn_llvm_emit_indent(ctx->emit->stream);
    cn_llvm_emit_reg(ctx->emit->stream, reg_id);
    fputs(" = insertvalue ", ctx->emit->stream);
    cn_llvm_emit_type(ctx->emit->stream, aggregate_type);
    fputc(' ', ctx->emit->stream);
    if (aggregate_is_zero) {
        fputs("zeroinitializer", ctx->emit->stream);
    } else {
        cn_llvm_emit_reg(ctx->emit->stream, aggregate_reg_id);
    }
    fputs(", ", ctx->emit->stream);
    cn_llvm_emit_type(ctx->emit->stream, element.type);
    fputc(' ', ctx->emit->stream);
    cn_llvm_emit_value_ref(ctx->emit->stream, element);
    fprintf(ctx->emit->stream, ", %zu\n", index);
    return reg_id;
}

static cn_llvm_value cn_llvm_emit_extractvalue(
    cn_llvm_function_ctx *ctx,
    cn_llvm_value aggregate,
    const cn_ir_type *element_type,
    size_t index
) {
    int reg_id = cn_llvm_next_temp(ctx);
    cn_llvm_emit_indent(ctx->emit->stream);
    cn_llvm_emit_reg(ctx->emit->stream, reg_id);
    fputs(" = extractvalue ", ctx->emit->stream);
    cn_llvm_emit_type(ctx->emit->stream, aggregate.type);
    fputc(' ', ctx->emit->stream);
    cn_llvm_emit_value_ref(ctx->emit->stream, aggregate);
    fprintf(ctx->emit->stream, ", %zu\n", index);
    return cn_llvm_register_value(element_type, reg_id);
}

static cn_llvm_value cn_llvm_emit_bool_and(cn_llvm_function_ctx *ctx, cn_llvm_value left, cn_llvm_value right) {
    int reg_id = cn_llvm_next_temp(ctx);
    cn_llvm_emit_indent(ctx->emit->stream);
    cn_llvm_emit_reg(ctx->emit->stream, reg_id);
    fputs(" = and i1 ", ctx->emit->stream);
    cn_llvm_emit_value_ref(ctx->emit->stream, left);
    fputs(", ", ctx->emit->stream);
    cn_llvm_emit_value_ref(ctx->emit->stream, right);
    fputc('\n', ctx->emit->stream);
    return cn_llvm_register_value(&g_llvm_bool_type, reg_id);
}

static cn_llvm_value cn_llvm_emit_bool_not(cn_llvm_function_ctx *ctx, cn_llvm_value value) {
    if (value.is_constant) {
        return cn_llvm_constant_bool(&g_llvm_bool_type, !value.data.bool_value);
    }

    int reg_id = cn_llvm_next_temp(ctx);
    cn_llvm_emit_indent(ctx->emit->stream);
    cn_llvm_emit_reg(ctx->emit->stream, reg_id);
    fputs(" = xor i1 ", ctx->emit->stream);
    cn_llvm_emit_value_ref(ctx->emit->stream, value);
    fputs(", true\n", ctx->emit->stream);
    return cn_llvm_register_value(&g_llvm_bool_type, reg_id);
}

static cn_llvm_value cn_llvm_emit_int_sub(cn_llvm_function_ctx *ctx, cn_llvm_value left, cn_llvm_value right) {
    if (left.is_constant && right.is_constant) {
        return cn_llvm_constant_int(&g_llvm_int_type, left.data.int_value - right.data.int_value);
    }

    int reg_id = cn_llvm_next_temp(ctx);
    cn_llvm_emit_indent(ctx->emit->stream);
    cn_llvm_emit_reg(ctx->emit->stream, reg_id);
    fputs(" = sub i64 ", ctx->emit->stream);
    cn_llvm_emit_value_ref(ctx->emit->stream, left);
    fputs(", ", ctx->emit->stream);
    cn_llvm_emit_value_ref(ctx->emit->stream, right);
    fputc('\n', ctx->emit->stream);
    return cn_llvm_register_value(&g_llvm_int_type, reg_id);
}

static cn_llvm_value cn_llvm_emit_value_equality(
    cn_llvm_function_ctx *ctx,
    cn_llvm_value left,
    cn_llvm_value right,
    const cn_ir_type *type,
    size_t offset
) {
    switch (type->kind) {
    case CN_IR_TYPE_INT:
    case CN_IR_TYPE_U8:
    case CN_IR_TYPE_BOOL:
    case CN_IR_TYPE_PTR: {
        int reg_id = cn_llvm_next_temp(ctx);
        cn_llvm_emit_indent(ctx->emit->stream);
        cn_llvm_emit_reg(ctx->emit->stream, reg_id);
        fputs(" = icmp eq ", ctx->emit->stream);
        cn_llvm_emit_type(ctx->emit->stream, type);
        fputc(' ', ctx->emit->stream);
        cn_llvm_emit_value_ref(ctx->emit->stream, left);
        fputs(", ", ctx->emit->stream);
        cn_llvm_emit_value_ref(ctx->emit->stream, right);
        fputc('\n', ctx->emit->stream);
        return cn_llvm_register_value(&g_llvm_bool_type, reg_id);
    }
    case CN_IR_TYPE_STR: {
        int strcmp_reg_id = cn_llvm_next_temp(ctx);
        int eq_reg_id = cn_llvm_next_temp(ctx);
        cn_llvm_emit_indent(ctx->emit->stream);
        cn_llvm_emit_reg(ctx->emit->stream, strcmp_reg_id);
        fputs(" = call i32 @strcmp(ptr ", ctx->emit->stream);
        cn_llvm_emit_value_ref(ctx->emit->stream, left);
        fputs(", ptr ", ctx->emit->stream);
        cn_llvm_emit_value_ref(ctx->emit->stream, right);
        fputs(")\n", ctx->emit->stream);
        cn_llvm_emit_indent(ctx->emit->stream);
        cn_llvm_emit_reg(ctx->emit->stream, eq_reg_id);
        fputs(" = icmp eq i32 ", ctx->emit->stream);
        cn_llvm_emit_reg(ctx->emit->stream, strcmp_reg_id);
        fputs(", 0\n", ctx->emit->stream);
        return cn_llvm_register_value(&g_llvm_bool_type, eq_reg_id);
    }
    case CN_IR_TYPE_RESULT: {
        cn_llvm_value ok_eq = cn_llvm_emit_value_equality(
            ctx,
            cn_llvm_emit_extractvalue(ctx, left, &g_llvm_bool_type, 0),
            cn_llvm_emit_extractvalue(ctx, right, &g_llvm_bool_type, 0),
            &g_llvm_bool_type,
            offset
        );
        cn_llvm_value value_eq = cn_llvm_emit_value_equality(
            ctx,
            cn_llvm_emit_extractvalue(ctx, left, type->inner, 1),
            cn_llvm_emit_extractvalue(ctx, right, type->inner, 1),
            type->inner,
            offset
        );
        if (!ok_eq.is_valid || !value_eq.is_valid) {
            return cn_llvm_invalid_value(&g_llvm_bool_type);
        }
        return cn_llvm_emit_bool_and(ctx, ok_eq, value_eq);
    }
    case CN_IR_TYPE_SLICE: {
        cn_ir_type ptr_type = {CN_IR_TYPE_PTR, {NULL, 0}, {NULL, 0}, (cn_ir_type *)type->inner, 0};
        cn_llvm_value ptr_eq = cn_llvm_emit_value_equality(
            ctx,
            cn_llvm_emit_extractvalue(ctx, left, &ptr_type, 0),
            cn_llvm_emit_extractvalue(ctx, right, &ptr_type, 0),
            &ptr_type,
            offset
        );
        cn_llvm_value len_eq = cn_llvm_emit_value_equality(
            ctx,
            cn_llvm_emit_extractvalue(ctx, left, &g_llvm_int_type, 1),
            cn_llvm_emit_extractvalue(ctx, right, &g_llvm_int_type, 1),
            &g_llvm_int_type,
            offset
        );
        if (!ptr_eq.is_valid || !len_eq.is_valid) {
            return cn_llvm_invalid_value(&g_llvm_bool_type);
        }
        return cn_llvm_emit_bool_and(ctx, ptr_eq, len_eq);
    }
    case CN_IR_TYPE_ARRAY: {
        cn_llvm_value result = cn_llvm_constant_bool(&g_llvm_bool_type, true);
        for (size_t i = 0; i < type->array_size; ++i) {
            cn_llvm_value item_eq = cn_llvm_emit_value_equality(
                ctx,
                cn_llvm_emit_extractvalue(ctx, left, type->inner, i),
                cn_llvm_emit_extractvalue(ctx, right, type->inner, i),
                type->inner,
                offset
            );
            if (!item_eq.is_valid) {
                return cn_llvm_invalid_value(&g_llvm_bool_type);
            }
            result = result.is_constant && result.data.bool_value
                ? item_eq
                : cn_llvm_emit_bool_and(ctx, result, item_eq);
        }
        return result;
    }
    case CN_IR_TYPE_NAMED: {
        const cn_ir_struct *struct_decl = cn_llvm_find_struct(ctx->emit->program, type->module_name, type->name);
        cn_llvm_value result = cn_llvm_constant_bool(&g_llvm_bool_type, true);

        if (struct_decl == NULL) {
            cn_diag_emit(
                ctx->emit->diagnostics,
                CN_DIAG_ERROR,
                "E3020",
                offset,
                "llvm backend lost a checked struct type during equality lowering"
            );
            return cn_llvm_invalid_value(&g_llvm_bool_type);
        }

        for (size_t i = 0; i < struct_decl->fields.count; ++i) {
            cn_llvm_value field_eq = cn_llvm_emit_value_equality(
                ctx,
                cn_llvm_emit_extractvalue(ctx, left, struct_decl->fields.items[i].type, i),
                cn_llvm_emit_extractvalue(ctx, right, struct_decl->fields.items[i].type, i),
                struct_decl->fields.items[i].type,
                offset
            );
            if (!field_eq.is_valid) {
                return cn_llvm_invalid_value(&g_llvm_bool_type);
            }
            result = result.is_constant && result.data.bool_value
                ? field_eq
                : cn_llvm_emit_bool_and(ctx, result, field_eq);
        }
        return result;
    }
    case CN_IR_TYPE_VOID:
    case CN_IR_TYPE_UNKNOWN:
        break;
    }

    cn_llvm_emit_unsupported_feature(ctx->emit->diagnostics, offset, "equality on this value type");
    return cn_llvm_invalid_value(&g_llvm_bool_type);
}

static cn_llvm_value cn_llvm_emit_sizeof(cn_llvm_function_ctx *ctx, const cn_ir_type *type) {
    int gep_reg_id = cn_llvm_next_temp(ctx);
    int size_reg_id = cn_llvm_next_temp(ctx);

    cn_llvm_emit_indent(ctx->emit->stream);
    cn_llvm_emit_reg(ctx->emit->stream, gep_reg_id);
    fputs(" = getelementptr ", ctx->emit->stream);
    cn_llvm_emit_type(ctx->emit->stream, type);
    fputs(", ptr null, i64 1\n", ctx->emit->stream);

    cn_llvm_emit_indent(ctx->emit->stream);
    cn_llvm_emit_reg(ctx->emit->stream, size_reg_id);
    fputs(" = ptrtoint ptr ", ctx->emit->stream);
    cn_llvm_emit_reg(ctx->emit->stream, gep_reg_id);
    fputs(" to i64\n", ctx->emit->stream);

    return cn_llvm_register_value(&g_llvm_int_type, size_reg_id);
}

static const cn_ir_type *cn_llvm_resolve_field_type(
    cn_llvm_emit_ctx *ctx,
    const cn_ir_type *base_type,
    cn_strview field_name,
    size_t *out_index
) {
    if (base_type->kind == CN_IR_TYPE_RESULT) {
        if (cn_sv_eq_cstr(field_name, "ok")) {
            if (out_index != NULL) {
                *out_index = 0;
            }
            return &g_llvm_bool_type;
        }
        if (cn_sv_eq_cstr(field_name, "value")) {
            if (out_index != NULL) {
                *out_index = 1;
            }
            return base_type->inner;
        }
        return NULL;
    }

    if (base_type->kind == CN_IR_TYPE_SLICE) {
        if (cn_sv_eq_cstr(field_name, "length")) {
            if (out_index != NULL) {
                *out_index = 1;
            }
            return &g_llvm_int_type;
        }
        return NULL;
    }

    if (base_type->kind != CN_IR_TYPE_NAMED) {
        return NULL;
    }

    const cn_ir_struct *struct_decl = cn_llvm_find_struct(ctx->program, base_type->module_name, base_type->name);
    if (struct_decl == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < struct_decl->fields.count; ++i) {
        if (cn_sv_eq(struct_decl->fields.items[i].name, field_name)) {
            if (out_index != NULL) {
                *out_index = i;
            }
            return struct_decl->fields.items[i].type;
        }
    }

    return NULL;
}

static cn_llvm_address cn_llvm_materialize_address(
    cn_llvm_function_ctx *ctx,
    cn_llvm_scope *scope,
    const cn_ir_expr *expression
) {
    if (expression->kind == CN_IR_EXPR_LOCAL ||
        expression->kind == CN_IR_EXPR_FIELD ||
        expression->kind == CN_IR_EXPR_INDEX ||
        expression->kind == CN_IR_EXPR_DEREF) {
        return cn_llvm_lower_address(ctx, scope, expression, false);
    }

    cn_llvm_value value = cn_llvm_lower_expression(ctx, scope, expression);
    if (!value.is_valid) {
        return cn_llvm_invalid_address(expression->type);
    }

    int alloca_id = cn_llvm_emit_alloca(ctx, expression->type);
    cn_llvm_emit_store(ctx, value, alloca_id);
    return cn_llvm_register_address(expression->type, alloca_id);
}

static cn_llvm_address cn_llvm_lower_address(
    cn_llvm_function_ctx *ctx,
    cn_llvm_scope *scope,
    const cn_ir_expr *expression,
    bool require_addressable
) {
    switch (expression->kind) {
    case CN_IR_EXPR_LOCAL: {
        const cn_llvm_binding *binding = cn_llvm_scope_lookup(scope, expression->data.local_name);
        if (binding == NULL) {
            cn_diag_emit(
                ctx->emit->diagnostics,
                CN_DIAG_ERROR,
                "E3020",
                expression->offset,
                "llvm backend lost local binding '%.*s'",
                (int)expression->data.local_name.length,
                expression->data.local_name.data
            );
            return cn_llvm_invalid_address(expression->type);
        }

        return cn_llvm_register_address(binding->type, binding->alloca_id);
    }
    case CN_IR_EXPR_FIELD: {
        const cn_ir_type *base_type = expression->data.field.base->type;
        if (base_type->kind == CN_IR_TYPE_PTR) {
            cn_llvm_value base_value = cn_llvm_lower_expression(ctx, scope, expression->data.field.base);
            if (!base_value.is_valid) {
                return cn_llvm_invalid_address(expression->type);
            }
            return cn_llvm_register_address(base_type->inner, base_value.data.reg_id);
        }

        cn_llvm_address base_address = cn_llvm_materialize_address(ctx, scope, expression->data.field.base);
        if (!base_address.is_valid) {
            return cn_llvm_invalid_address(expression->type);
        }

        size_t field_index = 0;
        const cn_ir_type *field_type = cn_llvm_resolve_field_type(
            ctx->emit,
            expression->data.field.base->type,
            expression->data.field.field_name,
            &field_index
        );
        if (field_type == NULL) {
            cn_diag_emit(
                ctx->emit->diagnostics,
                CN_DIAG_ERROR,
                "E3020",
                expression->offset,
                "llvm backend lost checked field access '%.*s'",
                (int)expression->data.field.field_name.length,
                expression->data.field.field_name.data
            );
            return cn_llvm_invalid_address(expression->type);
        }

        return cn_llvm_register_address(
            field_type,
            cn_llvm_emit_struct_field_gep(ctx, expression->data.field.base->type, base_address.pointer_reg_id, field_index)
        );
    }
    case CN_IR_EXPR_DEREF: {
        cn_llvm_value target = cn_llvm_lower_expression(ctx, scope, expression->data.deref_expr.target);
        if (!target.is_valid) {
            return cn_llvm_invalid_address(expression->type);
        }

        return cn_llvm_register_address(expression->type, target.data.reg_id);
    }
    case CN_IR_EXPR_INDEX: {
        cn_llvm_value index_value = cn_llvm_lower_expression(ctx, scope, expression->data.index.index);
        if (!index_value.is_valid) {
            return cn_llvm_invalid_address(expression->type);
        }

        if (expression->data.index.base->type->kind == CN_IR_TYPE_SLICE) {
            cn_llvm_value slice_value = cn_llvm_lower_expression(ctx, scope, expression->data.index.base);
            if (!slice_value.is_valid) {
                return cn_llvm_invalid_address(expression->type);
            }

            cn_ir_type ptr_type = {
                CN_IR_TYPE_PTR,
                {NULL, 0},
                {NULL, 0},
                (cn_ir_type *)expression->data.index.base->type->inner,
                0
            };
            cn_llvm_value data_ptr = cn_llvm_emit_extractvalue(ctx, slice_value, &ptr_type, 0);
            return cn_llvm_register_address(
                expression->type,
                cn_llvm_emit_pointer_element_gep(ctx, expression->type, data_ptr, index_value)
            );
        }

        cn_llvm_address base_address = cn_llvm_materialize_address(ctx, scope, expression->data.index.base);
        if (!base_address.is_valid) {
            return cn_llvm_invalid_address(expression->type);
        }

        return cn_llvm_register_address(
            expression->type,
            cn_llvm_emit_array_element_gep(ctx, expression->data.index.base->type, base_address.pointer_reg_id, index_value)
        );
    }
    default:
        if (require_addressable) {
            cn_diag_emit(
                ctx->emit->diagnostics,
                CN_DIAG_ERROR,
                "E3020",
                expression->offset,
                "llvm backend expected an addressable checked expression"
            );
            return cn_llvm_invalid_address(expression->type);
        }
        return cn_llvm_materialize_address(ctx, scope, expression);
    }
}

static cn_llvm_value cn_llvm_lower_short_circuit(
    cn_llvm_function_ctx *ctx,
    cn_llvm_scope *scope,
    const cn_ir_expr *left_expr,
    const cn_ir_expr *right_expr,
    bool is_and
) {
    const cn_ir_type *bool_type = left_expr->type;
    int result_ptr = cn_llvm_emit_alloca(ctx, bool_type);
    int rhs_label = cn_llvm_next_label(ctx);
    int false_or_true_label = cn_llvm_next_label(ctx);
    int merge_label = cn_llvm_next_label(ctx);

    cn_llvm_value left = cn_llvm_lower_expression(ctx, scope, left_expr);
    if (!left.is_valid) {
        return cn_llvm_invalid_value(bool_type);
    }

    if (is_and) {
        cn_llvm_emit_cond_br(ctx, left, rhs_label, false_or_true_label);
    } else {
        cn_llvm_emit_cond_br(ctx, left, false_or_true_label, rhs_label);
    }

    cn_llvm_emit_label(ctx, rhs_label);
    cn_llvm_value right = cn_llvm_lower_expression(ctx, scope, right_expr);
    if (!right.is_valid) {
        return cn_llvm_invalid_value(bool_type);
    }
    cn_llvm_emit_store(ctx, right, result_ptr);
    cn_llvm_emit_br(ctx, merge_label);

    cn_llvm_emit_label(ctx, false_or_true_label);
    cn_llvm_emit_store(ctx, cn_llvm_constant_bool(bool_type, !is_and), result_ptr);
    cn_llvm_emit_br(ctx, merge_label);

    cn_llvm_emit_label(ctx, merge_label);
    return cn_llvm_emit_load(ctx, bool_type, result_ptr);
}

static cn_llvm_value cn_llvm_lower_if_expression(
    cn_llvm_function_ctx *ctx,
    cn_llvm_scope *scope,
    const cn_ir_expr *expression
) {
    int result_ptr = cn_llvm_emit_alloca(ctx, expression->type);
    int then_label = cn_llvm_next_label(ctx);
    int else_label = cn_llvm_next_label(ctx);
    int merge_label = cn_llvm_next_label(ctx);

    cn_llvm_value condition = cn_llvm_lower_expression(ctx, scope, expression->data.if_expr.condition);
    if (!condition.is_valid) {
        return cn_llvm_invalid_value(expression->type);
    }

    cn_llvm_emit_cond_br(ctx, condition, then_label, else_label);

    cn_llvm_emit_label(ctx, then_label);
    cn_llvm_value then_value = cn_llvm_lower_expression(ctx, scope, expression->data.if_expr.then_expr);
    if (!then_value.is_valid) {
        return cn_llvm_invalid_value(expression->type);
    }
    cn_llvm_emit_store(ctx, then_value, result_ptr);
    cn_llvm_emit_br(ctx, merge_label);

    cn_llvm_emit_label(ctx, else_label);
    cn_llvm_value else_value = cn_llvm_lower_expression(ctx, scope, expression->data.if_expr.else_expr);
    if (!else_value.is_valid) {
        return cn_llvm_invalid_value(expression->type);
    }
    cn_llvm_emit_store(ctx, else_value, result_ptr);
    cn_llvm_emit_br(ctx, merge_label);

    cn_llvm_emit_label(ctx, merge_label);
    return cn_llvm_emit_load(ctx, expression->type, result_ptr);
}

static const cn_llvm_string_entry *cn_llvm_find_string(const cn_llvm_emit_ctx *ctx, cn_strview value) {
    for (const cn_llvm_string_entry *entry = ctx->strings; entry != NULL; entry = entry->next) {
        if (cn_sv_eq(entry->value, value)) {
            return entry;
        }
    }
    return NULL;
}

static cn_llvm_value cn_llvm_lower_string_literal(cn_llvm_function_ctx *ctx, const cn_ir_expr *expression) {
    const cn_llvm_string_entry *entry = cn_llvm_find_string(ctx->emit, expression->data.string_value);
    if (entry == NULL) {
        cn_diag_emit(
            ctx->emit->diagnostics,
            CN_DIAG_ERROR,
            "E3020",
            expression->offset,
            "llvm backend lost a collected string literal"
        );
        return cn_llvm_invalid_value(expression->type);
    }

    int reg_id = cn_llvm_next_temp(ctx);
    cn_llvm_emit_indent(ctx->emit->stream);
    cn_llvm_emit_reg(ctx->emit->stream, reg_id);
    fprintf(
        ctx->emit->stream,
        " = getelementptr inbounds [%zu x i8], ptr @.cn.str.%d, i64 0, i64 0\n",
        entry->value.length + 1,
        entry->global_id
    );
    return cn_llvm_register_value(&g_llvm_str_type, reg_id);
}

static cn_llvm_value cn_llvm_lower_result_ok(cn_llvm_function_ctx *ctx, cn_llvm_scope *scope, const cn_ir_expr *expression) {
    cn_llvm_value inner_value = cn_llvm_lower_expression(ctx, scope, expression->data.ok_expr.value);
    if (!inner_value.is_valid) {
        return cn_llvm_invalid_value(expression->type);
    }

    int ok_reg = cn_llvm_emit_insertvalue(
        ctx,
        expression->type,
        true,
        0,
        cn_llvm_constant_bool(&g_llvm_bool_type, true),
        0
    );
    int value_reg = cn_llvm_emit_insertvalue(ctx, expression->type, false, ok_reg, inner_value, 1);
    return cn_llvm_register_value(expression->type, value_reg);
}

static cn_llvm_value cn_llvm_lower_result_err(cn_llvm_function_ctx *ctx, const cn_ir_expr *expression) {
    int reg_id = cn_llvm_emit_insertvalue(
        ctx,
        expression->type,
        true,
        0,
        cn_llvm_constant_bool(&g_llvm_bool_type, false),
        0
    );
    return cn_llvm_register_value(expression->type, reg_id);
}

static cn_llvm_value cn_llvm_lower_array_literal(cn_llvm_function_ctx *ctx, cn_llvm_scope *scope, const cn_ir_expr *expression) {
    int aggregate_reg_id = -1;
    bool have_value = false;

    for (size_t i = 0; i < expression->data.array_literal.items.count; ++i) {
        cn_llvm_value item = cn_llvm_lower_expression(ctx, scope, expression->data.array_literal.items.items[i]);
        if (!item.is_valid) {
            return cn_llvm_invalid_value(expression->type);
        }

        aggregate_reg_id = cn_llvm_emit_insertvalue(
            ctx,
            expression->type,
            !have_value,
            aggregate_reg_id,
            item,
            i
        );
        have_value = true;
    }

    if (!have_value) {
        cn_llvm_emit_unsupported_feature(ctx->emit->diagnostics, expression->offset, "empty array literals");
        return cn_llvm_invalid_value(expression->type);
    }

    return cn_llvm_register_value(expression->type, aggregate_reg_id);
}

static cn_llvm_value cn_llvm_lower_slice_from_array(cn_llvm_function_ctx *ctx, cn_llvm_scope *scope, const cn_ir_expr *expression) {
    cn_llvm_address base_address = cn_llvm_materialize_address(ctx, scope, expression->data.slice_from_array.base);
    if (!base_address.is_valid) {
        return cn_llvm_invalid_value(expression->type);
    }

    cn_llvm_value zero = cn_llvm_constant_int(&g_llvm_int_type, 0);
    int data_reg_id = cn_llvm_emit_array_element_gep(ctx, expression->data.slice_from_array.base->type, base_address.pointer_reg_id, zero);
    cn_ir_type ptr_type = {
        CN_IR_TYPE_PTR,
        {NULL, 0},
        {NULL, 0},
        (cn_ir_type *)expression->type->inner,
        0
    };
    cn_llvm_value data_ptr = cn_llvm_register_value(&ptr_type, data_reg_id);
    cn_llvm_value length = cn_llvm_constant_int(&g_llvm_int_type, (int64_t)expression->data.slice_from_array.base->type->array_size);

    int ptr_reg = cn_llvm_emit_insertvalue(ctx, expression->type, true, 0, data_ptr, 0);
    int len_reg = cn_llvm_emit_insertvalue(ctx, expression->type, false, ptr_reg, length, 1);
    return cn_llvm_register_value(expression->type, len_reg);
}

static cn_llvm_value cn_llvm_lower_slice_view(cn_llvm_function_ctx *ctx, cn_llvm_scope *scope, const cn_ir_expr *expression) {
    cn_llvm_value start = expression->data.slice_view.start != NULL
        ? cn_llvm_lower_expression(ctx, scope, expression->data.slice_view.start)
        : cn_llvm_constant_int(&g_llvm_int_type, 0);
    if (!start.is_valid) {
        return cn_llvm_invalid_value(expression->type);
    }

    cn_ir_type ptr_type = {
        CN_IR_TYPE_PTR,
        {NULL, 0},
        {NULL, 0},
        (cn_ir_type *)expression->type->inner,
        0
    };

    cn_llvm_value data_ptr;
    cn_llvm_value end;

    if (expression->data.slice_view.base->type->kind == CN_IR_TYPE_ARRAY) {
        cn_llvm_address base_address = cn_llvm_materialize_address(ctx, scope, expression->data.slice_view.base);
        if (!base_address.is_valid) {
            return cn_llvm_invalid_value(expression->type);
        }

        int data_reg_id = cn_llvm_emit_array_element_gep(
            ctx,
            expression->data.slice_view.base->type,
            base_address.pointer_reg_id,
            start
        );
        data_ptr = cn_llvm_register_value(&ptr_type, data_reg_id);
        end = expression->data.slice_view.end != NULL
            ? cn_llvm_lower_expression(ctx, scope, expression->data.slice_view.end)
            : cn_llvm_constant_int(&g_llvm_int_type, (int64_t)expression->data.slice_view.base->type->array_size);
    } else {
        cn_llvm_value base_slice = cn_llvm_lower_expression(ctx, scope, expression->data.slice_view.base);
        if (!base_slice.is_valid) {
            return cn_llvm_invalid_value(expression->type);
        }

        cn_llvm_value base_ptr = cn_llvm_emit_extractvalue(ctx, base_slice, &ptr_type, 0);
        data_ptr = cn_llvm_register_value(
            &ptr_type,
            cn_llvm_emit_pointer_element_gep(ctx, expression->type->inner, base_ptr, start)
        );
        end = expression->data.slice_view.end != NULL
            ? cn_llvm_lower_expression(ctx, scope, expression->data.slice_view.end)
            : cn_llvm_emit_extractvalue(ctx, base_slice, &g_llvm_int_type, 1);
    }

    if (!data_ptr.is_valid || !end.is_valid) {
        return cn_llvm_invalid_value(expression->type);
    }

    cn_llvm_value length = cn_llvm_emit_int_sub(ctx, end, start);
    int ptr_reg = cn_llvm_emit_insertvalue(ctx, expression->type, true, 0, data_ptr, 0);
    int len_reg = cn_llvm_emit_insertvalue(ctx, expression->type, false, ptr_reg, length, 1);
    return cn_llvm_register_value(expression->type, len_reg);
}

static cn_llvm_value cn_llvm_lower_struct_literal(cn_llvm_function_ctx *ctx, cn_llvm_scope *scope, const cn_ir_expr *expression) {
    int aggregate_reg_id = -1;
    bool have_value = false;
    const cn_ir_struct *struct_decl = cn_llvm_find_struct(
        ctx->emit->program,
        expression->data.struct_literal.module_name,
        expression->data.struct_literal.type_name
    );

    if (struct_decl == NULL) {
        cn_diag_emit(
            ctx->emit->diagnostics,
            CN_DIAG_ERROR,
            "E3020",
            expression->offset,
            "llvm backend lost a checked struct definition"
        );
        return cn_llvm_invalid_value(expression->type);
    }

    for (size_t i = 0; i < expression->data.struct_literal.fields.count; ++i) {
        const cn_ir_field_init *field_init = &expression->data.struct_literal.fields.items[i];
        size_t field_index = struct_decl->fields.count;
        for (size_t j = 0; j < struct_decl->fields.count; ++j) {
            if (cn_sv_eq(struct_decl->fields.items[j].name, field_init->name)) {
                field_index = j;
                break;
            }
        }

        if (field_index == struct_decl->fields.count) {
            cn_diag_emit(
                ctx->emit->diagnostics,
                CN_DIAG_ERROR,
                "E3020",
                field_init->value->offset,
                "llvm backend lost a checked struct field '%.*s'",
                (int)field_init->name.length,
                field_init->name.data
            );
            return cn_llvm_invalid_value(expression->type);
        }

        cn_llvm_value field_value = cn_llvm_lower_expression(ctx, scope, field_init->value);
        if (!field_value.is_valid) {
            return cn_llvm_invalid_value(expression->type);
        }

        aggregate_reg_id = cn_llvm_emit_insertvalue(
            ctx,
            expression->type,
            !have_value,
            aggregate_reg_id,
            field_value,
            field_index
        );
        have_value = true;
    }

    if (!have_value) {
        cn_llvm_emit_unsupported_feature(ctx->emit->diagnostics, expression->offset, "empty struct literals");
        return cn_llvm_invalid_value(expression->type);
    }

    return cn_llvm_register_value(expression->type, aggregate_reg_id);
}

static bool cn_llvm_lower_builtin_arguments(
    cn_llvm_function_ctx *ctx,
    cn_llvm_scope *scope,
    const cn_ir_expr *expression,
    cn_llvm_value *arguments,
    size_t max_count
) {
    if (expression->data.call.arguments.count > max_count) {
        cn_llvm_emit_unsupported_feature(ctx->emit->diagnostics, expression->offset, "builtin calls with too many arguments");
        return false;
    }

    for (size_t i = 0; i < expression->data.call.arguments.count; ++i) {
        arguments[i] = cn_llvm_lower_expression(ctx, scope, expression->data.call.arguments.items[i]);
        if (!arguments[i].is_valid) {
            return false;
        }
    }

    return true;
}

static cn_llvm_value cn_llvm_emit_named_call(
    cn_llvm_function_ctx *ctx,
    const cn_ir_type *result_type,
    const char *symbol,
    cn_llvm_value *arguments,
    size_t argument_count
) {
    if (result_type->kind == CN_IR_TYPE_VOID) {
        cn_llvm_emit_indent(ctx->emit->stream);
        fputs("call void ", ctx->emit->stream);
        fputs(symbol, ctx->emit->stream);
        fputc('(', ctx->emit->stream);
        for (size_t i = 0; i < argument_count; ++i) {
            if (i > 0) {
                fputs(", ", ctx->emit->stream);
            }
            cn_llvm_emit_type(ctx->emit->stream, arguments[i].type);
            fputc(' ', ctx->emit->stream);
            cn_llvm_emit_value_ref(ctx->emit->stream, arguments[i]);
        }
        fputs(")\n", ctx->emit->stream);
        return cn_llvm_void_value(result_type);
    }

    int reg_id = cn_llvm_next_temp(ctx);
    cn_llvm_emit_indent(ctx->emit->stream);
    cn_llvm_emit_reg(ctx->emit->stream, reg_id);
    fputs(" = call ", ctx->emit->stream);
    cn_llvm_emit_type(ctx->emit->stream, result_type);
    fputc(' ', ctx->emit->stream);
    fputs(symbol, ctx->emit->stream);
    fputc('(', ctx->emit->stream);
    for (size_t i = 0; i < argument_count; ++i) {
        if (i > 0) {
            fputs(", ", ctx->emit->stream);
        }
        cn_llvm_emit_type(ctx->emit->stream, arguments[i].type);
        fputc(' ', ctx->emit->stream);
        cn_llvm_emit_value_ref(ctx->emit->stream, arguments[i]);
    }
    fputs(")\n", ctx->emit->stream);
    return cn_llvm_register_value(result_type, reg_id);
}

static cn_llvm_value cn_llvm_lower_builtin_call(cn_llvm_function_ctx *ctx, cn_llvm_scope *scope, const cn_ir_expr *expression) {
    cn_llvm_value arguments[4];

    if (!cn_llvm_lower_builtin_arguments(ctx, scope, expression, arguments, CN_ARRAY_LEN(arguments))) {
        return cn_llvm_invalid_value(expression->type);
    }

    if (cn_llvm_call_matches(expression, NULL, "input")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_input", arguments, 0);
    }

    if (cn_llvm_call_matches(expression, "std.io", "read_line")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_input", arguments, 0);
    }

    if (cn_llvm_call_matches(expression, "std.bytes", "new")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_bytes_new", arguments, 0);
    }

    if (cn_llvm_call_matches(expression, "std.lines", "new")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_lines_new", arguments, 0);
    }

    if (cn_llvm_call_matches(expression, "std.ipc", "spawn")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_ipc_spawn", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.term", "is_tty")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_is_tty", arguments, 0);
    }

    if (cn_llvm_call_matches(expression, "std.term", "columns")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_columns", arguments, 0);
    }

    if (cn_llvm_call_matches(expression, "std.term", "rows")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_rows", arguments, 0);
    }

    if (cn_llvm_call_matches(expression, "std.term", "term_name")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_term_name", arguments, 0);
    }

    if (cn_llvm_call_matches(expression, "std.term", "supports_truecolor")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_supports_truecolor", arguments, 0);
    }

    if (cn_llvm_call_matches(expression, "std.term", "supports_256color")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_supports_256color", arguments, 0);
    }

    if (cn_llvm_call_matches(expression, "std.term", "supports_unicode")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_supports_unicode", arguments, 0);
    }

    if (cn_llvm_call_matches(expression, "std.term", "supports_mouse")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_supports_mouse", arguments, 0);
    }

    if (cn_llvm_call_matches(expression, "std.term", "read_byte")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_read_byte", arguments, 0);
    }

    if (cn_llvm_call_matches(expression, "std.term", "read_byte_timeout")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_read_byte_timeout", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.term", "read_event")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_read_event", arguments, 0);
    }

    if (cn_llvm_call_matches(expression, "std.term", "read_event_timeout")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_read_event_timeout", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.term", "read_paste")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_read_paste", arguments, 0);
    }

    if (cn_llvm_call_matches(expression, "std.term", "codepoint_width")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_codepoint_width", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.term", "string_width")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_string_width", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.term", "decode_codepoint")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_decode_codepoint", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.term", "next_codepoint_offset")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_next_codepoint_offset", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.term", "reset_style")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_reset_style", arguments, 0);
    }

    if (cn_llvm_call_matches(expression, "std.term", "enable_mouse")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_enable_mouse", arguments, 0);
    }

    if (cn_llvm_call_matches(expression, "std.term", "disable_mouse")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_disable_mouse", arguments, 0);
    }

    if (cn_llvm_call_matches(expression, "std.term", "enable_bracketed_paste")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_enable_bracketed_paste", arguments, 0);
    }

    if (cn_llvm_call_matches(expression, "std.term", "disable_bracketed_paste")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_disable_bracketed_paste", arguments, 0);
    }

    if (cn_llvm_call_matches(expression, "std.process", "platform")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_process_platform", arguments, 0);
    }

    if (cn_llvm_call_matches(expression, "std.process", "arch")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_process_arch", arguments, 0);
    }

    if (cn_llvm_call_matches(expression, "std.text", "new")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_text_new", arguments, 0);
    }

    if (cn_llvm_call_matches(expression, "std.time", "now_ms")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_time_now_ms", arguments, 0);
    }

    if (cn_llvm_call_matches(expression, "std.fs", "cwd")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_fs_cwd", arguments, 0);
    }

    if (cn_llvm_call_matches(expression, NULL, "str_copy") ||
        cn_llvm_call_matches(expression, "std.strings", "copy")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_dup_cstr", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, NULL, "str_concat") ||
        cn_llvm_call_matches(expression, "std.strings", "concat")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_concat_str", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.math", "abs")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_math_abs", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.math", "sign")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_math_sign", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.math", "square")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_math_square", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.math", "cube")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_math_cube", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.math", "is_even")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_math_is_even", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.math", "is_odd")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_math_is_odd", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.bytes", "with_capacity")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_bytes_with_capacity", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.bytes", "release")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_bytes_free", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.bytes", "clear")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_bytes_clear", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.bytes", "length")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_bytes_length", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.bytes", "capacity")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_bytes_capacity", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.bytes", "view")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_bytes_slice", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.lines", "with_capacity")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_lines_with_capacity", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.lines", "release")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_lines_free", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.lines", "clear")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_lines_clear", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.lines", "length")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_lines_length", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.lines", "capacity")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_lines_capacity", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.ipc", "stdin_close")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_ipc_stdin_close", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.ipc", "wait")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_ipc_wait", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.ipc", "kill")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_ipc_kill", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.ipc", "release")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_ipc_release", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.strings", "len")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@strlen", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.strings", "eq")) {
        return cn_llvm_emit_value_equality(ctx, arguments[0], arguments[1], &g_llvm_str_type, expression->offset);
    }

    if (cn_llvm_call_matches(expression, "std.strings", "starts_with")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_starts_with", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.strings", "ends_with")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_ends_with", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.parse", "to_int")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_parse_int", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.parse", "to_bool")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_parse_bool", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.math", "min")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_math_min", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.math", "max")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_math_max", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.math", "clamp")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_math_clamp", arguments, 3);
    }

    if (cn_llvm_call_matches(expression, "std.math", "gcd")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_math_gcd", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.math", "lcm")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_math_lcm", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.math", "distance")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_math_distance", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.math", "between")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_math_between", arguments, 3);
    }

    if (cn_llvm_call_matches(expression, "std.x11", "open_window")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_x11_open_window", arguments, 3);
    }

    if (cn_llvm_call_matches(expression, "std.x11", "pump")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_x11_pump", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.x11", "close")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_x11_close", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.net", "is_ipv4")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_net_is_ipv4", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.net", "accept")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_net_accept", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.net", "close")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_net_close", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.env", "has")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_env_has", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.env", "get")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_env_get", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.fs", "exists")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_fs_exists", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.fs", "create_dir")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_fs_create_dir", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.fs", "remove_dir")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_fs_remove_dir", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.fs", "read_text")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_fs_read_text", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.fs", "file_size")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_fs_file_size", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.fs", "copy")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_fs_copy", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.fs", "write_text")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_fs_write_text", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.fs", "append_text")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_fs_append_text", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.fs", "rename")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_fs_rename", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.fs", "move")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_fs_rename", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.fs", "remove")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_fs_remove", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.io", "write")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_write_str", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.io", "write_line")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_print_str", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.term", "write")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_write_str", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.term", "buffer_free")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_buffer_free", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.term", "flush")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_flush", arguments, 0);
    }

    if (cn_llvm_call_matches(expression, "std.term", "clear")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_clear", arguments, 0);
    }

    if (cn_llvm_call_matches(expression, "std.term", "clear_line")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_clear_line", arguments, 0);
    }

    if (cn_llvm_call_matches(expression, "std.term", "clear_line_left")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_clear_line_left", arguments, 0);
    }

    if (cn_llvm_call_matches(expression, "std.term", "clear_line_right")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_clear_line_right", arguments, 0);
    }

    if (cn_llvm_call_matches(expression, "std.term", "save_cursor")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_save_cursor", arguments, 0);
    }

    if (cn_llvm_call_matches(expression, "std.term", "restore_cursor")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_restore_cursor", arguments, 0);
    }

    if (cn_llvm_call_matches(expression, "std.term", "hide_cursor")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_hide_cursor", arguments, 0);
    }

    if (cn_llvm_call_matches(expression, "std.term", "show_cursor")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_show_cursor", arguments, 0);
    }

    if (cn_llvm_call_matches(expression, "std.term", "enter_alt_screen")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_enter_alt_screen", arguments, 0);
    }

    if (cn_llvm_call_matches(expression, "std.term", "leave_alt_screen")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_leave_alt_screen", arguments, 0);
    }

    if (cn_llvm_call_matches(expression, "std.term", "reset_scroll_region")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_reset_scroll_region", arguments, 0);
    }

    if (cn_llvm_call_matches(expression, "std.term", "move_cursor")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_move_cursor", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.term", "set_scroll_region")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_set_scroll_region", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.term", "buffer_new")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_buffer_new", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.term", "buffer_resize")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_buffer_resize", arguments, 4);
    }

    if (cn_llvm_call_matches(expression, "std.term", "buffer_clear")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_buffer_clear", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.term", "render_diff")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_render_diff", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.term", "render_diff_clip")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_render_diff_clip", arguments, 3);
    }

    if (cn_llvm_call_matches(expression, "std.term", "enter_raw_mode")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_enter_raw_mode", arguments, 0);
    }

    if (cn_llvm_call_matches(expression, "std.term", "leave_raw_mode")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_leave_raw_mode", arguments, 0);
    }

    if (cn_llvm_call_matches(expression, "std.term", "set_style")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_set_style", arguments, 3);
    }

    if (cn_llvm_call_matches(expression, "std.term", "rgb")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_rgb", arguments, 3);
    }

    if (cn_llvm_call_matches(expression, "std.term", "buffer_get")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_buffer_get", arguments, 3);
    }

    if (cn_llvm_call_matches(expression, "std.term", "buffer_set")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_term_buffer_set", arguments, 4);
    }

    if (cn_llvm_call_matches(expression, "std.process", "exit")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_process_exit", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.time", "sleep_ms")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_time_sleep_ms", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.text", "with_capacity")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_text_with_capacity", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.text", "release")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_text_free", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.text", "clear")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_text_clear", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.text", "length")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_text_length", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.text", "capacity")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_text_capacity", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.text", "build")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_text_build", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.text", "view")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_text_slice", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.path", "join")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_path_join", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.bytes", "push")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_bytes_push", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.bytes", "append")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_bytes_append", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.bytes", "get")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_bytes_get", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.lines", "get")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_lines_get", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.ipc", "stdin_write")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_ipc_stdin_write", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.ipc", "stdin_write_line")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_ipc_stdin_write_line", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.ipc", "stdout_read")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_ipc_stdout_read", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.ipc", "stdout_read_line")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_ipc_stdout_read_line", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.ipc", "request_line")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_ipc_request_line", arguments, 3);
    }

    if (cn_llvm_call_matches(expression, "std.ipc", "stderr_read")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_ipc_stderr_read", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.ipc", "stderr_read_line")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_ipc_stderr_read_line", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.lines", "push")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_lines_push", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.lines", "remove")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_lines_remove", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.net", "join_host_port")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_net_join_host_port", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.net", "tcp_connect")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_net_tcp_connect", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.net", "tcp_listen")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_net_tcp_listen", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.net", "udp_bind")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_net_udp_bind", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.net", "send")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_net_send", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.net", "recv")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_net_recv", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.text", "append")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_text_append", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.text", "push_byte")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_text_push_byte", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.bytes", "set")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_bytes_set", arguments, 3);
    }

    if (cn_llvm_call_matches(expression, "std.lines", "set")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_lines_set", arguments, 3);
    }

    if (cn_llvm_call_matches(expression, "std.lines", "insert")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_lines_insert", arguments, 3);
    }

    if (cn_llvm_call_matches(expression, "std.net", "udp_recv_from")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_net_udp_recv_from", arguments, 2);
    }

    if (cn_llvm_call_matches(expression, "std.net", "udp_send_to")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_net_udp_send_to", arguments, 4);
    }

    if (cn_llvm_call_matches(expression, "std.path", "file_name")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_path_file_name", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.path", "stem")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_path_stem", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.path", "extension")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_path_extension", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.path", "is_absolute")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_path_is_absolute", arguments, 1);
    }

    if (cn_llvm_call_matches(expression, "std.path", "parent")) {
        return cn_llvm_emit_named_call(ctx, expression->type, "@cn_path_parent", arguments, 1);
    }

    if (!cn_llvm_call_matches(expression, NULL, "print")) {
        cn_llvm_emit_unsupported_feature(ctx->emit->diagnostics, expression->offset, "unknown builtin call target");
        return cn_llvm_invalid_value(expression->type);
    }

    cn_llvm_emit_indent(ctx->emit->stream);
    switch (arguments[0].type->kind) {
    case CN_IR_TYPE_INT:
        fputs("call void @cn_print_int(i64 ", ctx->emit->stream);
        break;
    case CN_IR_TYPE_U8: {
        int widened_reg_id = cn_llvm_next_temp(ctx);
        cn_llvm_emit_reg(ctx->emit->stream, widened_reg_id);
        fputs(" = zext i8 ", ctx->emit->stream);
        cn_llvm_emit_value_ref(ctx->emit->stream, arguments[0]);
        fputs(" to i64\n", ctx->emit->stream);
        cn_llvm_emit_indent(ctx->emit->stream);
        fputs("call void @cn_print_int(i64 ", ctx->emit->stream);
        cn_llvm_emit_reg(ctx->emit->stream, widened_reg_id);
        fputs(")\n", ctx->emit->stream);
        return cn_llvm_void_value(expression->type);
    }
    case CN_IR_TYPE_BOOL:
        fputs("call void @cn_print_bool(i1 ", ctx->emit->stream);
        break;
    case CN_IR_TYPE_STR:
        fputs("call void @cn_print_str(ptr ", ctx->emit->stream);
        break;
    default:
        cn_llvm_emit_unsupported_feature(ctx->emit->diagnostics, expression->offset, "print on non-display values");
        return cn_llvm_invalid_value(expression->type);
    }

    cn_llvm_emit_value_ref(ctx->emit->stream, arguments[0]);
    fputs(")\n", ctx->emit->stream);
    return cn_llvm_void_value(expression->type);
}

static cn_llvm_value cn_llvm_lower_call(cn_llvm_function_ctx *ctx, cn_llvm_scope *scope, const cn_ir_expr *expression) {
    if (expression->data.call.target_kind == CN_IR_CALL_BUILTIN) {
        return cn_llvm_lower_builtin_call(ctx, scope, expression);
    }

    cn_llvm_value arguments[32];
    if (expression->data.call.arguments.count > CN_ARRAY_LEN(arguments)) {
        cn_llvm_emit_unsupported_feature(ctx->emit->diagnostics, expression->offset, "function calls with more than 32 arguments");
        return cn_llvm_invalid_value(expression->type);
    }

    for (size_t i = 0; i < expression->data.call.arguments.count; ++i) {
        arguments[i] = cn_llvm_lower_expression(ctx, scope, expression->data.call.arguments.items[i]);
        if (!arguments[i].is_valid) {
            return cn_llvm_invalid_value(expression->type);
        }
    }

    if (expression->type->kind == CN_IR_TYPE_VOID) {
        cn_llvm_emit_indent(ctx->emit->stream);
        fputs("call void ", ctx->emit->stream);
    } else {
        int reg_id = cn_llvm_next_temp(ctx);
        cn_llvm_emit_indent(ctx->emit->stream);
        cn_llvm_emit_reg(ctx->emit->stream, reg_id);
        fputs(" = call ", ctx->emit->stream);
        cn_llvm_emit_type(ctx->emit->stream, expression->type);
        fputc(' ', ctx->emit->stream);
        cn_llvm_emit_function_symbol(
            ctx->emit->stream,
            expression->data.call.module_name,
            expression->data.call.function_name
        );
        fputc('(', ctx->emit->stream);
        for (size_t i = 0; i < expression->data.call.arguments.count; ++i) {
            if (i > 0) {
                fputs(", ", ctx->emit->stream);
            }
            cn_llvm_emit_type(ctx->emit->stream, arguments[i].type);
            fputc(' ', ctx->emit->stream);
            cn_llvm_emit_value_ref(ctx->emit->stream, arguments[i]);
        }
        fputs(")\n", ctx->emit->stream);
        return cn_llvm_register_value(expression->type, reg_id);
    }

    cn_llvm_emit_function_symbol(
        ctx->emit->stream,
        expression->data.call.module_name,
        expression->data.call.function_name
    );
    fputc('(', ctx->emit->stream);
    for (size_t i = 0; i < expression->data.call.arguments.count; ++i) {
        if (i > 0) {
            fputs(", ", ctx->emit->stream);
        }
        cn_llvm_emit_type(ctx->emit->stream, arguments[i].type);
        fputc(' ', ctx->emit->stream);
        cn_llvm_emit_value_ref(ctx->emit->stream, arguments[i]);
    }
    fputs(")\n", ctx->emit->stream);
    return cn_llvm_void_value(expression->type);
}

static cn_llvm_value cn_llvm_lower_expression(cn_llvm_function_ctx *ctx, cn_llvm_scope *scope, const cn_ir_expr *expression) {
    switch (expression->kind) {
    case CN_IR_EXPR_INT:
        return cn_llvm_constant_int(expression->type, expression->data.int_value);
    case CN_IR_EXPR_BOOL:
        return cn_llvm_constant_bool(expression->type, expression->data.bool_value);
    case CN_IR_EXPR_STRING:
        return cn_llvm_lower_string_literal(ctx, expression);
    case CN_IR_EXPR_LOCAL: {
        const cn_llvm_binding *binding = cn_llvm_scope_lookup(scope, expression->data.local_name);
        if (binding == NULL) {
            cn_diag_emit(
                ctx->emit->diagnostics,
                CN_DIAG_ERROR,
                "E3020",
                expression->offset,
                "llvm backend lost local binding '%.*s'",
                (int)expression->data.local_name.length,
                expression->data.local_name.data
            );
            return cn_llvm_invalid_value(expression->type);
        }

        return cn_llvm_emit_load(ctx, binding->type, binding->alloca_id);
    }
    case CN_IR_EXPR_UNARY: {
        cn_llvm_value operand = cn_llvm_lower_expression(ctx, scope, expression->data.unary.operand);
        if (!operand.is_valid) {
            return cn_llvm_invalid_value(expression->type);
        }

        int reg_id = cn_llvm_next_temp(ctx);
        cn_llvm_emit_indent(ctx->emit->stream);
        cn_llvm_emit_reg(ctx->emit->stream, reg_id);
        if (expression->data.unary.op == CN_IR_UNARY_NEGATE) {
            fputs(" = sub i64 0, ", ctx->emit->stream);
        } else {
            fputs(" = xor i1 ", ctx->emit->stream);
            cn_llvm_emit_value_ref(ctx->emit->stream, operand);
            fputs(", true\n", ctx->emit->stream);
            return cn_llvm_register_value(expression->type, reg_id);
        }
        cn_llvm_emit_value_ref(ctx->emit->stream, operand);
        fputc('\n', ctx->emit->stream);
        return cn_llvm_register_value(expression->type, reg_id);
    }
    case CN_IR_EXPR_BINARY: {
        if (expression->data.binary.op == CN_IR_BINARY_AND) {
            return cn_llvm_lower_short_circuit(ctx, scope, expression->data.binary.left, expression->data.binary.right, true);
        }
        if (expression->data.binary.op == CN_IR_BINARY_OR) {
            return cn_llvm_lower_short_circuit(ctx, scope, expression->data.binary.left, expression->data.binary.right, false);
        }

        cn_llvm_value left = cn_llvm_lower_expression(ctx, scope, expression->data.binary.left);
        cn_llvm_value right = cn_llvm_lower_expression(ctx, scope, expression->data.binary.right);
        if (!left.is_valid || !right.is_valid) {
            return cn_llvm_invalid_value(expression->type);
        }

        if (expression->data.binary.op == CN_IR_BINARY_EQUAL || expression->data.binary.op == CN_IR_BINARY_NOT_EQUAL) {
            cn_llvm_value eq_value = cn_llvm_emit_value_equality(ctx, left, right, left.type, expression->offset);
            if (!eq_value.is_valid) {
                return cn_llvm_invalid_value(expression->type);
            }
            if (expression->data.binary.op == CN_IR_BINARY_NOT_EQUAL) {
                return cn_llvm_emit_bool_not(ctx, eq_value);
            }
            return eq_value;
        }

        int reg_id = cn_llvm_next_temp(ctx);
        cn_llvm_emit_indent(ctx->emit->stream);
        cn_llvm_emit_reg(ctx->emit->stream, reg_id);

        switch (expression->data.binary.op) {
        case CN_IR_BINARY_ADD:
            fputs(" = add ", ctx->emit->stream);
            cn_llvm_emit_type(ctx->emit->stream, left.type);
            fputc(' ', ctx->emit->stream);
            break;
        case CN_IR_BINARY_SUB:
            fputs(" = sub ", ctx->emit->stream);
            cn_llvm_emit_type(ctx->emit->stream, left.type);
            fputc(' ', ctx->emit->stream);
            break;
        case CN_IR_BINARY_MUL:
            fputs(" = mul ", ctx->emit->stream);
            cn_llvm_emit_type(ctx->emit->stream, left.type);
            fputc(' ', ctx->emit->stream);
            break;
        case CN_IR_BINARY_DIV:
            fputs(" = sdiv ", ctx->emit->stream);
            cn_llvm_emit_type(ctx->emit->stream, left.type);
            fputc(' ', ctx->emit->stream);
            break;
        case CN_IR_BINARY_MOD:
            fputs(" = srem ", ctx->emit->stream);
            cn_llvm_emit_type(ctx->emit->stream, left.type);
            fputc(' ', ctx->emit->stream);
            break;
        case CN_IR_BINARY_EQUAL:
        case CN_IR_BINARY_NOT_EQUAL:
            break;
        case CN_IR_BINARY_LESS:
        case CN_IR_BINARY_LESS_EQUAL:
        case CN_IR_BINARY_GREATER:
        case CN_IR_BINARY_GREATER_EQUAL:
            fputs(cn_llvm_compare_prefix(expression->data.binary.op, left.type->kind), ctx->emit->stream);
            cn_llvm_emit_type(ctx->emit->stream, left.type);
            fputc(' ', ctx->emit->stream);
            break;
        case CN_IR_BINARY_AND:
        case CN_IR_BINARY_OR:
            break;
        }

        cn_llvm_emit_value_ref(ctx->emit->stream, left);
        fputs(", ", ctx->emit->stream);
        cn_llvm_emit_value_ref(ctx->emit->stream, right);
        fputc('\n', ctx->emit->stream);
        return cn_llvm_register_value(expression->type, reg_id);
    }
    case CN_IR_EXPR_IF:
        return cn_llvm_lower_if_expression(ctx, scope, expression);
    case CN_IR_EXPR_CALL:
        return cn_llvm_lower_call(ctx, scope, expression);
    case CN_IR_EXPR_ARRAY_LITERAL:
        return cn_llvm_lower_array_literal(ctx, scope, expression);
    case CN_IR_EXPR_SLICE_FROM_ARRAY:
        return cn_llvm_lower_slice_from_array(ctx, scope, expression);
    case CN_IR_EXPR_SLICE_VIEW:
        return cn_llvm_lower_slice_view(ctx, scope, expression);
    case CN_IR_EXPR_INDEX: {
        cn_llvm_address address = cn_llvm_lower_address(ctx, scope, expression, false);
        if (!address.is_valid) {
            return cn_llvm_invalid_value(expression->type);
        }
        return cn_llvm_emit_load(ctx, address.type, address.pointer_reg_id);
    }
    case CN_IR_EXPR_FIELD: {
        cn_llvm_address address = cn_llvm_lower_address(ctx, scope, expression, false);
        if (!address.is_valid) {
            return cn_llvm_invalid_value(expression->type);
        }
        return cn_llvm_emit_load(ctx, address.type, address.pointer_reg_id);
    }
    case CN_IR_EXPR_STRUCT_LITERAL:
        return cn_llvm_lower_struct_literal(ctx, scope, expression);
    case CN_IR_EXPR_OK:
        return cn_llvm_lower_result_ok(ctx, scope, expression);
    case CN_IR_EXPR_ERR:
        return cn_llvm_lower_result_err(ctx, expression);
    case CN_IR_EXPR_ALLOC: {
        cn_llvm_value size_value = cn_llvm_emit_sizeof(ctx, expression->data.alloc_expr.alloc_type);
        int reg_id = cn_llvm_next_temp(ctx);
        cn_llvm_emit_indent(ctx->emit->stream);
        cn_llvm_emit_reg(ctx->emit->stream, reg_id);
        fputs(" = call ptr @malloc(i64 ", ctx->emit->stream);
        cn_llvm_emit_value_ref(ctx->emit->stream, size_value);
        fputs(")\n", ctx->emit->stream);
        return cn_llvm_register_value(expression->type, reg_id);
    }
    case CN_IR_EXPR_ADDR: {
        cn_llvm_address address = cn_llvm_lower_address(ctx, scope, expression->data.addr_expr.target, true);
        if (!address.is_valid) {
            return cn_llvm_invalid_value(expression->type);
        }
        return cn_llvm_register_value(expression->type, address.pointer_reg_id);
    }
    case CN_IR_EXPR_DEREF: {
        cn_llvm_address address = cn_llvm_lower_address(ctx, scope, expression, false);
        if (!address.is_valid) {
            return cn_llvm_invalid_value(expression->type);
        }
        return cn_llvm_emit_load(ctx, address.type, address.pointer_reg_id);
    }
    }

    return cn_llvm_invalid_value(expression->type);
}

static bool cn_llvm_emit_block(
    cn_llvm_function_ctx *ctx,
    cn_llvm_scope *parent,
    const cn_ir_block *block,
    bool creates_scope
);

static bool cn_llvm_emit_statement(
    cn_llvm_function_ctx *ctx,
    cn_llvm_scope *scope,
    const cn_ir_stmt *statement
) {
    switch (statement->kind) {
    case CN_IR_STMT_LET: {
        cn_llvm_value initializer = cn_llvm_lower_expression(ctx, scope, statement->data.let_stmt.initializer);
        if (!initializer.is_valid) {
            return false;
        }

        int alloca_id = cn_llvm_emit_alloca(ctx, statement->data.let_stmt.type);
        cn_llvm_emit_store(ctx, initializer, alloca_id);
        cn_llvm_scope_define(ctx->emit, scope, statement->data.let_stmt.name, statement->data.let_stmt.type, alloca_id);
        return true;
    }
    case CN_IR_STMT_ASSIGN: {
        cn_llvm_address target_address = cn_llvm_lower_address(ctx, scope, statement->data.assign_stmt.target, true);
        cn_llvm_value value = cn_llvm_lower_expression(ctx, scope, statement->data.assign_stmt.value);
        if (!target_address.is_valid || !value.is_valid) {
            return false;
        }

        cn_llvm_emit_store(ctx, value, target_address.pointer_reg_id);
        return true;
    }
    case CN_IR_STMT_RETURN:
        if (statement->data.return_stmt.value == NULL) {
            cn_llvm_emit_ret(ctx, cn_llvm_void_value(ctx->function->return_type));
            return true;
        }
        {
            cn_llvm_value value = cn_llvm_lower_expression(ctx, scope, statement->data.return_stmt.value);
            if (!value.is_valid) {
                return false;
            }
            cn_llvm_emit_ret(ctx, value);
        }
        return true;
    case CN_IR_STMT_EXPR: {
        cn_llvm_value value = cn_llvm_lower_expression(ctx, scope, statement->data.expr_stmt.value);
        return value.is_valid;
    }
    case CN_IR_STMT_IF: {
        int then_label = cn_llvm_next_label(ctx);
        int else_label = cn_llvm_next_label(ctx);
        int merge_label = cn_llvm_next_label(ctx);
        cn_llvm_value condition = cn_llvm_lower_expression(ctx, scope, statement->data.if_stmt.condition);
        if (!condition.is_valid) {
            return false;
        }

        if (statement->data.if_stmt.else_block == NULL) {
            cn_llvm_emit_cond_br(ctx, condition, then_label, merge_label);
        } else {
            cn_llvm_emit_cond_br(ctx, condition, then_label, else_label);
        }

        cn_llvm_emit_label(ctx, then_label);
        if (!cn_llvm_emit_block(ctx, scope, statement->data.if_stmt.then_block, true)) {
            return false;
        }
        if (!ctx->current_block_terminated) {
            cn_llvm_emit_br(ctx, merge_label);
        }

        if (statement->data.if_stmt.else_block != NULL) {
            cn_llvm_emit_label(ctx, else_label);
            if (!cn_llvm_emit_block(ctx, scope, statement->data.if_stmt.else_block, true)) {
                return false;
            }
            if (!ctx->current_block_terminated) {
                cn_llvm_emit_br(ctx, merge_label);
            }
        }

        cn_llvm_emit_label(ctx, merge_label);
        return true;
    }
    case CN_IR_STMT_WHILE: {
        int cond_label = cn_llvm_next_label(ctx);
        int body_label = cn_llvm_next_label(ctx);
        int end_label = cn_llvm_next_label(ctx);

        cn_llvm_emit_br(ctx, cond_label);
        cn_llvm_emit_label(ctx, cond_label);
        cn_llvm_value condition = cn_llvm_lower_expression(ctx, scope, statement->data.while_stmt.condition);
        if (!condition.is_valid) {
            return false;
        }
        cn_llvm_emit_cond_br(ctx, condition, body_label, end_label);

        cn_llvm_emit_label(ctx, body_label);
        if (!cn_llvm_emit_block(ctx, scope, statement->data.while_stmt.body, true)) {
            return false;
        }
        if (!ctx->current_block_terminated) {
            cn_llvm_emit_br(ctx, cond_label);
        }

        cn_llvm_emit_label(ctx, end_label);
        return true;
    }
    case CN_IR_STMT_LOOP: {
        int body_label = cn_llvm_next_label(ctx);
        int end_label = cn_llvm_next_label(ctx);

        cn_llvm_emit_br(ctx, body_label);
        cn_llvm_emit_label(ctx, body_label);
        if (!cn_llvm_emit_block(ctx, scope, statement->data.loop_stmt.body, true)) {
            return false;
        }
        if (!ctx->current_block_terminated) {
            cn_llvm_emit_br(ctx, body_label);
        }

        cn_llvm_emit_label(ctx, end_label);
        return true;
    }
    case CN_IR_STMT_FOR: {
        cn_llvm_scope loop_scope = {0};
        int iter_ptr = cn_llvm_emit_alloca(ctx, statement->data.for_stmt.type);
        int end_ptr = cn_llvm_emit_alloca(ctx, statement->data.for_stmt.type);
        int cond_label = cn_llvm_next_label(ctx);
        int body_label = cn_llvm_next_label(ctx);
        int end_label = cn_llvm_next_label(ctx);

        cn_llvm_value start = cn_llvm_lower_expression(ctx, scope, statement->data.for_stmt.start);
        cn_llvm_value end = cn_llvm_lower_expression(ctx, scope, statement->data.for_stmt.end);
        if (!start.is_valid || !end.is_valid) {
            return false;
        }

        cn_llvm_emit_store(ctx, start, iter_ptr);
        cn_llvm_emit_store(ctx, end, end_ptr);
        loop_scope.parent = scope;
        cn_llvm_scope_define(ctx->emit, &loop_scope, statement->data.for_stmt.name, statement->data.for_stmt.type, iter_ptr);

        cn_llvm_emit_br(ctx, cond_label);
        cn_llvm_emit_label(ctx, cond_label);
        cn_llvm_value iter_value = cn_llvm_emit_load(ctx, statement->data.for_stmt.type, iter_ptr);
        cn_llvm_value end_value = cn_llvm_emit_load(ctx, statement->data.for_stmt.type, end_ptr);
        int cmp_reg = cn_llvm_next_temp(ctx);
        cn_llvm_emit_indent(ctx->emit->stream);
        cn_llvm_emit_reg(ctx->emit->stream, cmp_reg);
        fputs(" = icmp slt i64 ", ctx->emit->stream);
        cn_llvm_emit_value_ref(ctx->emit->stream, iter_value);
        fputs(", ", ctx->emit->stream);
        cn_llvm_emit_value_ref(ctx->emit->stream, end_value);
        fputc('\n', ctx->emit->stream);
        cn_llvm_emit_cond_br(ctx, cn_llvm_register_value(&g_llvm_bool_type, cmp_reg), body_label, end_label);

        cn_llvm_emit_label(ctx, body_label);
        if (!cn_llvm_emit_block(ctx, &loop_scope, statement->data.for_stmt.body, true)) {
            cn_llvm_scope_release(ctx->emit, &loop_scope);
            return false;
        }
        if (!ctx->current_block_terminated) {
            cn_llvm_value current_iter = cn_llvm_emit_load(ctx, statement->data.for_stmt.type, iter_ptr);
            int next_reg = cn_llvm_next_temp(ctx);
            cn_llvm_emit_indent(ctx->emit->stream);
            cn_llvm_emit_reg(ctx->emit->stream, next_reg);
            fputs(" = add i64 ", ctx->emit->stream);
            cn_llvm_emit_value_ref(ctx->emit->stream, current_iter);
            fputs(", 1\n", ctx->emit->stream);
            cn_llvm_emit_store(ctx, cn_llvm_register_value(statement->data.for_stmt.type, next_reg), iter_ptr);
            cn_llvm_emit_br(ctx, cond_label);
        }

        cn_llvm_scope_release(ctx->emit, &loop_scope);
        cn_llvm_emit_label(ctx, end_label);
        return true;
    }
    case CN_IR_STMT_FREE: {
        cn_llvm_value value = cn_llvm_lower_expression(ctx, scope, statement->data.free_stmt.value);
        if (!value.is_valid) {
            return false;
        }
        cn_llvm_emit_indent(ctx->emit->stream);
        if (value.type->kind == CN_IR_TYPE_STR) {
            fputs("call void @cn_free_str(ptr ", ctx->emit->stream);
        } else {
            fputs("call void @free(ptr ", ctx->emit->stream);
        }
        cn_llvm_emit_value_ref(ctx->emit->stream, value);
        fputs(")\n", ctx->emit->stream);
        return true;
    }
    }

    return false;
}

static bool cn_llvm_emit_block(
    cn_llvm_function_ctx *ctx,
    cn_llvm_scope *parent,
    const cn_ir_block *block,
    bool creates_scope
) {
    cn_llvm_scope scope = {0};
    cn_llvm_scope *active_scope = parent;

    if (creates_scope) {
        scope.parent = parent;
        active_scope = &scope;
    }

    for (size_t i = 0; i < block->statements.count; ++i) {
        cn_llvm_begin_unreachable_block_if_needed(ctx);
        if (!cn_llvm_emit_statement(ctx, active_scope, block->statements.items[i])) {
            if (creates_scope) {
                cn_llvm_scope_release(ctx->emit, &scope);
            }
            return false;
        }
    }

    if (creates_scope) {
        cn_llvm_scope_release(ctx->emit, &scope);
    }

    return true;
}

static bool cn_llvm_emit_function(cn_llvm_emit_ctx *ctx, const cn_ir_function *function) {
    cn_llvm_function_ctx function_ctx = {0};
    cn_llvm_scope root_scope = {0};
    function_ctx.emit = ctx;
    function_ctx.function = function;
    function_ctx.next_temp = 0;
    function_ctx.next_label = 0;
    function_ctx.current_block_terminated = false;

    fputs("define ", ctx->stream);
    cn_llvm_emit_type(ctx->stream, function->return_type);
    fputc(' ', ctx->stream);
    cn_llvm_emit_function_symbol(ctx->stream, function->module_name, function->name);
    fputc('(', ctx->stream);
    for (size_t i = 0; i < function->parameters.count; ++i) {
        if (i > 0) {
            fputs(", ", ctx->stream);
        }
        cn_llvm_emit_type(ctx->stream, function->parameters.items[i].type);
        fprintf(ctx->stream, " %%arg%zu", i);
    }
    fputs(") {\n", ctx->stream);
    fputs("entry:\n", ctx->stream);

    for (size_t i = 0; i < function->parameters.count; ++i) {
        int alloca_id = cn_llvm_emit_alloca(&function_ctx, function->parameters.items[i].type);
        cn_llvm_emit_indent(ctx->stream);
        fputs("store ", ctx->stream);
        cn_llvm_emit_type(ctx->stream, function->parameters.items[i].type);
        fprintf(ctx->stream, " %%arg%zu, ptr ", i);
        cn_llvm_emit_reg(ctx->stream, alloca_id);
        fputc('\n', ctx->stream);
        cn_llvm_scope_define(ctx, &root_scope, function->parameters.items[i].name, function->parameters.items[i].type, alloca_id);
    }

    if (!cn_llvm_emit_block(&function_ctx, &root_scope, function->body, false)) {
        cn_llvm_scope_release(ctx, &root_scope);
        fputs("}\n", ctx->stream);
        return false;
    }

    if (!function_ctx.current_block_terminated) {
        if (function->return_type->kind == CN_IR_TYPE_VOID) {
            cn_llvm_emit_ret(&function_ctx, cn_llvm_void_value(function->return_type));
        } else {
            cn_llvm_emit_unreachable(&function_ctx);
        }
    }

    cn_llvm_scope_release(ctx, &root_scope);
    fputs("}\n", ctx->stream);
    return true;
}

static bool cn_llvm_collect_string(cn_llvm_emit_ctx *ctx, cn_strview value) {
    if (cn_llvm_find_string(ctx, value) != NULL) {
        return true;
    }

    cn_llvm_string_entry *entry = CN_ALLOC(ctx->allocator, cn_llvm_string_entry);
    entry->value = value;
    entry->global_id = ctx->next_string_id++;
    entry->next = ctx->strings;
    ctx->strings = entry;
    return true;
}

static bool cn_llvm_collect_strings_from_expression(cn_llvm_emit_ctx *ctx, const cn_ir_expr *expression) {
    if (expression == NULL) {
        return true;
    }

    switch (expression->kind) {
    case CN_IR_EXPR_STRING:
        return cn_llvm_collect_string(ctx, expression->data.string_value);
    case CN_IR_EXPR_UNARY:
        return cn_llvm_collect_strings_from_expression(ctx, expression->data.unary.operand);
    case CN_IR_EXPR_BINARY:
        return cn_llvm_collect_strings_from_expression(ctx, expression->data.binary.left) &&
               cn_llvm_collect_strings_from_expression(ctx, expression->data.binary.right);
    case CN_IR_EXPR_IF:
        return cn_llvm_collect_strings_from_expression(ctx, expression->data.if_expr.condition) &&
               cn_llvm_collect_strings_from_expression(ctx, expression->data.if_expr.then_expr) &&
               cn_llvm_collect_strings_from_expression(ctx, expression->data.if_expr.else_expr);
    case CN_IR_EXPR_CALL:
        for (size_t i = 0; i < expression->data.call.arguments.count; ++i) {
            if (!cn_llvm_collect_strings_from_expression(ctx, expression->data.call.arguments.items[i])) {
                return false;
            }
        }
        return true;
    case CN_IR_EXPR_ARRAY_LITERAL:
        for (size_t i = 0; i < expression->data.array_literal.items.count; ++i) {
            if (!cn_llvm_collect_strings_from_expression(ctx, expression->data.array_literal.items.items[i])) {
                return false;
            }
        }
        return true;
    case CN_IR_EXPR_SLICE_FROM_ARRAY:
        return cn_llvm_collect_strings_from_expression(ctx, expression->data.slice_from_array.base);
    case CN_IR_EXPR_INDEX:
        return cn_llvm_collect_strings_from_expression(ctx, expression->data.index.base) &&
               cn_llvm_collect_strings_from_expression(ctx, expression->data.index.index);
    case CN_IR_EXPR_SLICE_VIEW:
        return cn_llvm_collect_strings_from_expression(ctx, expression->data.slice_view.base) &&
               (expression->data.slice_view.start == NULL ||
                cn_llvm_collect_strings_from_expression(ctx, expression->data.slice_view.start)) &&
               (expression->data.slice_view.end == NULL ||
                cn_llvm_collect_strings_from_expression(ctx, expression->data.slice_view.end));
    case CN_IR_EXPR_FIELD:
        return cn_llvm_collect_strings_from_expression(ctx, expression->data.field.base);
    case CN_IR_EXPR_STRUCT_LITERAL:
        for (size_t i = 0; i < expression->data.struct_literal.fields.count; ++i) {
            if (!cn_llvm_collect_strings_from_expression(ctx, expression->data.struct_literal.fields.items[i].value)) {
                return false;
            }
        }
        return true;
    case CN_IR_EXPR_OK:
        return cn_llvm_collect_strings_from_expression(ctx, expression->data.ok_expr.value);
    case CN_IR_EXPR_ADDR:
        return cn_llvm_collect_strings_from_expression(ctx, expression->data.addr_expr.target);
    case CN_IR_EXPR_DEREF:
        return cn_llvm_collect_strings_from_expression(ctx, expression->data.deref_expr.target);
    case CN_IR_EXPR_INT:
    case CN_IR_EXPR_BOOL:
    case CN_IR_EXPR_LOCAL:
    case CN_IR_EXPR_ERR:
    case CN_IR_EXPR_ALLOC:
        return true;
    }

    return true;
}

static bool cn_llvm_collect_strings_from_statement(cn_llvm_emit_ctx *ctx, const cn_ir_stmt *statement) {
    switch (statement->kind) {
    case CN_IR_STMT_LET:
        return cn_llvm_collect_strings_from_expression(ctx, statement->data.let_stmt.initializer);
    case CN_IR_STMT_ASSIGN:
        return cn_llvm_collect_strings_from_expression(ctx, statement->data.assign_stmt.target) &&
               cn_llvm_collect_strings_from_expression(ctx, statement->data.assign_stmt.value);
    case CN_IR_STMT_RETURN:
        return statement->data.return_stmt.value == NULL ||
               cn_llvm_collect_strings_from_expression(ctx, statement->data.return_stmt.value);
    case CN_IR_STMT_EXPR:
        return cn_llvm_collect_strings_from_expression(ctx, statement->data.expr_stmt.value);
    case CN_IR_STMT_FREE:
        return cn_llvm_collect_strings_from_expression(ctx, statement->data.free_stmt.value);
    case CN_IR_STMT_IF:
    case CN_IR_STMT_WHILE:
    case CN_IR_STMT_LOOP:
    case CN_IR_STMT_FOR:
        return true;
    }

    return true;
}

static bool cn_llvm_collect_strings_from_block(cn_llvm_emit_ctx *ctx, const cn_ir_block *block) {
    for (size_t i = 0; i < block->statements.count; ++i) {
        const cn_ir_stmt *statement = block->statements.items[i];

        switch (statement->kind) {
        case CN_IR_STMT_IF:
            if (!cn_llvm_collect_strings_from_expression(ctx, statement->data.if_stmt.condition) ||
                !cn_llvm_collect_strings_from_block(ctx, statement->data.if_stmt.then_block) ||
                (statement->data.if_stmt.else_block != NULL &&
                 !cn_llvm_collect_strings_from_block(ctx, statement->data.if_stmt.else_block))) {
                return false;
            }
            break;
        case CN_IR_STMT_WHILE:
            if (!cn_llvm_collect_strings_from_expression(ctx, statement->data.while_stmt.condition) ||
                !cn_llvm_collect_strings_from_block(ctx, statement->data.while_stmt.body)) {
                return false;
            }
            break;
        case CN_IR_STMT_LOOP:
            if (!cn_llvm_collect_strings_from_block(ctx, statement->data.loop_stmt.body)) {
                return false;
            }
            break;
        case CN_IR_STMT_FOR:
            if (!cn_llvm_collect_strings_from_expression(ctx, statement->data.for_stmt.start) ||
                !cn_llvm_collect_strings_from_expression(ctx, statement->data.for_stmt.end) ||
                !cn_llvm_collect_strings_from_block(ctx, statement->data.for_stmt.body)) {
                return false;
            }
            break;
        default:
            if (!cn_llvm_collect_strings_from_statement(ctx, statement)) {
                return false;
            }
            break;
        }
    }

    return true;
}

static bool cn_llvm_collect_program_strings(cn_llvm_emit_ctx *ctx) {
    for (size_t module_index = 0; module_index < ctx->program->modules.count; ++module_index) {
        const cn_ir_module *module = ctx->program->modules.items[module_index];
        for (size_t function_index = 0; function_index < module->functions.count; ++function_index) {
            if (!cn_llvm_collect_strings_from_block(ctx, module->functions.items[function_index]->body)) {
                return false;
            }
        }
    }

    return true;
}

static void cn_llvm_release_strings(cn_llvm_emit_ctx *ctx) {
    cn_llvm_string_entry *entry = ctx->strings;
    while (entry != NULL) {
        cn_llvm_string_entry *next = entry->next;
        CN_FREE(ctx->allocator, entry);
        entry = next;
    }
    ctx->strings = NULL;
}

static void cn_llvm_emit_escaped_bytes(FILE *stream, cn_strview value) {
    for (size_t i = 0; i < value.length; ++i) {
        unsigned char ch = (unsigned char)value.data[i];
        if (ch >= 32 && ch <= 126 && ch != '\\' && ch != '"') {
            fputc((char)ch, stream);
        } else {
            fprintf(stream, "\\%02X", ch);
        }
    }
}

static void cn_llvm_emit_struct_definitions(const cn_ir_program *program, FILE *stream) {
    for (size_t module_index = 0; module_index < program->modules.count; ++module_index) {
        const cn_ir_module *module = program->modules.items[module_index];
        for (size_t struct_index = 0; struct_index < module->structs.count; ++struct_index) {
            const cn_ir_struct *struct_decl = module->structs.items[struct_index];
            if (cn_llvm_is_runtime_owned_struct(struct_decl->module_name, struct_decl->name)) {
                continue;
            }
            fprintf(
                stream,
                "%%cn_"
            );
            cn_llvm_emit_identifier_part(stream, struct_decl->module_name);
            fputs("__", stream);
            cn_llvm_emit_identifier_part(stream, struct_decl->name);
            fputs(" = type { ", stream);
            for (size_t field_index = 0; field_index < struct_decl->fields.count; ++field_index) {
                if (field_index > 0) {
                    fputs(", ", stream);
                }
                cn_llvm_emit_type(stream, struct_decl->fields.items[field_index].type);
            }
            fputs(" }\n", stream);
        }
    }
}

static void cn_llvm_emit_string_globals(const cn_llvm_emit_ctx *ctx, FILE *stream) {
    for (const cn_llvm_string_entry *entry = ctx->strings; entry != NULL; entry = entry->next) {
        fprintf(
            stream,
            "@.cn.str.%d = private unnamed_addr constant [%zu x i8] c\"",
            entry->global_id,
            entry->value.length + 1
        );
        cn_llvm_emit_escaped_bytes(stream, entry->value);
        fputs("\\00\"\n", stream);
    }
}

static bool cn_llvm_expr_uses_builtin_module(const cn_ir_expr *expression, const char *module_name) {
    if (expression == NULL) {
        return false;
    }

    switch (expression->kind) {
    case CN_IR_EXPR_UNARY:
        return cn_llvm_expr_uses_builtin_module(expression->data.unary.operand, module_name);
    case CN_IR_EXPR_BINARY:
        return cn_llvm_expr_uses_builtin_module(expression->data.binary.left, module_name) ||
               cn_llvm_expr_uses_builtin_module(expression->data.binary.right, module_name);
    case CN_IR_EXPR_IF:
        return cn_llvm_expr_uses_builtin_module(expression->data.if_expr.condition, module_name) ||
               cn_llvm_expr_uses_builtin_module(expression->data.if_expr.then_expr, module_name) ||
               cn_llvm_expr_uses_builtin_module(expression->data.if_expr.else_expr, module_name);
    case CN_IR_EXPR_CALL:
        if (expression->data.call.target_kind == CN_IR_CALL_BUILTIN &&
            cn_sv_eq_cstr(expression->data.call.module_name, module_name)) {
            return true;
        }
        for (size_t i = 0; i < expression->data.call.arguments.count; ++i) {
            if (cn_llvm_expr_uses_builtin_module(expression->data.call.arguments.items[i], module_name)) {
                return true;
            }
        }
        return false;
    case CN_IR_EXPR_ARRAY_LITERAL:
        for (size_t i = 0; i < expression->data.array_literal.items.count; ++i) {
            if (cn_llvm_expr_uses_builtin_module(expression->data.array_literal.items.items[i], module_name)) {
                return true;
            }
        }
        return false;
    case CN_IR_EXPR_SLICE_FROM_ARRAY:
        return cn_llvm_expr_uses_builtin_module(expression->data.slice_from_array.base, module_name);
    case CN_IR_EXPR_INDEX:
        return cn_llvm_expr_uses_builtin_module(expression->data.index.base, module_name) ||
               cn_llvm_expr_uses_builtin_module(expression->data.index.index, module_name);
    case CN_IR_EXPR_SLICE_VIEW:
        return cn_llvm_expr_uses_builtin_module(expression->data.slice_view.base, module_name) ||
               (expression->data.slice_view.start != NULL &&
                cn_llvm_expr_uses_builtin_module(expression->data.slice_view.start, module_name)) ||
               (expression->data.slice_view.end != NULL &&
                cn_llvm_expr_uses_builtin_module(expression->data.slice_view.end, module_name));
    case CN_IR_EXPR_FIELD:
        return cn_llvm_expr_uses_builtin_module(expression->data.field.base, module_name);
    case CN_IR_EXPR_STRUCT_LITERAL:
        for (size_t i = 0; i < expression->data.struct_literal.fields.count; ++i) {
            if (cn_llvm_expr_uses_builtin_module(expression->data.struct_literal.fields.items[i].value, module_name)) {
                return true;
            }
        }
        return false;
    case CN_IR_EXPR_OK:
        return cn_llvm_expr_uses_builtin_module(expression->data.ok_expr.value, module_name);
    case CN_IR_EXPR_ADDR:
        return cn_llvm_expr_uses_builtin_module(expression->data.addr_expr.target, module_name);
    case CN_IR_EXPR_DEREF:
        return cn_llvm_expr_uses_builtin_module(expression->data.deref_expr.target, module_name);
    case CN_IR_EXPR_INT:
    case CN_IR_EXPR_BOOL:
    case CN_IR_EXPR_STRING:
    case CN_IR_EXPR_LOCAL:
    case CN_IR_EXPR_ERR:
    case CN_IR_EXPR_ALLOC:
        return false;
    }

    return false;
}

static bool cn_llvm_stmt_uses_builtin_module(const cn_ir_stmt *statement, const char *module_name);

static bool cn_llvm_block_uses_builtin_module(const cn_ir_block *block, const char *module_name) {
    if (block == NULL) {
        return false;
    }

    for (size_t i = 0; i < block->statements.count; ++i) {
        if (cn_llvm_stmt_uses_builtin_module(block->statements.items[i], module_name)) {
            return true;
        }
    }

    return false;
}

static bool cn_llvm_stmt_uses_builtin_module(const cn_ir_stmt *statement, const char *module_name) {
    switch (statement->kind) {
    case CN_IR_STMT_LET:
        return cn_llvm_expr_uses_builtin_module(statement->data.let_stmt.initializer, module_name);
    case CN_IR_STMT_ASSIGN:
        return cn_llvm_expr_uses_builtin_module(statement->data.assign_stmt.target, module_name) ||
               cn_llvm_expr_uses_builtin_module(statement->data.assign_stmt.value, module_name);
    case CN_IR_STMT_RETURN:
        return cn_llvm_expr_uses_builtin_module(statement->data.return_stmt.value, module_name);
    case CN_IR_STMT_EXPR:
        return cn_llvm_expr_uses_builtin_module(statement->data.expr_stmt.value, module_name);
    case CN_IR_STMT_IF:
        return cn_llvm_expr_uses_builtin_module(statement->data.if_stmt.condition, module_name) ||
               cn_llvm_block_uses_builtin_module(statement->data.if_stmt.then_block, module_name) ||
               cn_llvm_block_uses_builtin_module(statement->data.if_stmt.else_block, module_name);
    case CN_IR_STMT_WHILE:
        return cn_llvm_expr_uses_builtin_module(statement->data.while_stmt.condition, module_name) ||
               cn_llvm_block_uses_builtin_module(statement->data.while_stmt.body, module_name);
    case CN_IR_STMT_LOOP:
        return cn_llvm_block_uses_builtin_module(statement->data.loop_stmt.body, module_name);
    case CN_IR_STMT_FOR:
        return cn_llvm_expr_uses_builtin_module(statement->data.for_stmt.start, module_name) ||
               cn_llvm_expr_uses_builtin_module(statement->data.for_stmt.end, module_name) ||
               cn_llvm_block_uses_builtin_module(statement->data.for_stmt.body, module_name);
    case CN_IR_STMT_FREE:
        return cn_llvm_expr_uses_builtin_module(statement->data.free_stmt.value, module_name);
    }

    return false;
}

static bool cn_llvm_program_uses_builtin_module(const cn_ir_program *program, const char *module_name) {
    for (size_t i = 0; i < program->modules.count; ++i) {
        const cn_ir_module *module = program->modules.items[i];
        for (size_t j = 0; j < module->consts.count; ++j) {
            if (cn_llvm_expr_uses_builtin_module(module->consts.items[j]->initializer, module_name)) {
                return true;
            }
        }
        for (size_t j = 0; j < module->functions.count; ++j) {
            if (cn_llvm_block_uses_builtin_module(module->functions.items[j]->body, module_name)) {
                return true;
            }
        }
    }

    return false;
}

static const cn_ir_function *cn_llvm_find_entry_function(const cn_ir_program *program) {
    if (program->modules.count == 0) {
        return NULL;
    }

    const cn_ir_module *root_module = program->modules.items[0];
    for (size_t i = 0; i < root_module->functions.count; ++i) {
        if (cn_sv_eq_cstr(root_module->functions.items[i]->name, "main")) {
            return root_module->functions.items[i];
        }
    }

    return NULL;
}

static bool cn_llvm_emit_entry_wrapper(cn_llvm_emit_ctx *ctx) {
    const cn_ir_function *entry = cn_llvm_find_entry_function(ctx->program);
    if (entry == NULL) {
        return true;
    }

    if (entry->parameters.count != 0) {
        cn_llvm_emit_unsupported_feature(
            ctx->diagnostics,
            entry->offset,
            "main functions with parameters in binary entry lowering"
        );
        return false;
    }

    if (entry->return_type->kind != CN_IR_TYPE_INT &&
        entry->return_type->kind != CN_IR_TYPE_U8 &&
        entry->return_type->kind != CN_IR_TYPE_VOID) {
        cn_llvm_emit_unsupported_feature(
            ctx->diagnostics,
            entry->offset,
            "main functions that do not return int, u8, or void"
        );
        return false;
    }

    fputs("define i32 @main() {\n", ctx->stream);
    fputs("entry:\n", ctx->stream);
    if (entry->return_type->kind == CN_IR_TYPE_VOID) {
        fputs("  call void ", ctx->stream);
        cn_llvm_emit_function_symbol(ctx->stream, entry->module_name, entry->name);
        fputs("()\n", ctx->stream);
        fputs("  ret i32 0\n", ctx->stream);
    } else if (entry->return_type->kind == CN_IR_TYPE_U8) {
        fputs("  %entry.result = call i8 ", ctx->stream);
        cn_llvm_emit_function_symbol(ctx->stream, entry->module_name, entry->name);
        fputs("()\n", ctx->stream);
        fputs("  %entry.status = zext i8 %entry.result to i32\n", ctx->stream);
        fputs("  ret i32 %entry.status\n", ctx->stream);
    } else {
        fputs("  %entry.result = call i64 ", ctx->stream);
        cn_llvm_emit_function_symbol(ctx->stream, entry->module_name, entry->name);
        fputs("()\n", ctx->stream);
        fputs("  %entry.status = trunc i64 %entry.result to i32\n", ctx->stream);
        fputs("  ret i32 %entry.status\n", ctx->stream);
    }
    fputs("}\n", ctx->stream);
    return true;
}

bool cn_backend_emit_llvm_ir(
    cn_allocator *allocator,
    const cn_ir_program *program,
    cn_diag_bag *diagnostics,
    FILE *stream
) {
    cn_llvm_emit_ctx ctx = {0};
    ctx.allocator = allocator;
    ctx.diagnostics = diagnostics;
    ctx.program = program;
    ctx.stream = stream;
    ctx.strings = NULL;
    ctx.next_string_id = 0;

    bool use_x11 = cn_llvm_program_uses_builtin_module(program, "std.x11");
    bool use_ipc = cn_llvm_program_uses_builtin_module(program, "std.ipc");

    if (!cn_llvm_collect_program_strings(&ctx)) {
        cn_llvm_release_strings(&ctx);
        return false;
    }

    for (size_t module_index = 0; module_index < program->modules.count; ++module_index) {
        const cn_ir_module *module = program->modules.items[module_index];
        cn_diag_bag_set_source(diagnostics, module->source);
        if (!cn_llvm_validate_module(&ctx, module, 0)) {
            cn_llvm_release_strings(&ctx);
            return false;
        }
    }

    const char *target_triple = cn_llvm_host_target_triple();
    if (target_triple != NULL) {
        fprintf(stream, "target triple = \"%s\"\n\n", target_triple);
    }

    cn_llvm_emit_struct_definitions(program, stream);
    if (target_triple != NULL || program->modules.count > 0 || ctx.strings != NULL) {
        fputc('\n', stream);
    }
    cn_llvm_emit_string_globals(&ctx, stream);
    if (ctx.strings != NULL) {
        fputc('\n', stream);
    }
    cn_llvm_emit_runtime_prelude(stream, use_x11, use_ipc);
    fputs("\n\n", stream);
    if (!cn_llvm_emit_entry_wrapper(&ctx)) {
        cn_llvm_release_strings(&ctx);
        return false;
    }
    fputs("\n\n", stream);

    for (size_t module_index = 0; module_index < program->modules.count; ++module_index) {
        const cn_ir_module *module = program->modules.items[module_index];
        cn_diag_bag_set_source(diagnostics, module->source);
        fprintf(
            stream,
            "; module %.*s (%.*s)\n",
            (int)module->name.length,
            module->name.data,
            (int)module->path.length,
            module->path.data
        );

        for (size_t function_index = 0; function_index < module->functions.count; ++function_index) {
            if (!cn_llvm_emit_function(&ctx, module->functions.items[function_index])) {
                cn_llvm_release_strings(&ctx);
                return false;
            }
            if (function_index + 1 < module->functions.count) {
                fputc('\n', stream);
            }
        }

        if (module_index + 1 < program->modules.count) {
            fputs("\n\n", stream);
        }
    }

    cn_llvm_release_strings(&ctx);
    return !cn_diag_has_error(diagnostics);
}
