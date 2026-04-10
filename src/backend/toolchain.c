#ifndef _WIN32
#define _XOPEN_SOURCE 700
#endif

#include "cnegative/backend.h"
#include "cnegative/native_runtime.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>

extern int mkstemp(char *);
#endif

static void cn_backend_emit_toolchain_error(cn_diag_bag *diagnostics, const char *message, const char *path) {
    cn_diag_emit(
        diagnostics,
        CN_DIAG_ERROR,
        "E3022",
        0,
        "%s: %s",
        message,
        path
    );
}

static bool cn_backend_delete_file(const char *path) {
    if (remove(path) == 0) {
        return true;
    }

    return errno == ENOENT;
}

static FILE *cn_backend_open_file(const char *path, const char *mode) {
#ifdef _WIN32
    FILE *stream = NULL;
    errno_t error = fopen_s(&stream, path, mode);
    if (error != 0) {
        return NULL;
    }
    return stream;
#else
    return fopen(path, mode);
#endif
}

static bool cn_backend_create_temp_path(char *buffer, size_t buffer_size, const char *prefix) {
#ifdef _WIN32
    char temp_dir[MAX_PATH + 1];
    char temp_path[MAX_PATH + 1];
    DWORD dir_length = GetTempPathA((DWORD)sizeof(temp_dir), temp_dir);

    if (dir_length == 0 || dir_length >= sizeof(temp_dir)) {
        return false;
    }

    if (GetTempFileNameA(temp_dir, prefix, 0, temp_path) == 0) {
        return false;
    }

    if (strlen(temp_path) + 1 > buffer_size) {
        cn_backend_delete_file(temp_path);
        return false;
    }

    memcpy(buffer, temp_path, strlen(temp_path) + 1);
    return true;
#else
    int written = snprintf(buffer, buffer_size, "/tmp/%s-XXXXXX", prefix);
    if (written < 0 || (size_t)written >= buffer_size) {
        return false;
    }

    int fd = mkstemp(buffer);
    if (fd < 0) {
        return false;
    }

    close(fd);
    return true;
#endif
}

static bool cn_backend_write_ir_file(
    cn_allocator *allocator,
    const cn_ir_program *program,
    cn_diag_bag *diagnostics,
    char *buffer,
    size_t buffer_size
) {
    FILE *stream = NULL;
    bool emitted_ok = false;
    bool io_ok = true;

    if (!cn_backend_create_temp_path(buffer, buffer_size, "cni")) {
        cn_backend_emit_toolchain_error(diagnostics, "could not create temporary llvm ir file", "<temp>");
        return false;
    }

    stream = cn_backend_open_file(buffer, "wb");
    if (stream == NULL) {
        cn_backend_delete_file(buffer);
        cn_backend_emit_toolchain_error(diagnostics, "could not open temporary llvm ir file", buffer);
        return false;
    }

    emitted_ok = cn_backend_emit_llvm_ir(allocator, program, diagnostics, stream);
    if (fflush(stream) != 0) {
        io_ok = false;
    }

    if (fclose(stream) != 0) {
        io_ok = false;
    }

    if (!emitted_ok) {
        cn_backend_delete_file(buffer);
        return false;
    }

    if (!io_ok) {
        cn_backend_delete_file(buffer);
        cn_backend_emit_toolchain_error(diagnostics, "could not finalize temporary llvm ir file", buffer);
        return false;
    }

    return true;
}

static bool cn_backend_write_text_file(const char *path, const char *contents, cn_diag_bag *diagnostics, const char *message) {
    FILE *stream = cn_backend_open_file(path, "wb");
    if (stream == NULL) {
        cn_backend_emit_toolchain_error(diagnostics, message, path);
        return false;
    }

    size_t length = strlen(contents);
    bool ok = fwrite(contents, 1, length, stream) == length;
    if (fflush(stream) != 0) {
        ok = false;
    }
    if (fclose(stream) != 0) {
        ok = false;
    }

    if (!ok) {
        cn_backend_delete_file(path);
        cn_backend_emit_toolchain_error(diagnostics, message, path);
        return false;
    }

    return true;
}

static int cn_backend_run_command(char *const argv[]) {
#ifdef _WIN32
    intptr_t status = _spawnvp(_P_WAIT, argv[0], (const char *const *)argv);
    if (status == -1) {
        return errno == ENOENT ? 127 : 126;
    }
    return (int)status;
#else
    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }

    if (pid == 0) {
        execvp(argv[0], argv);
        _exit(errno == ENOENT ? 127 : 126);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        return -1;
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }

    return 128;
#endif
}

