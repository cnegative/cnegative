#ifndef CNEGATIVE_PROJECT_H
#define CNEGATIVE_PROJECT_H

#include "cnegative/ast.h"
#include "cnegative/diagnostics.h"
#include "cnegative/source.h"

typedef struct cn_module {
    char *name;
    char *path;
    char *directory;
    cn_source source;
    cn_program *program;
    struct cn_module **imports;
    size_t import_count;
    bool loading;
} cn_module;

typedef struct cn_project {
    cn_allocator *allocator;
    cn_module **modules;
    size_t module_count;
    size_t module_capacity;
    cn_module *root;
} cn_project;

cn_project *cn_project_load(cn_allocator *allocator, const char *entry_path, cn_diag_bag *diagnostics);
void cn_project_destroy(cn_allocator *allocator, cn_project *project);
const cn_module *cn_project_find_module_by_name(const cn_project *project, cn_strview name);

#endif
