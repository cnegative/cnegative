#include "cnegative/llvm_runtime.h"

void cn_llvm_emit_runtime_process(FILE *stream) {
    fputs(
        "define private ptr @cn_process_platform() {\n"
        "entry:\n"
        "  %platform = getelementptr inbounds ",
        stream
    );
#ifdef _WIN32
    fputs("[8 x i8], ptr @.cn.process.platform, i64 0, i64 0\n", stream);
#elif defined(__APPLE__)
    fputs("[6 x i8], ptr @.cn.process.platform, i64 0, i64 0\n", stream);
#elif defined(__linux__)
    fputs("[6 x i8], ptr @.cn.process.platform, i64 0, i64 0\n", stream);
#else
    fputs("[8 x i8], ptr @.cn.process.platform, i64 0, i64 0\n", stream);
#endif
    fputs(
        "  %copy = call ptr @cn_dup_cstr(ptr %platform)\n"
        "  ret ptr %copy\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private ptr @cn_process_arch() {\n"
        "entry:\n"
        "  %arch = getelementptr inbounds ",
        stream
    );
#if defined(__x86_64__) || defined(_M_X64)
    fputs("[7 x i8], ptr @.cn.process.arch, i64 0, i64 0\n", stream);
#elif defined(__aarch64__) || defined(_M_ARM64)
    fputs("[6 x i8], ptr @.cn.process.arch, i64 0, i64 0\n", stream);
#else
    fputs("[8 x i8], ptr @.cn.process.arch, i64 0, i64 0\n", stream);
#endif
    fputs(
        "  %copy = call ptr @cn_dup_cstr(ptr %arch)\n"
        "  ret ptr %copy\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private void @cn_process_exit(i64 %status) {\n"
        "entry:\n"
        "  %status32 = trunc i64 %status to i32\n"
        "  call void @exit(i32 %status32)\n"
        "  unreachable\n"
        "}\n\n",
        stream
    );
}
