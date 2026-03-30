#ifndef CNEGATIVE_LLVM_RUNTIME_H
#define CNEGATIVE_LLVM_RUNTIME_H

#include <stdio.h>

void cn_llvm_emit_runtime_decls(FILE *stream);
void cn_llvm_emit_runtime_core(FILE *stream);
void cn_llvm_emit_runtime_strings(FILE *stream);
void cn_llvm_emit_runtime_parse(FILE *stream);
void cn_llvm_emit_runtime_math(FILE *stream);
void cn_llvm_emit_runtime_time(FILE *stream);
void cn_llvm_emit_runtime_env(FILE *stream);
void cn_llvm_emit_runtime_net(FILE *stream);
void cn_llvm_emit_runtime_path(FILE *stream);
void cn_llvm_emit_runtime_fs(FILE *stream);
void cn_llvm_emit_runtime_process(FILE *stream);
void cn_llvm_emit_runtime_io(FILE *stream);
void cn_llvm_emit_runtime_prelude(FILE *stream);

#endif
