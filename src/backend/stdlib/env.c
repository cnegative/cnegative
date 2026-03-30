#include "cnegative/llvm_runtime.h"

void cn_llvm_emit_runtime_env(FILE *stream) {
    fputs(
        "define private i1 @cn_env_has(ptr %name) {\n"
        "entry:\n"
        "  %value = call ptr @getenv(ptr %name)\n"
        "  %exists = icmp ne ptr %value, null\n"
        "  ret i1 %exists\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, ptr } @cn_env_get(ptr %name) {\n"
        "entry:\n"
        "  %value = call ptr @getenv(ptr %name)\n"
        "  %exists = icmp ne ptr %value, null\n"
        "  br i1 %exists, label %return.ok, label %return.err\n"
        "return.ok:\n"
        "  %owned = call ptr @cn_dup_cstr(ptr %value)\n"
        "  %ok = insertvalue { i1, ptr } zeroinitializer, i1 true, 0\n"
        "  %value.ok = insertvalue { i1, ptr } %ok, ptr %owned, 1\n"
        "  ret { i1, ptr } %value.ok\n"
        "return.err:\n"
        "  ret { i1, ptr } zeroinitializer\n"
        "}\n\n",
        stream
    );
}
