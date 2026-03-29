#include "cnegative/backend.h"
#include "cnegative/diagnostics.h"
#include "cnegative/ir.h"
#include "cnegative/lexer.h"
#include "cnegative/project.h"
#include "cnegative/sema.h"
#include "cnegative/source.h"
#include "cnegative/token.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define CN_DEFAULT_BINARY_SUFFIX ".exe"
#else
#define CN_DEFAULT_BINARY_SUFFIX ""
#endif

static void cn_print_usage(FILE *stream) {
    fprintf(stream, "usage: cnegc <check|ir|llvm-ir|obj|build|bench-lexer> <file> [output|iterations]\n");
}

static cn_project *cn_load_checked_project(cn_allocator *allocator, const char *path, cn_diag_bag *diagnostics) {
    cn_project *project = cn_project_load(allocator, path, diagnostics);
    if (project != NULL && !cn_diag_has_error(diagnostics)) {
        cn_sema_check_project(project, diagnostics);
    }
    return project;
}

static int cn_finish_command(
    cn_allocator *allocator,
    cn_diag_bag *diagnostics,
    cn_project *project,
    const char *success_prefix,
    const char *path
) {
    bool has_error = cn_diag_has_error(diagnostics);
    if (has_error) {
        cn_diag_print_all(diagnostics, stderr);
    } else if (success_prefix != NULL) {
        printf("%s: %s\n", success_prefix, path);
    }

    if (project != NULL) {
        cn_project_destroy(allocator, project);
    }
    cn_diag_bag_destroy(diagnostics);
    cn_allocator_dump_leaks(allocator, stderr);
    cn_allocator_destroy(allocator);
    return has_error ? 1 : 0;
}

static int cn_run_check(const char *path) {
    cn_allocator allocator;
    cn_allocator_init(&allocator);

    cn_diag_bag diagnostics;
    cn_diag_bag_init(&diagnostics, &allocator, NULL);

    cn_project *project = cn_load_checked_project(&allocator, path, &diagnostics);
    return cn_finish_command(&allocator, &diagnostics, project, "check passed", path);
}

static int cn_run_ir(const char *path) {
    cn_allocator allocator;
    cn_allocator_init(&allocator);

    cn_diag_bag diagnostics;
    cn_diag_bag_init(&diagnostics, &allocator, NULL);

    cn_project *project = cn_load_checked_project(&allocator, path, &diagnostics);
    cn_ir_program *program = NULL;

    if (project != NULL && !cn_diag_has_error(&diagnostics)) {
        cn_ir_lower_project(&allocator, project, &diagnostics, &program);
        if (program != NULL && !cn_diag_has_error(&diagnostics)) {
            cn_ir_optimize_program(&allocator, program);
        }
    }

    if (!cn_diag_has_error(&diagnostics) && program != NULL) {
        cn_ir_program_dump(program, stdout);
    }

    if (program != NULL) {
        cn_ir_program_destroy(&allocator, program);
    }

    return cn_finish_command(&allocator, &diagnostics, project, NULL, path);
}

static int cn_run_llvm_ir(const char *path) {
    cn_allocator allocator;
    cn_allocator_init(&allocator);

    cn_diag_bag diagnostics;
    cn_diag_bag_init(&diagnostics, &allocator, NULL);

    cn_project *project = cn_load_checked_project(&allocator, path, &diagnostics);
    cn_ir_program *program = NULL;

    if (project != NULL && !cn_diag_has_error(&diagnostics)) {
        cn_ir_lower_project(&allocator, project, &diagnostics, &program);
        if (program != NULL && !cn_diag_has_error(&diagnostics)) {
            cn_ir_optimize_program(&allocator, program);
        }
    }

    if (program != NULL && !cn_diag_has_error(&diagnostics)) {
        cn_backend_emit_llvm_ir(&allocator, program, &diagnostics, stdout);
    }

    if (program != NULL) {
        cn_ir_program_destroy(&allocator, program);
    }

    return cn_finish_command(&allocator, &diagnostics, project, NULL, path);
}

static bool cn_path_is_separator(char value) {
    return value == '/' || value == '\\';
}

static void cn_default_output_path(const char *input_path, const char *suffix, bool keep_extension, char *buffer, size_t buffer_size) {
    const char *slash = NULL;
    for (const char *cursor = input_path; *cursor != '\0'; ++cursor) {
        if (cn_path_is_separator(*cursor)) {
            slash = cursor;
        }
    }
    const char *dot = strrchr(input_path, '.');
    size_t stem_length = strlen(input_path);

    if (dot != NULL && (slash == NULL || dot > slash)) {
        stem_length = (size_t)(dot - input_path);
    }

    if (keep_extension) {
        snprintf(buffer, buffer_size, "%.*s%s", (int)stem_length, input_path, suffix);
    } else {
        snprintf(buffer, buffer_size, "%.*s", (int)stem_length, input_path);
    }
}

static int cn_run_object(const char *path, const char *output_path) {
    cn_allocator allocator;
    cn_allocator_init(&allocator);

    cn_diag_bag diagnostics;
    cn_diag_bag_init(&diagnostics, &allocator, NULL);

    cn_project *project = cn_load_checked_project(&allocator, path, &diagnostics);
    cn_ir_program *program = NULL;

    if (project != NULL && !cn_diag_has_error(&diagnostics)) {
        cn_ir_lower_project(&allocator, project, &diagnostics, &program);
        if (program != NULL && !cn_diag_has_error(&diagnostics)) {
            cn_ir_optimize_program(&allocator, program);
        }
    }

    if (program != NULL && !cn_diag_has_error(&diagnostics)) {
        cn_backend_emit_object(&allocator, program, &diagnostics, output_path);
    }

    if (program != NULL) {
        cn_ir_program_destroy(&allocator, program);
    }

    return cn_finish_command(&allocator, &diagnostics, project, "object emitted", output_path);
}

