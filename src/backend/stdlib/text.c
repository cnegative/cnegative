#include "cnegative/llvm_runtime.h"

#include <stdio.h>

void cn_llvm_emit_runtime_text(FILE *stream) {
    fputs("%cn_std_x2E_text__Builder = type { ptr, i64, i64 }\n", stream);
    fputs(
        "define private { i1, ptr } @cn_text_new() {\n"
        "entry:\n"
        "  %result = call { i1, ptr } @cn_bytes_new()\n"
        "  ret { i1, ptr } %result\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, ptr } @cn_text_with_capacity(i64 %capacity) {\n"
        "entry:\n"
        "  %result = call { i1, ptr } @cn_bytes_with_capacity(i64 %capacity)\n"
        "  ret { i1, ptr } %result\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, i1 } @cn_text_free(ptr %builder) {\n"
        "entry:\n"
        "  %result = call { i1, i1 } @cn_bytes_free(ptr %builder)\n"
        "  ret { i1, i1 } %result\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, i1 } @cn_text_clear(ptr %builder) {\n"
        "entry:\n"
        "  %result = call { i1, i1 } @cn_bytes_clear(ptr %builder)\n"
        "  ret { i1, i1 } %result\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private i64 @cn_text_length(ptr %builder) {\n"
        "entry:\n"
        "  %value = call i64 @cn_bytes_length(ptr %builder)\n"
        "  ret i64 %value\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private i64 @cn_text_capacity(ptr %builder) {\n"
        "entry:\n"
        "  %value = call i64 @cn_bytes_capacity(ptr %builder)\n"
        "  ret i64 %value\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, i1 } @cn_text_push_byte(ptr %builder, i8 %value) {\n"
        "entry:\n"
        "  %result = call { i1, i1 } @cn_bytes_push(ptr %builder, i8 %value)\n"
        "  ret { i1, i1 } %result\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { ptr, i64 } @cn_text_slice(ptr %builder) {\n"
        "entry:\n"
        "  %value = call { ptr, i64 } @cn_bytes_slice(ptr %builder)\n"
        "  ret { ptr, i64 } %value\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, i1 } @cn_text_append(ptr %builder, ptr %value) {\n"
        "entry:\n"
        "  %len = call i64 @strlen(ptr %value)\n"
        "  %slice.ptr = insertvalue { ptr, i64 } zeroinitializer, ptr %value, 0\n"
        "  %slice = insertvalue { ptr, i64 } %slice.ptr, i64 %len, 1\n"
        "  %result = call { i1, i1 } @cn_bytes_append(ptr %builder, { ptr, i64 } %slice)\n"
        "  ret { i1, i1 } %result\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, ptr } @cn_text_build(ptr %builder) {\n"
        "entry:\n"
        "  %has.builder = icmp ne ptr %builder, null\n"
        "  br i1 %has.builder, label %load, label %return.err\n"
        "load:\n"
        "  %data.ptr = getelementptr inbounds %cn_std_x2E_text__Builder, ptr %builder, i32 0, i32 0\n"
        "  %length.ptr = getelementptr inbounds %cn_std_x2E_text__Builder, ptr %builder, i32 0, i32 1\n"
        "  %data = load ptr, ptr %data.ptr\n"
        "  %length = load i64, ptr %length.ptr\n"
        "  %string = call ptr @cn_dup_range(ptr %data, i64 %length)\n"
        "  %ok = insertvalue { i1, ptr } zeroinitializer, i1 true, 0\n"
        "  %value = insertvalue { i1, ptr } %ok, ptr %string, 1\n"
        "  ret { i1, ptr } %value\n"
        "return.err:\n"
        "  ret { i1, ptr } zeroinitializer\n"
        "}\n\n",
        stream
    );
}
