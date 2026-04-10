#include "cnegative/llvm_runtime.h"

void cn_llvm_emit_runtime_io(FILE *stream) {
    fputs(
        "define private void @cn_write_int(i64 %value) {\n"
        "entry:\n"
        "  %fmt = getelementptr inbounds [5 x i8], ptr @.cn.int_str_fmt, i64 0, i64 0\n"
        "  call i32 (ptr, ...) @printf(ptr %fmt, i64 %value)\n"
        "  ret void\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private void @cn_write_bool(i1 %value) {\n"
        "entry:\n"
        "  br i1 %value, label %l0, label %l1\n"
        "l0:\n"
        "  %true.ptr = getelementptr inbounds [5 x i8], ptr @.cn.true, i64 0, i64 0\n"
        "  call void @cn_write_str(ptr %true.ptr)\n"
        "  ret void\n"
        "l1:\n"
        "  %false.ptr = getelementptr inbounds [6 x i8], ptr @.cn.false, i64 0, i64 0\n"
        "  call void @cn_write_str(ptr %false.ptr)\n"
        "  ret void\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private void @cn_write_str(ptr %value) {\n"
        "entry:\n"
        "  %fmt = getelementptr inbounds [3 x i8], ptr @.cn.str_fmt, i64 0, i64 0\n"
        "  call i32 (ptr, ...) @printf(ptr %fmt, ptr %value)\n"
        "  ret void\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private void @cn_print_int(i64 %value) {\n"
        "entry:\n"
        "  %fmt = getelementptr inbounds [6 x i8], ptr @.cn.int_fmt, i64 0, i64 0\n"
        "  call i32 (ptr, ...) @printf(ptr %fmt, i64 %value)\n"
        "  ret void\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private void @cn_print_bool(i1 %value) {\n"
        "entry:\n"
        "  br i1 %value, label %l0, label %l1\n"
        "l0:\n"
        "  %true.ptr = getelementptr inbounds [5 x i8], ptr @.cn.true, i64 0, i64 0\n"
        "  call i32 @puts(ptr %true.ptr)\n"
        "  ret void\n"
        "l1:\n"
        "  %false.ptr = getelementptr inbounds [6 x i8], ptr @.cn.false, i64 0, i64 0\n"
        "  call i32 @puts(ptr %false.ptr)\n"
        "  ret void\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private void @cn_print_str(ptr %value) {\n"
        "entry:\n"
        "  call i32 @puts(ptr %value)\n"
        "  ret void\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private ptr @cn_input() {\n"
        "entry:\n"
        "  %buf = getelementptr inbounds [4096 x i8], ptr @.cn.input_buffer, i64 0, i64 0\n"
        "  %index.slot = alloca i64\n"
        "  store i64 0, ptr %index.slot\n"
        "  br label %loop\n"
        "loop:\n"
        "  %index = load i64, ptr %index.slot\n"
        "  %has.space = icmp slt i64 %index, 4095\n"
        "  br i1 %has.space, label %read, label %finish\n"
        "read:\n"
        "  %ch = call i32 @getchar()\n"
        "  %is.eof = icmp eq i32 %ch, -1\n"
        "  br i1 %is.eof, label %finish, label %check_newline\n"
        "check_newline:\n"
        "  %is.newline = icmp eq i32 %ch, 10\n"
        "  br i1 %is.newline, label %finish, label %store\n"
        "store:\n"
        "  %char.ptr = getelementptr inbounds i8, ptr %buf, i64 %index\n"
        "  %char.byte = trunc i32 %ch to i8\n"
        "  store i8 %char.byte, ptr %char.ptr\n"
        "  %next.index = add i64 %index, 1\n"
        "  store i64 %next.index, ptr %index.slot\n"
        "  br label %loop\n"
        "finish:\n"
        "  %length = load i64, ptr %index.slot\n"
        "  %end.ptr = getelementptr inbounds i8, ptr %buf, i64 %length\n"
        "  store i8 0, ptr %end.ptr\n"
        "  %owned = call ptr @cn_dup_cstr(ptr %buf)\n"
        "  ret ptr %owned\n"
        "}\n",
        stream
    );
}
