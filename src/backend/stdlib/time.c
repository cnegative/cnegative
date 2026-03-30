#include "cnegative/llvm_runtime.h"

void cn_llvm_emit_runtime_time(FILE *stream) {
#ifdef _WIN32
    fputs(
        "define private i64 @cn_time_now_ms() {\n"
        "entry:\n"
        "  %filetime = alloca %cn_filetime\n"
        "  call void @GetSystemTimeAsFileTime(ptr %filetime)\n"
        "  %low.ptr = getelementptr inbounds %cn_filetime, ptr %filetime, i32 0, i32 0\n"
        "  %low.raw = load i32, ptr %low.ptr\n"
        "  %high.ptr = getelementptr inbounds %cn_filetime, ptr %filetime, i32 0, i32 1\n"
        "  %high.raw = load i32, ptr %high.ptr\n"
        "  %low = zext i32 %low.raw to i64\n"
        "  %high = zext i32 %high.raw to i64\n"
        "  %high.shift = shl i64 %high, 32\n"
        "  %ticks = or i64 %high.shift, %low\n"
        "  %millis.win = udiv i64 %ticks, 10000\n"
        "  %millis.unix = sub i64 %millis.win, 11644473600000\n"
        "  ret i64 %millis.unix\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private void @cn_time_sleep_ms(i64 %duration_ms) {\n"
        "entry:\n"
        "  %positive = icmp sgt i64 %duration_ms, 0\n"
        "  br i1 %positive, label %sleep, label %done\n"
        "sleep:\n"
        "  %ms = trunc i64 %duration_ms to i32\n"
        "  call void @Sleep(i32 %ms)\n"
        "  br label %done\n"
        "done:\n"
        "  ret void\n"
        "}\n\n",
        stream
    );
#else
    fputs(
        "define private i64 @cn_time_now_ms() {\n"
        "entry:\n"
        "  %ts = alloca %cn_timespec\n"
        "  %ok = call i32 @timespec_get(ptr %ts, i32 1)\n"
        "  %has = icmp eq i32 %ok, 1\n"
        "  br i1 %has, label %read, label %fallback\n"
        "read:\n"
        "  %sec.ptr = getelementptr inbounds %cn_timespec, ptr %ts, i32 0, i32 0\n"
        "  %sec = load i64, ptr %sec.ptr\n"
        "  %nsec.ptr = getelementptr inbounds %cn_timespec, ptr %ts, i32 0, i32 1\n"
        "  %nsec = load i64, ptr %nsec.ptr\n"
        "  %sec.ms = mul i64 %sec, 1000\n"
        "  %nsec.ms = udiv i64 %nsec, 1000000\n"
        "  %total = add i64 %sec.ms, %nsec.ms\n"
        "  ret i64 %total\n"
        "fallback:\n"
        "  ret i64 0\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private void @cn_time_sleep_ms(i64 %duration_ms) {\n"
        "entry:\n"
        "  %positive = icmp sgt i64 %duration_ms, 0\n"
        "  br i1 %positive, label %sleep, label %done\n"
        "sleep:\n"
        "  %micros = mul i64 %duration_ms, 1000\n"
        "  %micros32 = trunc i64 %micros to i32\n"
        "  %sleep.code = call i32 @usleep(i32 %micros32)\n"
        "  br label %done\n"
        "done:\n"
        "  ret void\n"
        "}\n\n",
        stream
    );
#endif
}
