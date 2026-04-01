#include "cnegative/llvm_runtime.h"

#include <stdio.h>

void cn_llvm_emit_runtime_bytes(FILE *stream) {
    fputs("%cn_std_x2E_bytes__Buffer = type { ptr, i64, i64 }\n", stream);
    fputs(
        "define private i64 @cn_bytes_normalize_capacity(i64 %requested) {\n"
        "entry:\n"
        "  %enough = icmp sge i64 %requested, 16\n"
        "  %value = select i1 %enough, i64 %requested, i64 16\n"
        "  ret i64 %value\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, i1 } @cn_bytes_buffer_ensure(ptr %buffer, i64 %needed) {\n"
        "entry:\n"
        "  %has.buffer = icmp ne ptr %buffer, null\n"
        "  br i1 %has.buffer, label %load, label %return.err\n"
        "load:\n"
        "  %data.ptr = getelementptr inbounds %cn_std_x2E_bytes__Buffer, ptr %buffer, i32 0, i32 0\n"
        "  %capacity.ptr = getelementptr inbounds %cn_std_x2E_bytes__Buffer, ptr %buffer, i32 0, i32 2\n"
        "  %data = load ptr, ptr %data.ptr\n"
        "  %capacity = load i64, ptr %capacity.ptr\n"
        "  %enough = icmp sge i64 %capacity, %needed\n"
        "  br i1 %enough, label %return.ok, label %grow\n"
        "grow:\n"
        "  %has.capacity = icmp sgt i64 %capacity, 0\n"
        "  %double = shl i64 %capacity, 1\n"
        "  %base = select i1 %has.capacity, i64 %double, i64 16\n"
        "  %base.enough = icmp sge i64 %base, %needed\n"
        "  %new.capacity = select i1 %base.enough, i64 %base, i64 %needed\n"
        "  %new.data = call ptr @realloc(ptr %data, i64 %new.capacity)\n"
        "  %has.new.data = icmp ne ptr %new.data, null\n"
        "  br i1 %has.new.data, label %store, label %return.err\n"
        "store:\n"
        "  store ptr %new.data, ptr %data.ptr\n"
        "  store i64 %new.capacity, ptr %capacity.ptr\n"
        "  br label %return.ok\n"
        "return.ok:\n"
        "  %ok = insertvalue { i1, i1 } zeroinitializer, i1 true, 0\n"
        "  %value = insertvalue { i1, i1 } %ok, i1 true, 1\n"
        "  ret { i1, i1 } %value\n"
        "return.err:\n"
        "  ret { i1, i1 } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, ptr } @cn_bytes_new() {\n"
        "entry:\n"
        "  %result = call { i1, ptr } @cn_bytes_with_capacity(i64 16)\n"
        "  ret { i1, ptr } %result\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, ptr } @cn_bytes_with_capacity(i64 %capacity) {\n"
        "entry:\n"
        "  %norm = call i64 @cn_bytes_normalize_capacity(i64 %capacity)\n"
        "  %buffer.size.ptr = getelementptr %cn_std_x2E_bytes__Buffer, ptr null, i32 1\n"
        "  %buffer.size = ptrtoint ptr %buffer.size.ptr to i64\n"
        "  %buffer = call ptr @malloc(i64 %buffer.size)\n"
        "  %has.buffer = icmp ne ptr %buffer, null\n"
        "  br i1 %has.buffer, label %init, label %return.err\n"
        "init:\n"
        "  %data.ptr = getelementptr inbounds %cn_std_x2E_bytes__Buffer, ptr %buffer, i32 0, i32 0\n"
        "  %length.ptr = getelementptr inbounds %cn_std_x2E_bytes__Buffer, ptr %buffer, i32 0, i32 1\n"
        "  %capacity.ptr = getelementptr inbounds %cn_std_x2E_bytes__Buffer, ptr %buffer, i32 0, i32 2\n"
        "  store ptr null, ptr %data.ptr\n"
        "  store i64 0, ptr %length.ptr\n"
        "  store i64 0, ptr %capacity.ptr\n"
        "  %ensure = call { i1, i1 } @cn_bytes_buffer_ensure(ptr %buffer, i64 %norm)\n"
        "  %ensure.ok = extractvalue { i1, i1 } %ensure, 0\n"
        "  br i1 %ensure.ok, label %return.ok, label %free.buffer\n"
        "free.buffer:\n"
        "  call void @free(ptr %buffer)\n"
        "  br label %return.err\n"
        "return.ok:\n"
        "  %ok = insertvalue { i1, ptr } zeroinitializer, i1 true, 0\n"
        "  %value = insertvalue { i1, ptr } %ok, ptr %buffer, 1\n"
        "  ret { i1, ptr } %value\n"
        "return.err:\n"
        "  ret { i1, ptr } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, i1 } @cn_bytes_free(ptr %buffer) {\n"
        "entry:\n"
        "  %has.buffer = icmp ne ptr %buffer, null\n"
        "  br i1 %has.buffer, label %free.data, label %return.err\n"
        "free.data:\n"
        "  %data.ptr = getelementptr inbounds %cn_std_x2E_bytes__Buffer, ptr %buffer, i32 0, i32 0\n"
        "  %data = load ptr, ptr %data.ptr\n"
        "  %has.data = icmp ne ptr %data, null\n"
        "  br i1 %has.data, label %do.free.data, label %free.buffer\n"
        "do.free.data:\n"
        "  call void @free(ptr %data)\n"
        "  br label %free.buffer\n"
        "free.buffer:\n"
        "  call void @free(ptr %buffer)\n"
        "  %ok = insertvalue { i1, i1 } zeroinitializer, i1 true, 0\n"
        "  %value = insertvalue { i1, i1 } %ok, i1 true, 1\n"
        "  ret { i1, i1 } %value\n"
        "return.err:\n"
        "  ret { i1, i1 } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, i1 } @cn_bytes_clear(ptr %buffer) {\n"
        "entry:\n"
        "  %has.buffer = icmp ne ptr %buffer, null\n"
        "  br i1 %has.buffer, label %store.zero, label %return.err\n"
        "store.zero:\n"
        "  %length.ptr = getelementptr inbounds %cn_std_x2E_bytes__Buffer, ptr %buffer, i32 0, i32 1\n"
        "  store i64 0, ptr %length.ptr\n"
        "  %ok = insertvalue { i1, i1 } zeroinitializer, i1 true, 0\n"
        "  %value = insertvalue { i1, i1 } %ok, i1 true, 1\n"
        "  ret { i1, i1 } %value\n"
        "return.err:\n"
        "  ret { i1, i1 } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private i64 @cn_bytes_length(ptr %buffer) {\n"
        "entry:\n"
        "  %has.buffer = icmp ne ptr %buffer, null\n"
        "  br i1 %has.buffer, label %load, label %return.zero\n"
        "load:\n"
        "  %length.ptr = getelementptr inbounds %cn_std_x2E_bytes__Buffer, ptr %buffer, i32 0, i32 1\n"
        "  %length = load i64, ptr %length.ptr\n"
        "  ret i64 %length\n"
        "return.zero:\n"
        "  ret i64 0\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private i64 @cn_bytes_capacity(ptr %buffer) {\n"
        "entry:\n"
        "  %has.buffer = icmp ne ptr %buffer, null\n"
        "  br i1 %has.buffer, label %load, label %return.zero\n"
        "load:\n"
        "  %capacity.ptr = getelementptr inbounds %cn_std_x2E_bytes__Buffer, ptr %buffer, i32 0, i32 2\n"
        "  %capacity = load i64, ptr %capacity.ptr\n"
        "  ret i64 %capacity\n"
        "return.zero:\n"
        "  ret i64 0\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { ptr, i64 } @cn_bytes_slice(ptr %buffer) {\n"
        "entry:\n"
        "  %has.buffer = icmp ne ptr %buffer, null\n"
        "  br i1 %has.buffer, label %load, label %return.empty\n"
        "load:\n"
        "  %data.ptr = getelementptr inbounds %cn_std_x2E_bytes__Buffer, ptr %buffer, i32 0, i32 0\n"
        "  %length.ptr = getelementptr inbounds %cn_std_x2E_bytes__Buffer, ptr %buffer, i32 0, i32 1\n"
        "  %data = load ptr, ptr %data.ptr\n"
        "  %length = load i64, ptr %length.ptr\n"
        "  %value.data = insertvalue { ptr, i64 } zeroinitializer, ptr %data, 0\n"
        "  %value = insertvalue { ptr, i64 } %value.data, i64 %length, 1\n"
        "  ret { ptr, i64 } %value\n"
        "return.empty:\n"
        "  ret { ptr, i64 } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, i1 } @cn_bytes_push(ptr %buffer, i8 %value) {\n"
        "entry:\n"
        "  %has.buffer = icmp ne ptr %buffer, null\n"
        "  br i1 %has.buffer, label %load, label %return.err\n"
        "load:\n"
        "  %length.ptr = getelementptr inbounds %cn_std_x2E_bytes__Buffer, ptr %buffer, i32 0, i32 1\n"
        "  %length = load i64, ptr %length.ptr\n"
        "  %needed = add i64 %length, 1\n"
        "  %ensure = call { i1, i1 } @cn_bytes_buffer_ensure(ptr %buffer, i64 %needed)\n"
        "  %ensure.ok = extractvalue { i1, i1 } %ensure, 0\n"
        "  br i1 %ensure.ok, label %store.byte, label %return.err\n"
        "store.byte:\n"
        "  %data.ptr = getelementptr inbounds %cn_std_x2E_bytes__Buffer, ptr %buffer, i32 0, i32 0\n"
        "  %data = load ptr, ptr %data.ptr\n"
        "  %byte.ptr = getelementptr inbounds i8, ptr %data, i64 %length\n"
        "  store i8 %value, ptr %byte.ptr\n"
        "  store i64 %needed, ptr %length.ptr\n"
        "  %ok = insertvalue { i1, i1 } zeroinitializer, i1 true, 0\n"
        "  %result = insertvalue { i1, i1 } %ok, i1 true, 1\n"
        "  ret { i1, i1 } %result\n"
        "return.err:\n"
        "  ret { i1, i1 } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, i1 } @cn_bytes_append(ptr %buffer, { ptr, i64 } %values) {\n"
        "entry:\n"
        "  %has.buffer = icmp ne ptr %buffer, null\n"
        "  br i1 %has.buffer, label %load.values, label %return.err\n"
        "load.values:\n"
        "  %src = extractvalue { ptr, i64 } %values, 0\n"
        "  %extra = extractvalue { ptr, i64 } %values, 1\n"
        "  %has.extra = icmp sgt i64 %extra, 0\n"
        "  br i1 %has.extra, label %load.buffer, label %return.ok\n"
        "load.buffer:\n"
        "  %length.ptr = getelementptr inbounds %cn_std_x2E_bytes__Buffer, ptr %buffer, i32 0, i32 1\n"
        "  %length = load i64, ptr %length.ptr\n"
        "  %needed = add i64 %length, %extra\n"
        "  %ensure = call { i1, i1 } @cn_bytes_buffer_ensure(ptr %buffer, i64 %needed)\n"
        "  %ensure.ok = extractvalue { i1, i1 } %ensure, 0\n"
        "  br i1 %ensure.ok, label %copy, label %return.err\n"
        "copy:\n"
        "  %data.ptr = getelementptr inbounds %cn_std_x2E_bytes__Buffer, ptr %buffer, i32 0, i32 0\n"
        "  %data = load ptr, ptr %data.ptr\n"
        "  %dst = getelementptr inbounds i8, ptr %data, i64 %length\n"
        "  %copied = call ptr @memcpy(ptr %dst, ptr %src, i64 %extra)\n"
        "  store i64 %needed, ptr %length.ptr\n"
        "  br label %return.ok\n"
        "return.ok:\n"
        "  %ok = insertvalue { i1, i1 } zeroinitializer, i1 true, 0\n"
        "  %result = insertvalue { i1, i1 } %ok, i1 true, 1\n"
        "  ret { i1, i1 } %result\n"
        "return.err:\n"
        "  ret { i1, i1 } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, i8 } @cn_bytes_get(ptr %buffer, i64 %index) {\n"
        "entry:\n"
        "  %has.buffer = icmp ne ptr %buffer, null\n"
        "  %index.low = icmp sge i64 %index, 0\n"
        "  %guard0 = and i1 %has.buffer, %index.low\n"
        "  br i1 %guard0, label %load, label %return.err\n"
        "load:\n"
        "  %length.ptr = getelementptr inbounds %cn_std_x2E_bytes__Buffer, ptr %buffer, i32 0, i32 1\n"
        "  %length = load i64, ptr %length.ptr\n"
        "  %index.high = icmp slt i64 %index, %length\n"
        "  br i1 %index.high, label %load.byte, label %return.err\n"
        "load.byte:\n"
        "  %data.ptr = getelementptr inbounds %cn_std_x2E_bytes__Buffer, ptr %buffer, i32 0, i32 0\n"
        "  %data = load ptr, ptr %data.ptr\n"
        "  %byte.ptr = getelementptr inbounds i8, ptr %data, i64 %index\n"
        "  %byte = load i8, ptr %byte.ptr\n"
        "  %ok = insertvalue { i1, i8 } zeroinitializer, i1 true, 0\n"
        "  %value = insertvalue { i1, i8 } %ok, i8 %byte, 1\n"
        "  ret { i1, i8 } %value\n"
        "return.err:\n"
        "  ret { i1, i8 } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, i1 } @cn_bytes_set(ptr %buffer, i64 %index, i8 %value) {\n"
        "entry:\n"
        "  %has.buffer = icmp ne ptr %buffer, null\n"
        "  %index.low = icmp sge i64 %index, 0\n"
        "  %guard0 = and i1 %has.buffer, %index.low\n"
        "  br i1 %guard0, label %load, label %return.err\n"
        "load:\n"
        "  %length.ptr = getelementptr inbounds %cn_std_x2E_bytes__Buffer, ptr %buffer, i32 0, i32 1\n"
        "  %length = load i64, ptr %length.ptr\n"
        "  %index.high = icmp slt i64 %index, %length\n"
        "  br i1 %index.high, label %store.byte, label %return.err\n"
        "store.byte:\n"
        "  %data.ptr = getelementptr inbounds %cn_std_x2E_bytes__Buffer, ptr %buffer, i32 0, i32 0\n"
        "  %data = load ptr, ptr %data.ptr\n"
        "  %byte.ptr = getelementptr inbounds i8, ptr %data, i64 %index\n"
        "  store i8 %value, ptr %byte.ptr\n"
        "  %ok = insertvalue { i1, i1 } zeroinitializer, i1 true, 0\n"
        "  %result = insertvalue { i1, i1 } %ok, i1 true, 1\n"
        "  ret { i1, i1 } %result\n"
        "return.err:\n"
        "  ret { i1, i1 } zeroinitializer\n"
        "}\n\n",
        stream
    );
}
