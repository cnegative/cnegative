#include "cnegative/llvm_runtime.h"

void cn_llvm_emit_runtime_prelude(FILE *stream) {
    cn_llvm_emit_runtime_decls(stream);
    cn_llvm_emit_runtime_core(stream);
    cn_llvm_emit_runtime_strings(stream);
    cn_llvm_emit_runtime_parse(stream);
    cn_llvm_emit_runtime_math(stream);
    cn_llvm_emit_runtime_time(stream);
    cn_llvm_emit_runtime_env(stream);
    cn_llvm_emit_runtime_net(stream);
    cn_llvm_emit_runtime_path(stream);
    cn_llvm_emit_runtime_fs(stream);
    cn_llvm_emit_runtime_process(stream);
    cn_llvm_emit_runtime_io(stream);
}
