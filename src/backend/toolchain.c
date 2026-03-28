#ifndef _WIN32
#define _XOPEN_SOURCE 700
#endif

#include "cnegative/backend.h"

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

    stream = fopen(buffer, "wb");
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

static bool cn_backend_link_object_to_binary(const char *object_path, const char *binary_path, cn_diag_bag *diagnostics) {
    char *clang18_argv[] = {
        "clang-18",
        (char *)object_path,
        "-o",
        (char *)binary_path,
        NULL
    };
    char *clang_argv[] = {
        "clang",
        (char *)object_path,
        "-o",
        (char *)binary_path,
        NULL
    };

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
    bool ok = false;

    if (!cn_backend_write_ir_file(allocator, program, diagnostics, ir_path, sizeof(ir_path))) {
        return false;
    }

    if (!cn_backend_create_temp_path(object_path, sizeof(object_path), "cno")) {
        cn_backend_delete_file(ir_path);
        cn_backend_emit_toolchain_error(diagnostics, "could not create temporary object file", "<temp>");
        return false;
    }

    ok = cn_backend_compile_ir_to_object(ir_path, object_path, diagnostics) &&
         cn_backend_link_object_to_binary(object_path, output_path, diagnostics);

    cn_backend_delete_file(ir_path);
    cn_backend_delete_file(object_path);
    return ok && !cn_diag_has_error(diagnostics);
}
