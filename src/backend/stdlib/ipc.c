#include "cnegative/llvm_runtime.h"

#include <stdio.h>

void cn_llvm_emit_runtime_ipc(FILE *stream) {
    fputs(
        "declare ptr @cn_ipc_native_spawn(ptr, ptr, i64)\n"
        "declare i64 @cn_ipc_native_stdin_write(ptr, ptr)\n"
        "declare i64 @cn_ipc_native_stdin_write_line(ptr, ptr)\n"
        "declare i32 @cn_ipc_native_stdin_close(ptr)\n"
        "declare ptr @cn_ipc_native_stdout_read(ptr, i64)\n"
        "declare ptr @cn_ipc_native_stdout_read_line(ptr, i64)\n"
        "declare ptr @cn_ipc_native_stderr_read(ptr, i64)\n"
        "declare ptr @cn_ipc_native_stderr_read_line(ptr, i64)\n"
        "declare i32 @cn_ipc_native_wait(ptr, ptr)\n"
        "declare i32 @cn_ipc_native_kill(ptr)\n"
        "declare i32 @cn_ipc_native_release(ptr)\n\n",
        stream
    );

    fputs(
        "define private { i1, ptr } @cn_ipc_spawn(ptr %program, { ptr, i64 } %args) {\n"
        "entry:\n"
        "  %args.data = extractvalue { ptr, i64 } %args, 0\n"
        "  %args.len = extractvalue { ptr, i64 } %args, 1\n"
        "  %child = call ptr @cn_ipc_native_spawn(ptr %program, ptr %args.data, i64 %args.len)\n"
        "  %has.child = icmp ne ptr %child, null\n"
        "  br i1 %has.child, label %return.ok, label %return.err\n"
        "return.ok:\n"
        "  %ok = insertvalue { i1, ptr } zeroinitializer, i1 true, 0\n"
        "  %value = insertvalue { i1, ptr } %ok, ptr %child, 1\n"
        "  ret { i1, ptr } %value\n"
        "return.err:\n"
        "  ret { i1, ptr } zeroinitializer\n"
        "}\n\n",
        stream
    );

    fputs(
        "define private { i1, i64 } @cn_ipc_stdin_write(ptr %child, ptr %value) {\n"
        "entry:\n"
        "  %written = call i64 @cn_ipc_native_stdin_write(ptr %child, ptr %value)\n"
        "  %ok = icmp sge i64 %written, 0\n"
        "  br i1 %ok, label %return.ok, label %return.err\n"
        "return.ok:\n"
        "  %result.ok = insertvalue { i1, i64 } zeroinitializer, i1 true, 0\n"
        "  %result = insertvalue { i1, i64 } %result.ok, i64 %written, 1\n"
        "  ret { i1, i64 } %result\n"
        "return.err:\n"
        "  ret { i1, i64 } zeroinitializer\n"
        "}\n\n",
        stream
    );

    fputs(
        "define private { i1, i64 } @cn_ipc_stdin_write_line(ptr %child, ptr %value) {\n"
        "entry:\n"
        "  %written = call i64 @cn_ipc_native_stdin_write_line(ptr %child, ptr %value)\n"
        "  %ok = icmp sge i64 %written, 0\n"
        "  br i1 %ok, label %return.ok, label %return.err\n"
        "return.ok:\n"
        "  %result.ok = insertvalue { i1, i64 } zeroinitializer, i1 true, 0\n"
        "  %result = insertvalue { i1, i64 } %result.ok, i64 %written, 1\n"
        "  ret { i1, i64 } %result\n"
        "return.err:\n"
        "  ret { i1, i64 } zeroinitializer\n"
        "}\n\n",
        stream
    );

    fputs(
        "define private { i1, i1 } @cn_ipc_stdin_close(ptr %child) {\n"
        "entry:\n"
        "  %status = call i32 @cn_ipc_native_stdin_close(ptr %child)\n"
        "  %ok = icmp ne i32 %status, 0\n"
        "  br i1 %ok, label %return.ok, label %return.err\n"
        "return.ok:\n"
        "  %result.ok = insertvalue { i1, i1 } zeroinitializer, i1 true, 0\n"
        "  %result = insertvalue { i1, i1 } %result.ok, i1 true, 1\n"
        "  ret { i1, i1 } %result\n"
        "return.err:\n"
        "  ret { i1, i1 } zeroinitializer\n"
        "}\n\n",
        stream
    );

    fputs(
        "define private { i1, ptr } @cn_ipc_stdout_read(ptr %child, i64 %max_bytes) {\n"
        "entry:\n"
        "  %value = call ptr @cn_ipc_native_stdout_read(ptr %child, i64 %max_bytes)\n"
        "  %has.value = icmp ne ptr %value, null\n"
        "  br i1 %has.value, label %return.ok, label %return.err\n"
        "return.ok:\n"
        "  %result.ok = insertvalue { i1, ptr } zeroinitializer, i1 true, 0\n"
        "  %result = insertvalue { i1, ptr } %result.ok, ptr %value, 1\n"
        "  ret { i1, ptr } %result\n"
        "return.err:\n"
        "  ret { i1, ptr } zeroinitializer\n"
        "}\n\n",
        stream
    );

    fputs(
        "define private { i1, ptr } @cn_ipc_stdout_read_line(ptr %child, i64 %max_bytes) {\n"
        "entry:\n"
        "  %value = call ptr @cn_ipc_native_stdout_read_line(ptr %child, i64 %max_bytes)\n"
        "  %has.value = icmp ne ptr %value, null\n"
        "  br i1 %has.value, label %return.ok, label %return.err\n"
        "return.ok:\n"
        "  %result.ok = insertvalue { i1, ptr } zeroinitializer, i1 true, 0\n"
        "  %result = insertvalue { i1, ptr } %result.ok, ptr %value, 1\n"
        "  ret { i1, ptr } %result\n"
        "return.err:\n"
        "  ret { i1, ptr } zeroinitializer\n"
        "}\n\n",
        stream
    );

    fputs(
        "define private { i1, ptr } @cn_ipc_request_line(ptr %child, ptr %value, i64 %max_bytes) {\n"
        "entry:\n"
        "  %write = call { i1, i64 } @cn_ipc_stdin_write_line(ptr %child, ptr %value)\n"
        "  %write.ok = extractvalue { i1, i64 } %write, 0\n"
        "  br i1 %write.ok, label %check.write, label %return.err\n"
        "check.write:\n"
        "  %written = extractvalue { i1, i64 } %write, 1\n"
        "  %len = call i64 @strlen(ptr %value)\n"
        "  %expected = add i64 %len, 1\n"
        "  %full = icmp eq i64 %written, %expected\n"
        "  br i1 %full, label %read.line, label %return.err\n"
        "read.line:\n"
        "  %line = call { i1, ptr } @cn_ipc_stdout_read_line(ptr %child, i64 %max_bytes)\n"
        "  %line.ok = extractvalue { i1, ptr } %line, 0\n"
        "  br i1 %line.ok, label %return.ok, label %return.err\n"
        "return.ok:\n"
        "  ret { i1, ptr } %line\n"
        "return.err:\n"
        "  ret { i1, ptr } zeroinitializer\n"
        "}\n\n",
        stream
    );

    fputs(
        "define private { i1, ptr } @cn_ipc_stderr_read(ptr %child, i64 %max_bytes) {\n"
        "entry:\n"
        "  %value = call ptr @cn_ipc_native_stderr_read(ptr %child, i64 %max_bytes)\n"
        "  %has.value = icmp ne ptr %value, null\n"
        "  br i1 %has.value, label %return.ok, label %return.err\n"
        "return.ok:\n"
        "  %result.ok = insertvalue { i1, ptr } zeroinitializer, i1 true, 0\n"
        "  %result = insertvalue { i1, ptr } %result.ok, ptr %value, 1\n"
        "  ret { i1, ptr } %result\n"
        "return.err:\n"
        "  ret { i1, ptr } zeroinitializer\n"
        "}\n\n",
        stream
    );

    fputs(
        "define private { i1, ptr } @cn_ipc_stderr_read_line(ptr %child, i64 %max_bytes) {\n"
        "entry:\n"
        "  %value = call ptr @cn_ipc_native_stderr_read_line(ptr %child, i64 %max_bytes)\n"
        "  %has.value = icmp ne ptr %value, null\n"
        "  br i1 %has.value, label %return.ok, label %return.err\n"
        "return.ok:\n"
        "  %result.ok = insertvalue { i1, ptr } zeroinitializer, i1 true, 0\n"
        "  %result = insertvalue { i1, ptr } %result.ok, ptr %value, 1\n"
        "  ret { i1, ptr } %result\n"
        "return.err:\n"
        "  ret { i1, ptr } zeroinitializer\n"
        "}\n\n",
        stream
    );

    fputs(
        "define private { i1, i64 } @cn_ipc_wait(ptr %child) {\n"
        "entry:\n"
        "  %status.ptr = alloca i64\n"
        "  %wait.status = call i32 @cn_ipc_native_wait(ptr %child, ptr %status.ptr)\n"
        "  %ok = icmp ne i32 %wait.status, 0\n"
        "  br i1 %ok, label %return.ok, label %return.err\n"
        "return.ok:\n"
        "  %status = load i64, ptr %status.ptr\n"
        "  %result.ok = insertvalue { i1, i64 } zeroinitializer, i1 true, 0\n"
        "  %result = insertvalue { i1, i64 } %result.ok, i64 %status, 1\n"
        "  ret { i1, i64 } %result\n"
        "return.err:\n"
        "  ret { i1, i64 } zeroinitializer\n"
        "}\n\n",
        stream
    );

    fputs(
        "define private { i1, i1 } @cn_ipc_kill(ptr %child) {\n"
        "entry:\n"
        "  %status = call i32 @cn_ipc_native_kill(ptr %child)\n"
        "  %ok = icmp ne i32 %status, 0\n"
        "  br i1 %ok, label %return.ok, label %return.err\n"
        "return.ok:\n"
        "  %result.ok = insertvalue { i1, i1 } zeroinitializer, i1 true, 0\n"
        "  %result = insertvalue { i1, i1 } %result.ok, i1 true, 1\n"
        "  ret { i1, i1 } %result\n"
        "return.err:\n"
        "  ret { i1, i1 } zeroinitializer\n"
        "}\n\n",
        stream
    );

    fputs(
        "define private { i1, i1 } @cn_ipc_release(ptr %child) {\n"
        "entry:\n"
        "  %status = call i32 @cn_ipc_native_release(ptr %child)\n"
        "  %ok = icmp ne i32 %status, 0\n"
        "  br i1 %ok, label %return.ok, label %return.err\n"
        "return.ok:\n"
        "  %result.ok = insertvalue { i1, i1 } zeroinitializer, i1 true, 0\n"
        "  %result = insertvalue { i1, i1 } %result.ok, i1 true, 1\n"
        "  ret { i1, i1 } %result\n"
        "return.err:\n"
        "  ret { i1, i1 } zeroinitializer\n"
        "}\n\n",
        stream
    );
}
