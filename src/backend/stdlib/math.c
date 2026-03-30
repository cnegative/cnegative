#include "cnegative/llvm_runtime.h"

void cn_llvm_emit_runtime_math(FILE *stream) {
    fputs(
        "define private i64 @cn_math_abs(i64 %value) {\n"
        "entry:\n"
        "  %is.nonnegative = icmp sge i64 %value, 0\n"
        "  %negated = sub i64 0, %value\n"
        "  %result = select i1 %is.nonnegative, i64 %value, i64 %negated\n"
        "  ret i64 %result\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private i64 @cn_math_min(i64 %left, i64 %right) {\n"
        "entry:\n"
        "  %take.left = icmp sle i64 %left, %right\n"
        "  %result = select i1 %take.left, i64 %left, i64 %right\n"
        "  ret i64 %result\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private i64 @cn_math_max(i64 %left, i64 %right) {\n"
        "entry:\n"
        "  %take.left = icmp sge i64 %left, %right\n"
        "  %result = select i1 %take.left, i64 %left, i64 %right\n"
        "  ret i64 %result\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private i64 @cn_math_clamp(i64 %value, i64 %lower, i64 %upper) {\n"
        "entry:\n"
        "  %low = call i64 @cn_math_min(i64 %lower, i64 %upper)\n"
        "  %high = call i64 @cn_math_max(i64 %lower, i64 %upper)\n"
        "  %below = icmp slt i64 %value, %low\n"
        "  %above = icmp sgt i64 %value, %high\n"
        "  %raised = select i1 %below, i64 %low, i64 %value\n"
        "  %result = select i1 %above, i64 %high, i64 %raised\n"
        "  ret i64 %result\n"
        "}\n\n",
        stream
    );
}
