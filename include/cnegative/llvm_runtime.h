#ifndef CNEGATIVE_LLVM_RUNTIME_H
#define CNEGATIVE_LLVM_RUNTIME_H

#include <stdbool.h>
#include <stdio.h>

void cn_llvm_emit_runtime_decls(FILE *stream);
void cn_llvm_emit_runtime_core(FILE *stream);
void cn_llvm_emit_runtime_bytes(FILE *stream);
void cn_llvm_emit_runtime_ipc(FILE *stream);
void cn_llvm_emit_runtime_lines(FILE *stream);
void cn_llvm_emit_runtime_strings(FILE *stream);
void cn_llvm_emit_runtime_text(FILE *stream);
void cn_llvm_emit_runtime_parse(FILE *stream);
void cn_llvm_emit_runtime_math(FILE *stream);
void cn_llvm_emit_runtime_time(FILE *stream);
void cn_llvm_emit_runtime_env(FILE *stream);
void cn_llvm_emit_runtime_net(FILE *stream);
void cn_llvm_emit_runtime_path(FILE *stream);
void cn_llvm_emit_runtime_fs(FILE *stream);
void cn_llvm_emit_runtime_process(FILE *stream);
void cn_llvm_emit_runtime_io(FILE *stream);
void cn_llvm_emit_runtime_term(FILE *stream);
void cn_llvm_emit_runtime_x11(FILE *stream);
void cn_llvm_emit_runtime_prelude(FILE *stream, bool use_x11, bool use_ipc);

#endif