static int cn_run_build(const char *path, const char *output_path) {
    cn_allocator allocator;
    cn_allocator_init(&allocator);

    cn_diag_bag diagnostics;
    cn_diag_bag_init(&diagnostics, &allocator, NULL);

    cn_project *project = cn_load_checked_project(&allocator, path, &diagnostics);
    cn_ir_program *program = NULL;

    if (project != NULL && !cn_diag_has_error(&diagnostics)) {
        cn_ir_lower_project(&allocator, project, &diagnostics, &program);
        if (program != NULL && !cn_diag_has_error(&diagnostics)) {
            cn_ir_optimize_program(&allocator, program);
        }
    }

    if (program != NULL && !cn_diag_has_error(&diagnostics)) {
        cn_backend_build_binary(&allocator, program, &diagnostics, output_path);
    }

    if (program != NULL) {
        cn_ir_program_destroy(&allocator, program);
    }

    return cn_finish_command(&allocator, &diagnostics, project, "binary emitted", output_path);
}

static unsigned long long cn_now_ns(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return ((unsigned long long)ts.tv_sec * 1000000000ull) + (unsigned long long)ts.tv_nsec;
}

static int cn_run_bench_lexer(const char *path, const char *iterations_text) {
    cn_allocator allocator;
    cn_allocator_init(&allocator);

    cn_source source = {0};
    char error_buffer[256];
    size_t iterations = 1000;
    size_t total_tokens = 0;

    if (iterations_text != NULL) {
        char *end = NULL;
        unsigned long long parsed = strtoull(iterations_text, &end, 10);
        if (end == iterations_text || *end != '\0' || parsed == 0) {
            fprintf(stderr, "invalid iteration count: %s\n", iterations_text);
            cn_allocator_dump_leaks(&allocator, stderr);
            cn_allocator_destroy(&allocator);
            return 1;
        }
        iterations = (size_t)parsed;
    }

    if (!cn_source_load(&allocator, path, &source, error_buffer, sizeof(error_buffer))) {
        fprintf(stderr, "%s\n", error_buffer);
        cn_allocator_dump_leaks(&allocator, stderr);
        cn_allocator_destroy(&allocator);
        return 1;
    }

    unsigned long long start_ns = cn_now_ns();
    for (size_t i = 0; i < iterations; ++i) {
        cn_diag_bag diagnostics;
        cn_token_buffer tokens;
        cn_lexer lexer;

        cn_diag_bag_init(&diagnostics, &allocator, &source);
        cn_token_buffer_init(&tokens, &allocator);
        cn_lexer_init(&lexer, &source, &diagnostics);
        cn_lexer_run(&lexer, &tokens);
        total_tokens += tokens.count;

        if (cn_diag_has_error(&diagnostics)) {
            cn_diag_print_all(&diagnostics, stderr);
            cn_token_buffer_destroy(&tokens);
            cn_diag_bag_destroy(&diagnostics);
            cn_source_destroy(&allocator, &source);
            cn_allocator_dump_leaks(&allocator, stderr);
            cn_allocator_destroy(&allocator);
            return 1;
        }

        cn_token_buffer_destroy(&tokens);
        cn_diag_bag_destroy(&diagnostics);
    }
    unsigned long long elapsed_ns = cn_now_ns() - start_ns;
    double elapsed_seconds = (double)elapsed_ns / 1000000000.0;

    printf("bench lexer: %s\n", path);
    printf("iterations: %zu\n", iterations);
    printf("elapsed_ms: %.3f\n", elapsed_seconds * 1000.0);
    printf("tokens_per_second: %.0f\n", elapsed_seconds > 0.0 ? (double)total_tokens / elapsed_seconds : 0.0);

    cn_source_destroy(&allocator, &source);
    cn_allocator_dump_leaks(&allocator, stderr);
    cn_allocator_destroy(&allocator);
    return 0;
}

int main(int argc, char **argv) {
    char output_path[512];
    const char *command = NULL;
    const char *input_path = NULL;
    const char *resolved_output = NULL;

    if (argc < 3 || argc > 4) {
        cn_print_usage(argc > 1 ? stderr : stdout);
        return argc > 1 ? 1 : 0;
    }

    command = argv[1];
    input_path = argv[2];

    if (strcmp(command, "check") == 0) {
        return cn_run_check(input_path);
    }

    if (strcmp(command, "ir") == 0) {
        return cn_run_ir(input_path);
    }

    if (strcmp(command, "llvm-ir") == 0) {
        return cn_run_llvm_ir(input_path);
    }

    if (strcmp(command, "obj") == 0) {
        resolved_output = argc == 4 ? argv[3] : (cn_default_output_path(input_path, ".o", true, output_path, sizeof(output_path)), output_path);
        return cn_run_object(input_path, resolved_output);
    }

    if (strcmp(command, "build") == 0) {
        resolved_output = argc == 4 ? argv[3] : (cn_default_output_path(input_path, CN_DEFAULT_BINARY_SUFFIX, false, output_path, sizeof(output_path)), output_path);
        return cn_run_build(input_path, resolved_output);
    }

    if (strcmp(command, "bench-lexer") == 0) {
        return cn_run_bench_lexer(input_path, argc == 4 ? argv[3] : NULL);
    }

    cn_print_usage(stderr);
    return 1;
}