static bool cn_backend_run_clang_stage(
    cn_diag_bag *diagnostics,
    char *const clang18_argv[],
    char *const clang_argv[],
    const char *stage_message,
    const char *output_path
) {
    int status = cn_backend_run_command(clang18_argv);
    if (status == 127) {
        status = cn_backend_run_command(clang_argv);
    }

    if (status != 0) {
        cn_backend_emit_toolchain_error(diagnostics, stage_message, output_path);
        return false;
    }

    return true;
}

static bool cn_backend_expr_uses_builtin_module(const cn_ir_expr *expression, const char *module_name);
static bool cn_backend_block_uses_builtin_module(const cn_ir_block *block, const char *module_name);

static bool cn_backend_expr_list_uses_builtin_module(const cn_ir_expr_list *list, const char *module_name) {
    for (size_t i = 0; i < list->count; ++i) {
        if (cn_backend_expr_uses_builtin_module(list->items[i], module_name)) {
            return true;
        }
    }

    return false;
}

static bool cn_backend_expr_uses_builtin_module(const cn_ir_expr *expression, const char *module_name) {
    if (expression == NULL) {
        return false;
    }

    switch (expression->kind) {
    case CN_IR_EXPR_UNARY:
        return cn_backend_expr_uses_builtin_module(expression->data.unary.operand, module_name);
    case CN_IR_EXPR_BINARY:
        return cn_backend_expr_uses_builtin_module(expression->data.binary.left, module_name) ||
               cn_backend_expr_uses_builtin_module(expression->data.binary.right, module_name);
    case CN_IR_EXPR_IF:
        return cn_backend_expr_uses_builtin_module(expression->data.if_expr.condition, module_name) ||
               cn_backend_expr_uses_builtin_module(expression->data.if_expr.then_expr, module_name) ||
               cn_backend_expr_uses_builtin_module(expression->data.if_expr.else_expr, module_name);
    case CN_IR_EXPR_CALL:
        return (expression->data.call.target_kind == CN_IR_CALL_BUILTIN &&
                cn_sv_eq_cstr(expression->data.call.module_name, module_name)) ||
               cn_backend_expr_list_uses_builtin_module(&expression->data.call.arguments, module_name);
    case CN_IR_EXPR_ARRAY_LITERAL:
        return cn_backend_expr_list_uses_builtin_module(&expression->data.array_literal.items, module_name);
    case CN_IR_EXPR_SLICE_FROM_ARRAY:
        return cn_backend_expr_uses_builtin_module(expression->data.slice_from_array.base, module_name);
    case CN_IR_EXPR_INDEX:
        return cn_backend_expr_uses_builtin_module(expression->data.index.base, module_name) ||
               cn_backend_expr_uses_builtin_module(expression->data.index.index, module_name);
    case CN_IR_EXPR_SLICE_VIEW:
        return cn_backend_expr_uses_builtin_module(expression->data.slice_view.base, module_name) ||
               (expression->data.slice_view.start != NULL &&
                cn_backend_expr_uses_builtin_module(expression->data.slice_view.start, module_name)) ||
               (expression->data.slice_view.end != NULL &&
                cn_backend_expr_uses_builtin_module(expression->data.slice_view.end, module_name));
    case CN_IR_EXPR_FIELD:
        return cn_backend_expr_uses_builtin_module(expression->data.field.base, module_name);
    case CN_IR_EXPR_STRUCT_LITERAL:
        for (size_t i = 0; i < expression->data.struct_literal.fields.count; ++i) {
            if (cn_backend_expr_uses_builtin_module(expression->data.struct_literal.fields.items[i].value, module_name)) {
                return true;
            }
        }
        return false;
    case CN_IR_EXPR_OK:
        return cn_backend_expr_uses_builtin_module(expression->data.ok_expr.value, module_name);
    case CN_IR_EXPR_ERR:
        return false;
    case CN_IR_EXPR_ADDR:
        return cn_backend_expr_uses_builtin_module(expression->data.addr_expr.target, module_name);
    case CN_IR_EXPR_DEREF:
        return cn_backend_expr_uses_builtin_module(expression->data.deref_expr.target, module_name);
    case CN_IR_EXPR_ZALLOC:
    case CN_IR_EXPR_INT:
    case CN_IR_EXPR_BOOL:
    case CN_IR_EXPR_STRING:
    case CN_IR_EXPR_NULL:
    case CN_IR_EXPR_LOCAL:
    case CN_IR_EXPR_ALLOC:
        return false;
    }

    return false;
}

