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
        "define private i64 @cn_math_sign(i64 %value) {\n"
        "entry:\n"
        "  %is.positive = icmp sgt i64 %value, 0\n"
        "  %is.negative = icmp slt i64 %value, 0\n"
        "  %positive = select i1 %is.positive, i64 1, i64 0\n"
        "  %result = select i1 %is.negative, i64 -1, i64 %positive\n"
        "  ret i64 %result\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private i64 @cn_math_square(i64 %value) {\n"
        "entry:\n"
        "  %result = mul i64 %value, %value\n"
        "  ret i64 %result\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private i64 @cn_math_cube(i64 %value) {\n"
        "entry:\n"
        "  %square = mul i64 %value, %value\n"
        "  %result = mul i64 %square, %value\n"
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
        "define private i1 @cn_math_is_even(i64 %value) {\n"
        "entry:\n"
        "  %remainder = srem i64 %value, 2\n"
        "  %result = icmp eq i64 %remainder, 0\n"
        "  ret i1 %result\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private i1 @cn_math_is_odd(i64 %value) {\n"
        "entry:\n"
        "  %remainder = srem i64 %value, 2\n"
        "  %result = icmp ne i64 %remainder, 0\n"
        "  ret i1 %result\n"
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
    fputs(
        "define private i1 @cn_math_between(i64 %value, i64 %lower, i64 %upper) {\n"
        "entry:\n"
        "  %low = call i64 @cn_math_min(i64 %lower, i64 %upper)\n"
        "  %high = call i64 @cn_math_max(i64 %lower, i64 %upper)\n"
        "  %ge.low = icmp sge i64 %value, %low\n"
        "  %le.high = icmp sle i64 %value, %high\n"
        "  %result = and i1 %ge.low, %le.high\n"
        "  ret i1 %result\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private i64 @cn_math_distance(i64 %left, i64 %right) {\n"
        "entry:\n"
        "  %delta = sub i64 %left, %right\n"
        "  %result = call i64 @cn_math_abs(i64 %delta)\n"
        "  ret i64 %result\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private i64 @cn_math_gcd(i64 %left, i64 %right) {\n"
        "entry:\n"
        "  %a0 = call i64 @cn_math_abs(i64 %left)\n"
        "  %b0 = call i64 @cn_math_abs(i64 %right)\n"
        "  br label %loop\n"
        "\n"
        "loop:\n"
        "  %a = phi i64 [ %a0, %entry ], [ %next.a, %body ]\n"
        "  %b = phi i64 [ %b0, %entry ], [ %next.b, %body ]\n"
        "  %done = icmp eq i64 %b, 0\n"
        "  br i1 %done, label %exit, label %body\n"
        "\n"
        "body:\n"
        "  %remainder = srem i64 %a, %b\n"
        "  %next.a = add i64 %b, 0\n"
        "  %next.b = add i64 %remainder, 0\n"
        "  br label %loop\n"
        "\n"
        "exit:\n"
        "  ret i64 %a\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private i64 @cn_math_lcm(i64 %left, i64 %right) {\n"
        "entry:\n"
        "  %left.zero = icmp eq i64 %left, 0\n"
        "  %right.zero = icmp eq i64 %right, 0\n"
        "  %has.zero = or i1 %left.zero, %right.zero\n"
        "  br i1 %has.zero, label %ret.zero, label %calc\n"
        "\n"
        "ret.zero:\n"
        "  ret i64 0\n"
        "\n"
        "calc:\n"
        "  %gcd = call i64 @cn_math_gcd(i64 %left, i64 %right)\n"
        "  %quotient = sdiv i64 %left, %gcd\n"
        "  %product = mul i64 %quotient, %right\n"
        "  %result = call i64 @cn_math_abs(i64 %product)\n"
        "  ret i64 %result\n"
        "}\n\n",
        stream
    );
}
