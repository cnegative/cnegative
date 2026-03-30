#include "cnegative/llvm_runtime.h"

void cn_llvm_emit_runtime_strings(FILE *stream) {
    fputs(
        "define private ptr @cn_dup_cstr(ptr %src) {\n"
        "entry:\n"
        "  %len = call i64 @strlen(ptr %src)\n"
        "  %size = add i64 %len, 1\n"
        "  %dst = call ptr @malloc(i64 %size)\n"
        "  %has.dst = icmp ne ptr %dst, null\n"
        "  br i1 %has.dst, label %copy, label %fallback\n"
        "copy:\n"
        "  %copied = call ptr @memcpy(ptr %dst, ptr %src, i64 %size)\n"
        "  call void @cn_track_str(ptr %dst)\n"
        "  ret ptr %dst\n"
        "fallback:\n"
        "  %empty.ptr = getelementptr inbounds [1 x i8], ptr @.cn.empty, i64 0, i64 0\n"
        "  ret ptr %empty.ptr\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private ptr @cn_dup_range(ptr %src, i64 %len) {\n"
        "entry:\n"
        "  %size = add i64 %len, 1\n"
        "  %dst = call ptr @malloc(i64 %size)\n"
        "  %has.dst = icmp ne ptr %dst, null\n"
        "  br i1 %has.dst, label %copy, label %fallback\n"
        "copy:\n"
        "  %copied = call ptr @memcpy(ptr %dst, ptr %src, i64 %len)\n"
        "  %end = getelementptr inbounds i8, ptr %dst, i64 %len\n"
        "  store i8 0, ptr %end\n"
        "  call void @cn_track_str(ptr %dst)\n"
        "  ret ptr %dst\n"
        "fallback:\n"
        "  %empty.ptr = getelementptr inbounds [1 x i8], ptr @.cn.empty, i64 0, i64 0\n"
        "  ret ptr %empty.ptr\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private ptr @cn_concat_str(ptr %left, ptr %right) {\n"
        "entry:\n"
        "  %left.len = call i64 @strlen(ptr %left)\n"
        "  %right.len = call i64 @strlen(ptr %right)\n"
        "  %total.no.null = add i64 %left.len, %right.len\n"
        "  %total = add i64 %total.no.null, 1\n"
        "  %dst = call ptr @malloc(i64 %total)\n"
        "  %has.dst = icmp ne ptr %dst, null\n"
        "  br i1 %has.dst, label %copy.left, label %fallback\n"
        "copy.left:\n"
        "  %left.copy = call ptr @memcpy(ptr %dst, ptr %left, i64 %left.len)\n"
        "  %right.dst = getelementptr inbounds i8, ptr %dst, i64 %left.len\n"
        "  %right.copy = call ptr @memcpy(ptr %right.dst, ptr %right, i64 %right.len)\n"
        "  %null.dst = getelementptr inbounds i8, ptr %dst, i64 %total.no.null\n"
        "  store i8 0, ptr %null.dst\n"
        "  call void @cn_track_str(ptr %dst)\n"
        "  ret ptr %dst\n"
        "fallback:\n"
        "  %empty.ptr = getelementptr inbounds [1 x i8], ptr @.cn.empty, i64 0, i64 0\n"
        "  ret ptr %empty.ptr\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private i1 @cn_starts_with(ptr %value, ptr %prefix) {\n"
        "entry:\n"
        "  %value.len = call i64 @strlen(ptr %value)\n"
        "  %prefix.len = call i64 @strlen(ptr %prefix)\n"
        "  %enough = icmp uge i64 %value.len, %prefix.len\n"
        "  br i1 %enough, label %cmp, label %short\n"
        "cmp:\n"
        "  %result = call i32 @memcmp(ptr %value, ptr %prefix, i64 %prefix.len)\n"
        "  %matches = icmp eq i32 %result, 0\n"
        "  ret i1 %matches\n"
        "short:\n"
        "  ret i1 false\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private i1 @cn_ends_with(ptr %value, ptr %suffix) {\n"
        "entry:\n"
        "  %value.len = call i64 @strlen(ptr %value)\n"
        "  %suffix.len = call i64 @strlen(ptr %suffix)\n"
        "  %enough = icmp uge i64 %value.len, %suffix.len\n"
        "  br i1 %enough, label %cmp, label %short\n"
        "cmp:\n"
        "  %start.offset = sub i64 %value.len, %suffix.len\n"
        "  %start = getelementptr inbounds i8, ptr %value, i64 %start.offset\n"
        "  %result = call i32 @memcmp(ptr %start, ptr %suffix, i64 %suffix.len)\n"
        "  %matches = icmp eq i32 %result, 0\n"
        "  ret i1 %matches\n"
        "short:\n"
        "  ret i1 false\n"
        "}\n\n",
        stream
    );
}