static bool cn_backend_stmt_uses_builtin_module(const cn_ir_stmt *statement, const char *module_name) {
    switch (statement->kind) {
    case CN_IR_STMT_LET:
        return cn_backend_expr_uses_builtin_module(statement->data.let_stmt.initializer, module_name);
    case CN_IR_STMT_ASSIGN:
        return cn_backend_expr_uses_builtin_module(statement->data.assign_stmt.target, module_name) ||
               cn_backend_expr_uses_builtin_module(statement->data.assign_stmt.value, module_name);
    case CN_IR_STMT_RETURN:
        return cn_backend_expr_uses_builtin_module(statement->data.return_stmt.value, module_name);
    case CN_IR_STMT_EXPR:
        return cn_backend_expr_uses_builtin_module(statement->data.expr_stmt.value, module_name);
    case CN_IR_STMT_ZONE:
        return cn_backend_block_uses_builtin_module(statement->data.zone_stmt.body, module_name);
    case CN_IR_STMT_IF:
        return cn_backend_expr_uses_builtin_module(statement->data.if_stmt.condition, module_name) ||
               cn_backend_block_uses_builtin_module(statement->data.if_stmt.then_block, module_name) ||
               cn_backend_block_uses_builtin_module(statement->data.if_stmt.else_block, module_name);
    case CN_IR_STMT_WHILE:
        return cn_backend_expr_uses_builtin_module(statement->data.while_stmt.condition, module_name) ||
               cn_backend_block_uses_builtin_module(statement->data.while_stmt.body, module_name);
    case CN_IR_STMT_LOOP:
        return cn_backend_block_uses_builtin_module(statement->data.loop_stmt.body, module_name);
    case CN_IR_STMT_FOR:
        return cn_backend_expr_uses_builtin_module(statement->data.for_stmt.start, module_name) ||
               cn_backend_expr_uses_builtin_module(statement->data.for_stmt.end, module_name) ||
               cn_backend_block_uses_builtin_module(statement->data.for_stmt.body, module_name);
    case CN_IR_STMT_FREE:
        return cn_backend_expr_uses_builtin_module(statement->data.free_stmt.value, module_name);
    }

    return false;
}

static bool cn_backend_block_uses_builtin_module(const cn_ir_block *block, const char *module_name) {
    if (block == NULL) {
        return false;
    }

    for (size_t i = 0; i < block->statements.count; ++i) {
        if (cn_backend_stmt_uses_builtin_module(block->statements.items[i], module_name)) {
            return true;
        }
    }

    return false;
}

static bool cn_backend_program_uses_builtin_module(const cn_ir_program *program, const char *module_name) {
    for (size_t i = 0; i < program->modules.count; ++i) {
        const cn_ir_module *module = program->modules.items[i];
        for (size_t j = 0; j < module->consts.count; ++j) {
            if (cn_backend_expr_uses_builtin_module(module->consts.items[j]->initializer, module_name)) {
                return true;
            }
        }
        for (size_t j = 0; j < module->functions.count; ++j) {
            if (cn_backend_block_uses_builtin_module(module->functions.items[j]->body, module_name)) {
                return true;
            }
        }
    }

    return false;
}

static bool cn_backend_compile_ir_to_object(const char *ir_path, const char *object_path, cn_diag_bag *diagnostics) {
    char *clang18_argv[] = {
        "clang-18",
        "-c",
        "-x",
        "ir",
        (char *)ir_path,
        "-o",
        (char *)object_path,
        NULL
    };
    char *clang_argv[] = {
        "clang",
        "-c",
        "-x",
        "ir",
        (char *)ir_path,
        "-o",
        (char *)object_path,
        NULL
    };

    return cn_backend_run_clang_stage(
        diagnostics,
        clang18_argv,
        clang_argv,
        "clang failed to compile llvm ir to object",
        object_path
    );
}

static bool cn_backend_compile_c_to_object(const char *source_path, const char *object_path, cn_diag_bag *diagnostics) {
    char *clang18_argv[] = {
        "clang-18",
        "-c",
        "-x",
        "c",
        (char *)source_path,
        "-o",
        (char *)object_path,
        NULL
    };
    char *clang_argv[] = {
        "clang",
        "-c",
        "-x",
        "c",
        (char *)source_path,
        "-o",
        (char *)object_path,
        NULL
    };

    return cn_backend_run_clang_stage(
        diagnostics,
        clang18_argv,
        clang_argv,
        "clang failed to compile embedded runtime helper",
        object_path
    );
}

