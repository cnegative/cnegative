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

static void cn_project_push_module(cn_project *project, cn_module *module) {
    cn_project_reserve_modules(project, project->module_count + 1);
    project->modules[project->module_count++] = module;
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
