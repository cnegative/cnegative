#ifndef CNEGATIVE_BACKEND_H
#define CNEGATIVE_BACKEND_H

#include "cnegative/diagnostics.h"
#include "cnegative/ir.h"
#include "cnegative/memory.h"

#include <stdio.h>

bool cn_backend_emit_llvm_ir(
    cn_allocator *allocator,
    const cn_ir_program *program,
    cn_diag_bag *diagnostics,
    FILE *stream
);

bool cn_backend_emit_object(
    cn_allocator *allocator,
    const cn_ir_program *program,
    cn_diag_bag *diagnostics,
    const char *output_path
);

bool cn_backend_build_binary(
    cn_allocator *allocator,
    const cn_ir_program *program,
    cn_diag_bag *diagnostics,
    const char *output_path
);

#endif