static bool cn_backend_link_object_to_binary(
    const cn_ir_program *program,
    const char *object_path,
    const char *extra_object_path,
    const char *binary_path,
    cn_diag_bag *diagnostics
) {
    bool use_ipc = cn_backend_program_uses_builtin_module(program, "std.ipc");
#ifdef _WIN32
    char *clang18_argv[8];
    char *clang_argv[8];
    size_t clang18_index = 0;
    size_t clang_index = 0;
#else
    bool use_x11 = cn_backend_program_uses_builtin_module(program, "std.x11");
    char *clang18_argv[8];
    char *clang_argv[8];
    size_t clang18_index = 0;
    size_t clang_index = 0;
#endif

    clang18_argv[clang18_index++] = "clang-18";
    clang18_argv[clang18_index++] = (char *)object_path;
    clang_argv[clang_index++] = "clang";
    clang_argv[clang_index++] = (char *)object_path;

    if (use_ipc && extra_object_path != NULL) {
        clang18_argv[clang18_index++] = (char *)extra_object_path;
        clang_argv[clang_index++] = (char *)extra_object_path;
    }

    clang18_argv[clang18_index++] = "-o";
    clang18_argv[clang18_index++] = (char *)binary_path;
    clang_argv[clang_index++] = "-o";
    clang_argv[clang_index++] = (char *)binary_path;

#ifdef _WIN32
    clang18_argv[clang18_index++] = "-lws2_32";
    clang_argv[clang_index++] = "-lws2_32";
#else
    if (use_x11) {
        clang18_argv[clang18_index++] = "-lX11";
        clang_argv[clang_index++] = "-lX11";
    }
#endif

    clang18_argv[clang18_index] = NULL;
    clang_argv[clang_index] = NULL;

    return cn_backend_run_clang_stage(
        diagnostics,
        clang18_argv,
        clang_argv,
        "clang failed to link binary",
        binary_path
    );
}

bool cn_backend_emit_object(
    cn_allocator *allocator,
    const cn_ir_program *program,
    cn_diag_bag *diagnostics,
    const char *output_path
) {
    char ir_path[512];
    bool ok = false;

    if (!cn_backend_write_ir_file(allocator, program, diagnostics, ir_path, sizeof(ir_path))) {
        return false;
    }

    ok = cn_backend_compile_ir_to_object(ir_path, output_path, diagnostics);
    cn_backend_delete_file(ir_path);
    return ok && !cn_diag_has_error(diagnostics);
}

bool cn_backend_build_binary(
    cn_allocator *allocator,
    const cn_ir_program *program,
    cn_diag_bag *diagnostics,
    const char *output_path
) {
    char ir_path[512];
    char object_path[512];
    char runtime_source_path[512];
    char runtime_object_path[512];
    bool ok = false;
    bool use_ipc = cn_backend_program_uses_builtin_module(program, "std.ipc");
    bool has_runtime_source = false;
    bool has_runtime_object = false;

    if (!cn_backend_write_ir_file(allocator, program, diagnostics, ir_path, sizeof(ir_path))) {
        return false;
    }

    if (!cn_backend_create_temp_path(object_path, sizeof(object_path), "cno")) {
        cn_backend_delete_file(ir_path);
        cn_backend_emit_toolchain_error(diagnostics, "could not create temporary object file", "<temp>");
        return false;
    }

    if (use_ipc) {
        if (!cn_backend_create_temp_path(runtime_source_path, sizeof(runtime_source_path), "cnc")) {
            cn_backend_delete_file(ir_path);
            cn_backend_delete_file(object_path);
            cn_backend_emit_toolchain_error(diagnostics, "could not create temporary embedded runtime source file", "<temp>");
            return false;
        }
        has_runtime_source = true;
        if (!cn_backend_create_temp_path(runtime_object_path, sizeof(runtime_object_path), "cno")) {
            cn_backend_delete_file(ir_path);
            cn_backend_delete_file(object_path);
            cn_backend_delete_file(runtime_source_path);
            cn_backend_emit_toolchain_error(diagnostics, "could not create temporary embedded runtime object file", "<temp>");
            return false;
        }
        has_runtime_object = true;
    }

    ok = cn_backend_compile_ir_to_object(ir_path, object_path, diagnostics);
    if (ok && use_ipc) {
        ok = cn_backend_write_text_file(
            runtime_source_path,
            cn_backend_ipc_runtime_source(),
            diagnostics,
            "could not write embedded runtime source file"
        ) && cn_backend_compile_c_to_object(runtime_source_path, runtime_object_path, diagnostics);
    }
    if (ok) {
        ok = cn_backend_link_object_to_binary(
            program,
            object_path,
            use_ipc ? runtime_object_path : NULL,
            output_path,
            diagnostics
        );
    }

    cn_backend_delete_file(ir_path);
    cn_backend_delete_file(object_path);
    if (has_runtime_source) {
        cn_backend_delete_file(runtime_source_path);
    }
    if (has_runtime_object) {
        cn_backend_delete_file(runtime_object_path);
    }
    return ok && !cn_diag_has_error(diagnostics);
}
