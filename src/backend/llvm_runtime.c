#include "cnegative/llvm_runtime.h"

#ifdef _WIN32
#define CN_LLVM_PATH_SEPARATOR 92
#else
#define CN_LLVM_PATH_SEPARATOR 47
#endif

void cn_llvm_emit_runtime_prelude(FILE *stream) {
    fputs("declare i32 @printf(ptr, ...)\n", stream);
    fputs("declare i32 @puts(ptr)\n", stream);
    fputs("declare i32 @getchar()\n", stream);
    fputs("declare i64 @strlen(ptr)\n", stream);
    fputs("declare i32 @strcmp(ptr, ptr)\n", stream);
    fputs("declare i32 @memcmp(ptr, ptr, i64)\n", stream);
    fputs("declare ptr @memcpy(ptr, ptr, i64)\n", stream);
    fputs("declare ptr @malloc(i64)\n", stream);
    fputs("declare ptr @realloc(ptr, i64)\n", stream);
    fputs("declare i32 @snprintf(ptr, i64, ptr, ...)\n", stream);
    fputs("declare void @free(ptr)\n", stream);
    fputs("declare ptr @fopen(ptr, ptr)\n", stream);
    fputs("declare i32 @fclose(ptr)\n", stream);
    fputs("declare i32 @fgetc(ptr)\n", stream);
    fputs("declare i64 @fwrite(ptr, i64, i64, ptr)\n", stream);
    fputs("declare ptr @getenv(ptr)\n", stream);
    fputs("declare i32 @remove(ptr)\n", stream);
    fputs("declare i32 @rename(ptr, ptr)\n", stream);
    fputs("declare void @exit(i32)\n", stream);
#ifdef _WIN32
    fputs("declare ptr @_getcwd(ptr, i32)\n", stream);
    fputs("declare void @Sleep(i32)\n", stream);
    fputs("declare void @GetSystemTimeAsFileTime(ptr)\n", stream);
    fputs("declare i32 @_mkdir(ptr)\n", stream);
    fputs("declare i32 @_rmdir(ptr)\n", stream);
    fputs("%cn_filetime = type { i32, i32 }\n", stream);
#else
    fputs("declare i32 @timespec_get(ptr, i32)\n", stream);
    fputs("declare ptr @getcwd(ptr, i64)\n", stream);
    fputs("declare i32 @usleep(i32)\n", stream);
    fputs("declare i32 @mkdir(ptr, i32)\n", stream);
    fputs("declare i32 @rmdir(ptr)\n", stream);
    fputs("%cn_timespec = type { i64, i64 }\n", stream);
#endif
    fputs("@.cn.int_fmt = private unnamed_addr constant [6 x i8] c\"%lld\\0A\\00\"\n", stream);
    fputs("@.cn.str_fmt = private unnamed_addr constant [3 x i8] c\"%s\\00\"\n", stream);
    fputs("@.cn.host_port_fmt = private unnamed_addr constant [8 x i8] c\"%s:%lld\\00\"\n", stream);
    fputs("@.cn.empty = private unnamed_addr constant [1 x i8] c\"\\00\"\n", stream);
    fputs("@.cn.true = private unnamed_addr constant [5 x i8] c\"true\\00\"\n", stream);
    fputs("@.cn.false = private unnamed_addr constant [6 x i8] c\"false\\00\"\n", stream);
#ifdef _WIN32
    fputs("@.cn.process.platform = private unnamed_addr constant [8 x i8] c\"windows\\00\"\n", stream);
#elif defined(__APPLE__)
    fputs("@.cn.process.platform = private unnamed_addr constant [6 x i8] c\"macos\\00\"\n", stream);
#elif defined(__linux__)
    fputs("@.cn.process.platform = private unnamed_addr constant [6 x i8] c\"linux\\00\"\n", stream);
#else
    fputs("@.cn.process.platform = private unnamed_addr constant [8 x i8] c\"unknown\\00\"\n", stream);
#endif
#if defined(__x86_64__) || defined(_M_X64)
    fputs("@.cn.process.arch = private unnamed_addr constant [7 x i8] c\"x86_64\\00\"\n", stream);
#elif defined(__aarch64__) || defined(_M_ARM64)
    fputs("@.cn.process.arch = private unnamed_addr constant [6 x i8] c\"arm64\\00\"\n", stream);
#else
    fputs("@.cn.process.arch = private unnamed_addr constant [8 x i8] c\"unknown\\00\"\n", stream);
#endif
    fputs("@.cn.mode.read = private unnamed_addr constant [3 x i8] c\"rb\\00\"\n", stream);
    fputs("@.cn.mode.write = private unnamed_addr constant [3 x i8] c\"wb\\00\"\n", stream);
    fputs("@.cn.mode.append = private unnamed_addr constant [3 x i8] c\"ab\\00\"\n", stream);
    fputs("@.cn.input_buffer = internal global [4096 x i8] zeroinitializer\n", stream);
    fputs("@.cn.str_head = internal global ptr null\n", stream);
    fputs(
        "\n"
        "define private void @cn_track_str(ptr %value) {\n"
        "entry:\n"
        "  %node = call ptr @malloc(i64 16)\n"
        "  %has.node = icmp ne ptr %node, null\n"
        "  br i1 %has.node, label %link, label %done\n"
        "link:\n"
        "  %node.value = getelementptr inbounds { ptr, ptr }, ptr %node, i32 0, i32 0\n"
        "  %node.next = getelementptr inbounds { ptr, ptr }, ptr %node, i32 0, i32 1\n"
        "  %head = load ptr, ptr @.cn.str_head\n"
        "  store ptr %value, ptr %node.value\n"
        "  store ptr %head, ptr %node.next\n"
        "  store ptr %node, ptr @.cn.str_head\n"
        "  br label %done\n"
        "done:\n"
        "  ret void\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private void @cn_free_str(ptr %value) {\n"
        "entry:\n"
        "  %prev.slot = alloca ptr\n"
        "  %curr.slot = alloca ptr\n"
        "  store ptr null, ptr %prev.slot\n"
        "  %head = load ptr, ptr @.cn.str_head\n"
        "  store ptr %head, ptr %curr.slot\n"
        "  br label %loop\n"
        "loop:\n"
        "  %curr = load ptr, ptr %curr.slot\n"
        "  %is.null = icmp eq ptr %curr, null\n"
        "  br i1 %is.null, label %done, label %check\n"
        "check:\n"
        "  %curr.value.ptr = getelementptr inbounds { ptr, ptr }, ptr %curr, i32 0, i32 0\n"
        "  %curr.value = load ptr, ptr %curr.value.ptr\n"
        "  %matches = icmp eq ptr %curr.value, %value\n"
        "  br i1 %matches, label %unlink, label %advance\n"
        "advance:\n"
        "  %curr.next.ptr = getelementptr inbounds { ptr, ptr }, ptr %curr, i32 0, i32 1\n"
        "  %next = load ptr, ptr %curr.next.ptr\n"
        "  store ptr %curr, ptr %prev.slot\n"
        "  store ptr %next, ptr %curr.slot\n"
        "  br label %loop\n"
        "unlink:\n"
        "  %curr.next.ptr2 = getelementptr inbounds { ptr, ptr }, ptr %curr, i32 0, i32 1\n"
        "  %next2 = load ptr, ptr %curr.next.ptr2\n"
        "  %prev = load ptr, ptr %prev.slot\n"
        "  %has.prev = icmp ne ptr %prev, null\n"
        "  br i1 %has.prev, label %unlink.prev, label %unlink.head\n"
        "unlink.prev:\n"
        "  %prev.next.ptr = getelementptr inbounds { ptr, ptr }, ptr %prev, i32 0, i32 1\n"
        "  store ptr %next2, ptr %prev.next.ptr\n"
        "  br label %free.node\n"
        "unlink.head:\n"
        "  store ptr %next2, ptr @.cn.str_head\n"
        "  br label %free.node\n"
        "free.node:\n"
        "  call void @free(ptr %curr.value)\n"
        "  call void @free(ptr %curr)\n"
        "  ret void\n"
        "done:\n"
        "  ret void\n"
        "}\n\n",
        stream
    );
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
    fputs(
        "define private { i1, i1 } @cn_parse_bool(ptr %value) {\n"
        "entry:\n"
        "  %true.ptr = getelementptr inbounds [5 x i8], ptr @.cn.true, i64 0, i64 0\n"
        "  %false.ptr = getelementptr inbounds [6 x i8], ptr @.cn.false, i64 0, i64 0\n"
        "  %cmp.true = call i32 @strcmp(ptr %value, ptr %true.ptr)\n"
        "  %is.true = icmp eq i32 %cmp.true, 0\n"
        "  br i1 %is.true, label %return.true, label %check.false\n"
        "check.false:\n"
        "  %cmp.false = call i32 @strcmp(ptr %value, ptr %false.ptr)\n"
        "  %is.false = icmp eq i32 %cmp.false, 0\n"
        "  br i1 %is.false, label %return.false, label %return.err\n"
        "return.true:\n"
        "  %ok.true = insertvalue { i1, i1 } zeroinitializer, i1 true, 0\n"
        "  %value.true = insertvalue { i1, i1 } %ok.true, i1 true, 1\n"
        "  ret { i1, i1 } %value.true\n"
        "return.false:\n"
        "  %ok.false = insertvalue { i1, i1 } zeroinitializer, i1 true, 0\n"
        "  %value.false = insertvalue { i1, i1 } %ok.false, i1 false, 1\n"
        "  ret { i1, i1 } %value.false\n"
        "return.err:\n"
        "  ret { i1, i1 } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, i64 } @cn_parse_int(ptr %value) {\n"
        "entry:\n"
        "  %first = load i8, ptr %value\n"
        "  %is.empty = icmp eq i8 %first, 0\n"
        "  br i1 %is.empty, label %return.err, label %setup\n"
        "setup:\n"
        "  %index.slot = alloca i64\n"
        "  %acc.slot = alloca i64\n"
        "  %negative.slot = alloca i1\n"
        "  store i64 0, ptr %index.slot\n"
        "  store i64 0, ptr %acc.slot\n"
        "  store i1 false, ptr %negative.slot\n"
        "  %is.minus = icmp eq i8 %first, 45\n"
        "  br i1 %is.minus, label %sign.minus, label %check.plus\n"
        "sign.minus:\n"
        "  %second.minus.ptr = getelementptr inbounds i8, ptr %value, i64 1\n"
        "  %second.minus = load i8, ptr %second.minus.ptr\n"
        "  %minus.has.digit = icmp ne i8 %second.minus, 0\n"
        "  br i1 %minus.has.digit, label %set.minus, label %return.err\n"
        "set.minus:\n"
        "  store i1 true, ptr %negative.slot\n"
        "  store i64 1, ptr %index.slot\n"
        "  br label %loop\n"
        "check.plus:\n"
        "  %is.plus = icmp eq i8 %first, 43\n"
        "  br i1 %is.plus, label %sign.plus, label %loop\n"
        "sign.plus:\n"
        "  %second.plus.ptr = getelementptr inbounds i8, ptr %value, i64 1\n"
        "  %second.plus = load i8, ptr %second.plus.ptr\n"
        "  %plus.has.digit = icmp ne i8 %second.plus, 0\n"
        "  br i1 %plus.has.digit, label %set.plus, label %return.err\n"
        "set.plus:\n"
        "  store i64 1, ptr %index.slot\n"
        "  br label %loop\n"
        "loop:\n"
        "  %index = load i64, ptr %index.slot\n"
        "  %char.ptr = getelementptr inbounds i8, ptr %value, i64 %index\n"
        "  %char = load i8, ptr %char.ptr\n"
        "  %is.end = icmp eq i8 %char, 0\n"
        "  br i1 %is.end, label %finish, label %check.digit\n"
        "check.digit:\n"
        "  %digit.low = icmp uge i8 %char, 48\n"
        "  %digit.high = icmp ule i8 %char, 57\n"
        "  %is.digit = and i1 %digit.low, %digit.high\n"
        "  br i1 %is.digit, label %accumulate, label %return.err\n"
        "accumulate:\n"
        "  %acc = load i64, ptr %acc.slot\n"
        "  %digit.byte = sub i8 %char, 48\n"
        "  %digit = zext i8 %digit.byte to i64\n"
        "  %mul = mul i64 %acc, 10\n"
        "  %next.acc = add i64 %mul, %digit\n"
        "  store i64 %next.acc, ptr %acc.slot\n"
        "  %next.index = add i64 %index, 1\n"
        "  store i64 %next.index, ptr %index.slot\n"
        "  br label %loop\n"
        "finish:\n"
        "  %acc.final = load i64, ptr %acc.slot\n"
        "  %negative = load i1, ptr %negative.slot\n"
        "  %negated = sub i64 0, %acc.final\n"
        "  %signed = select i1 %negative, i64 %negated, i64 %acc.final\n"
        "  %ok = insertvalue { i1, i64 } zeroinitializer, i1 true, 0\n"
        "  %value.ok = insertvalue { i1, i64 } %ok, i64 %signed, 1\n"
        "  ret { i1, i64 } %value.ok\n"
        "return.err:\n"
        "  ret { i1, i64 } zeroinitializer\n"
        "}\n\n",
        stream
    );
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
    fputs(
        "define private i1 @cn_net_is_ipv4(ptr %value) {\n"
        "entry:\n"
        "  %cursor.slot = alloca ptr\n"
        "  %segments.slot = alloca i64\n"
        "  %digits.slot = alloca i64\n"
        "  %octet.slot = alloca i64\n"
        "  store ptr %value, ptr %cursor.slot\n"
        "  store i64 0, ptr %segments.slot\n"
        "  store i64 0, ptr %digits.slot\n"
        "  store i64 0, ptr %octet.slot\n"
        "  br label %loop\n"
        "loop:\n"
        "  %cursor = load ptr, ptr %cursor.slot\n"
        "  %ch = load i8, ptr %cursor\n"
        "  %is.end = icmp eq i8 %ch, 0\n"
        "  br i1 %is.end, label %finish, label %check.dot\n"
        "check.dot:\n"
        "  %is.dot = icmp eq i8 %ch, 46\n"
        "  br i1 %is.dot, label %handle.dot, label %check.digit\n"
        "handle.dot:\n"
        "  %digits = load i64, ptr %digits.slot\n"
        "  %has.digits = icmp sgt i64 %digits, 0\n"
        "  br i1 %has.digits, label %advance.segment, label %return.false\n"
        "advance.segment:\n"
        "  %segments = load i64, ptr %segments.slot\n"
        "  %segments.next = add i64 %segments, 1\n"
        "  %too.many.segments = icmp sgt i64 %segments.next, 3\n"
        "  br i1 %too.many.segments, label %return.false, label %reset.segment\n"
        "reset.segment:\n"
        "  store i64 %segments.next, ptr %segments.slot\n"
        "  store i64 0, ptr %digits.slot\n"
        "  store i64 0, ptr %octet.slot\n"
        "  %next.cursor = getelementptr inbounds i8, ptr %cursor, i64 1\n"
        "  store ptr %next.cursor, ptr %cursor.slot\n"
        "  br label %loop\n"
        "check.digit:\n"
        "  %digit.low = icmp uge i8 %ch, 48\n"
        "  %digit.high = icmp ule i8 %ch, 57\n"
        "  %is.digit = and i1 %digit.low, %digit.high\n"
        "  br i1 %is.digit, label %accumulate, label %return.false\n"
        "accumulate:\n"
        "  %digits.old = load i64, ptr %digits.slot\n"
        "  %digits.next = add i64 %digits.old, 1\n"
        "  %too.long = icmp sgt i64 %digits.next, 3\n"
        "  br i1 %too.long, label %return.false, label %update.octet\n"
        "update.octet:\n"
        "  %octet.old = load i64, ptr %octet.slot\n"
        "  %digit.byte = sub i8 %ch, 48\n"
        "  %digit = zext i8 %digit.byte to i64\n"
        "  %octet.mul = mul i64 %octet.old, 10\n"
        "  %octet.next = add i64 %octet.mul, %digit\n"
        "  %octet.valid = icmp sle i64 %octet.next, 255\n"
        "  br i1 %octet.valid, label %store.values, label %return.false\n"
        "store.values:\n"
        "  store i64 %digits.next, ptr %digits.slot\n"
        "  store i64 %octet.next, ptr %octet.slot\n"
        "  %cursor.next = getelementptr inbounds i8, ptr %cursor, i64 1\n"
        "  store ptr %cursor.next, ptr %cursor.slot\n"
        "  br label %loop\n"
        "finish:\n"
        "  %digits.final = load i64, ptr %digits.slot\n"
        "  %has.final = icmp sgt i64 %digits.final, 0\n"
        "  br i1 %has.final, label %check.segments, label %return.false\n"
        "check.segments:\n"
        "  %segments.final = load i64, ptr %segments.slot\n"
        "  %is.ipv4 = icmp eq i64 %segments.final, 3\n"
        "  ret i1 %is.ipv4\n"
        "return.false:\n"
        "  ret i1 false\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private ptr @cn_net_join_host_port(ptr %host, i64 %port) {\n"
        "entry:\n"
        "  %host.len = call i64 @strlen(ptr %host)\n"
        "  %size = add i64 %host.len, 32\n"
        "  %dst = call ptr @malloc(i64 %size)\n"
        "  %has.dst = icmp ne ptr %dst, null\n"
        "  br i1 %has.dst, label %format, label %fallback\n"
        "format:\n"
        "  %fmt = getelementptr inbounds [8 x i8], ptr @.cn.host_port_fmt, i64 0, i64 0\n"
        "  %written = call i32 (ptr, i64, ptr, ...) @snprintf(ptr %dst, i64 %size, ptr %fmt, ptr %host, i64 %port)\n"
        "  %ok = icmp sge i32 %written, 0\n"
        "  br i1 %ok, label %return.ok, label %free.err\n"
        "return.ok:\n"
        "  call void @cn_track_str(ptr %dst)\n"
        "  ret ptr %dst\n"
        "free.err:\n"
        "  call void @free(ptr %dst)\n"
        "  br label %fallback\n"
        "fallback:\n"
        "  %empty.ptr = getelementptr inbounds [1 x i8], ptr @.cn.empty, i64 0, i64 0\n"
        "  ret ptr %empty.ptr\n"
        "}\n\n",
        stream
    );
    fprintf(
        stream,
        "define private ptr @cn_path_join(ptr %%left, ptr %%right) {\n"
        "entry:\n"
        "  %%left.len = call i64 @strlen(ptr %%left)\n"
        "  %%right.len = call i64 @strlen(ptr %%right)\n"
        "  %%left.empty = icmp eq i64 %%left.len, 0\n"
        "  br i1 %%left.empty, label %%return.right, label %%check.right.empty\n"
        "return.right:\n"
        "  %%right.copy.result = call ptr @cn_dup_cstr(ptr %%right)\n"
        "  ret ptr %%right.copy.result\n"
        "check.right.empty:\n"
        "  %%right.empty = icmp eq i64 %%right.len, 0\n"
        "  br i1 %%right.empty, label %%return.left, label %%check.sep\n"
        "return.left:\n"
        "  %%left.copy.result = call ptr @cn_dup_cstr(ptr %%left)\n"
        "  ret ptr %%left.copy.result\n"
        "check.sep:\n"
        "  %%left.last.offset = sub i64 %%left.len, 1\n"
        "  %%left.last.ptr = getelementptr inbounds i8, ptr %%left, i64 %%left.last.offset\n"
        "  %%left.last = load i8, ptr %%left.last.ptr\n"
        "  %%left.sep.slash = icmp eq i8 %%left.last, 47\n"
        "  %%left.sep.backslash = icmp eq i8 %%left.last, 92\n"
        "  %%left.sep = or i1 %%left.sep.slash, %%left.sep.backslash\n"
        "  %%right.first = load i8, ptr %%right\n"
        "  %%right.sep.slash = icmp eq i8 %%right.first, 47\n"
        "  %%right.sep.backslash = icmp eq i8 %%right.first, 92\n"
        "  %%right.sep = or i1 %%right.sep.slash, %%right.sep.backslash\n"
        "  %%skip.right = and i1 %%left.sep, %%right.sep\n"
        "  %%add.sep.candidate = xor i1 %%left.sep, true\n"
        "  %%right.sep.not = xor i1 %%right.sep, true\n"
        "  %%add.sep = and i1 %%add.sep.candidate, %%right.sep.not\n"
        "  %%skip.right.i64 = zext i1 %%skip.right to i64\n"
        "  %%add.sep.i64 = zext i1 %%add.sep to i64\n"
        "  %%right.copy.len = sub i64 %%right.len, %%skip.right.i64\n"
        "  %%size.base = add i64 %%left.len, %%right.copy.len\n"
        "  %%size.with.sep = add i64 %%size.base, %%add.sep.i64\n"
        "  %%size = add i64 %%size.with.sep, 1\n"
        "  %%dst = call ptr @malloc(i64 %%size)\n"
        "  %%has.dst = icmp ne ptr %%dst, null\n"
        "  br i1 %%has.dst, label %%copy.left, label %%fallback\n"
        "copy.left:\n"
        "  %%left.copy = call ptr @memcpy(ptr %%dst, ptr %%left, i64 %%left.len)\n"
        "  br i1 %%add.sep, label %%store.sep, label %%copy.right\n"
        "store.sep:\n"
        "  %%sep.ptr = getelementptr inbounds i8, ptr %%dst, i64 %%left.len\n"
        "  store i8 %d, ptr %%sep.ptr\n"
        "  br label %%copy.right\n"
        "copy.right:\n"
        "  %%right.src = getelementptr inbounds i8, ptr %%right, i64 %%skip.right.i64\n"
        "  %%right.dst.offset = add i64 %%left.len, %%add.sep.i64\n"
        "  %%right.dst = getelementptr inbounds i8, ptr %%dst, i64 %%right.dst.offset\n"
        "  %%right.copy = call ptr @memcpy(ptr %%right.dst, ptr %%right.src, i64 %%right.copy.len)\n"
        "  %%null.offset = add i64 %%right.dst.offset, %%right.copy.len\n"
        "  %%null.ptr = getelementptr inbounds i8, ptr %%dst, i64 %%null.offset\n"
        "  store i8 0, ptr %%null.ptr\n"
        "  call void @cn_track_str(ptr %%dst)\n"
        "  ret ptr %%dst\n"
        "fallback:\n"
        "  %%empty.ptr = getelementptr inbounds [1 x i8], ptr @.cn.empty, i64 0, i64 0\n"
        "  ret ptr %%empty.ptr\n"
        "}\n\n",
        CN_LLVM_PATH_SEPARATOR
    );
    fputs(
        "define private ptr @cn_path_file_name(ptr %path) {\n"
        "entry:\n"
        "  %cursor.slot = alloca ptr\n"
        "  %start.slot = alloca ptr\n"
        "  store ptr %path, ptr %cursor.slot\n"
        "  store ptr %path, ptr %start.slot\n"
        "  br label %loop\n"
        "loop:\n"
        "  %cursor = load ptr, ptr %cursor.slot\n"
        "  %ch = load i8, ptr %cursor\n"
        "  %is.end = icmp eq i8 %ch, 0\n"
        "  br i1 %is.end, label %finish, label %check.sep\n"
        "check.sep:\n"
        "  %is.slash = icmp eq i8 %ch, 47\n"
        "  %is.backslash = icmp eq i8 %ch, 92\n"
        "  %is.sep = or i1 %is.slash, %is.backslash\n"
        "  br i1 %is.sep, label %mark.start, label %advance\n"
        "mark.start:\n"
        "  %next.start = getelementptr inbounds i8, ptr %cursor, i64 1\n"
        "  store ptr %next.start, ptr %start.slot\n"
        "  br label %advance\n"
        "advance:\n"
        "  %next.cursor = getelementptr inbounds i8, ptr %cursor, i64 1\n"
        "  store ptr %next.cursor, ptr %cursor.slot\n"
        "  br label %loop\n"
        "finish:\n"
        "  %start = load ptr, ptr %start.slot\n"
        "  %copy = call ptr @cn_dup_cstr(ptr %start)\n"
        "  ret ptr %copy\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private ptr @cn_path_stem(ptr %path) {\n"
        "entry:\n"
        "  %cursor.slot = alloca ptr\n"
        "  %start.slot = alloca ptr\n"
        "  %last.dot.slot = alloca ptr\n"
        "  store ptr %path, ptr %cursor.slot\n"
        "  store ptr %path, ptr %start.slot\n"
        "  store ptr null, ptr %last.dot.slot\n"
        "  br label %loop\n"
        "loop:\n"
        "  %cursor = load ptr, ptr %cursor.slot\n"
        "  %ch = load i8, ptr %cursor\n"
        "  %is.end = icmp eq i8 %ch, 0\n"
        "  br i1 %is.end, label %finish, label %check.sep\n"
        "check.sep:\n"
        "  %is.slash = icmp eq i8 %ch, 47\n"
        "  %is.backslash = icmp eq i8 %ch, 92\n"
        "  %is.sep = or i1 %is.slash, %is.backslash\n"
        "  br i1 %is.sep, label %mark.sep, label %check.dot\n"
        "mark.sep:\n"
        "  %next.start = getelementptr inbounds i8, ptr %cursor, i64 1\n"
        "  store ptr %next.start, ptr %start.slot\n"
        "  store ptr null, ptr %last.dot.slot\n"
        "  br label %advance\n"
        "check.dot:\n"
        "  %is.dot = icmp eq i8 %ch, 46\n"
        "  br i1 %is.dot, label %mark.dot, label %advance\n"
        "mark.dot:\n"
        "  store ptr %cursor, ptr %last.dot.slot\n"
        "  br label %advance\n"
        "advance:\n"
        "  %next.cursor = getelementptr inbounds i8, ptr %cursor, i64 1\n"
        "  store ptr %next.cursor, ptr %cursor.slot\n"
        "  br label %loop\n"
        "finish:\n"
        "  %start = load ptr, ptr %start.slot\n"
        "  %last.dot = load ptr, ptr %last.dot.slot\n"
        "  %has.dot = icmp ne ptr %last.dot, null\n"
        "  br i1 %has.dot, label %check.valid.dot, label %copy.full\n"
        "check.valid.dot:\n"
        "  %start.int = ptrtoint ptr %start to i64\n"
        "  %dot.int = ptrtoint ptr %last.dot to i64\n"
        "  %dot.at.start = icmp eq i64 %start.int, %dot.int\n"
        "  %after.dot = getelementptr inbounds i8, ptr %last.dot, i64 1\n"
        "  %after.dot.ch = load i8, ptr %after.dot\n"
        "  %dot.at.end = icmp eq i8 %after.dot.ch, 0\n"
        "  %dot.valid.start = xor i1 %dot.at.start, true\n"
        "  %dot.valid.end = xor i1 %dot.at.end, true\n"
        "  %dot.valid = and i1 %dot.valid.start, %dot.valid.end\n"
        "  br i1 %dot.valid, label %copy.range, label %copy.full\n"
        "copy.range:\n"
        "  %length = sub i64 %dot.int, %start.int\n"
        "  %stem = call ptr @cn_dup_range(ptr %start, i64 %length)\n"
        "  ret ptr %stem\n"
        "copy.full:\n"
        "  %full = call ptr @cn_dup_cstr(ptr %start)\n"
        "  ret ptr %full\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private ptr @cn_path_extension(ptr %path) {\n"
        "entry:\n"
        "  %cursor.slot = alloca ptr\n"
        "  %start.slot = alloca ptr\n"
        "  %last.dot.slot = alloca ptr\n"
        "  store ptr %path, ptr %cursor.slot\n"
        "  store ptr %path, ptr %start.slot\n"
        "  store ptr null, ptr %last.dot.slot\n"
        "  br label %loop\n"
        "loop:\n"
        "  %cursor = load ptr, ptr %cursor.slot\n"
        "  %ch = load i8, ptr %cursor\n"
        "  %is.end = icmp eq i8 %ch, 0\n"
        "  br i1 %is.end, label %finish, label %check.sep\n"
        "check.sep:\n"
        "  %is.slash = icmp eq i8 %ch, 47\n"
        "  %is.backslash = icmp eq i8 %ch, 92\n"
        "  %is.sep = or i1 %is.slash, %is.backslash\n"
        "  br i1 %is.sep, label %mark.sep, label %check.dot\n"
        "mark.sep:\n"
        "  %next.start = getelementptr inbounds i8, ptr %cursor, i64 1\n"
        "  store ptr %next.start, ptr %start.slot\n"
        "  store ptr null, ptr %last.dot.slot\n"
        "  br label %advance\n"
        "check.dot:\n"
        "  %is.dot = icmp eq i8 %ch, 46\n"
        "  br i1 %is.dot, label %mark.dot, label %advance\n"
        "mark.dot:\n"
        "  store ptr %cursor, ptr %last.dot.slot\n"
        "  br label %advance\n"
        "advance:\n"
        "  %next.cursor = getelementptr inbounds i8, ptr %cursor, i64 1\n"
        "  store ptr %next.cursor, ptr %cursor.slot\n"
        "  br label %loop\n"
        "finish:\n"
        "  %start = load ptr, ptr %start.slot\n"
        "  %last.dot = load ptr, ptr %last.dot.slot\n"
        "  %has.dot = icmp ne ptr %last.dot, null\n"
        "  br i1 %has.dot, label %check.valid.dot, label %return.empty\n"
        "check.valid.dot:\n"
        "  %start.int = ptrtoint ptr %start to i64\n"
        "  %dot.int = ptrtoint ptr %last.dot to i64\n"
        "  %dot.at.start = icmp eq i64 %start.int, %dot.int\n"
        "  %after.dot = getelementptr inbounds i8, ptr %last.dot, i64 1\n"
        "  %after.dot.ch = load i8, ptr %after.dot\n"
        "  %dot.at.end = icmp eq i8 %after.dot.ch, 0\n"
        "  %dot.valid.start = xor i1 %dot.at.start, true\n"
        "  %dot.valid.end = xor i1 %dot.at.end, true\n"
        "  %dot.valid = and i1 %dot.valid.start, %dot.valid.end\n"
        "  br i1 %dot.valid, label %return.ext, label %return.empty\n"
        "return.ext:\n"
        "  %ext = call ptr @cn_dup_cstr(ptr %after.dot)\n"
        "  ret ptr %ext\n"
        "return.empty:\n"
        "  %empty = call ptr @cn_dup_range(ptr %path, i64 0)\n"
        "  ret ptr %empty\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private i1 @cn_path_is_absolute(ptr %path) {\n"
        "entry:\n"
        "  %first = load i8, ptr %path\n"
        "  %is.slash = icmp eq i8 %first, 47\n"
        "  %is.backslash = icmp eq i8 %first, 92\n"
        "  %is.sep = or i1 %is.slash, %is.backslash\n"
        "  br i1 %is.sep, label %return.true, label %check.drive\n"
        "check.drive:\n"
        "  %upper.low = icmp uge i8 %first, 65\n"
        "  %upper.high = icmp ule i8 %first, 90\n"
        "  %upper = and i1 %upper.low, %upper.high\n"
        "  %lower.low = icmp uge i8 %first, 97\n"
        "  %lower.high = icmp ule i8 %first, 122\n"
        "  %lower = and i1 %lower.low, %lower.high\n"
        "  %is.alpha = or i1 %upper, %lower\n"
        "  br i1 %is.alpha, label %check.colon, label %return.false\n"
        "check.colon:\n"
        "  %second.ptr = getelementptr inbounds i8, ptr %path, i64 1\n"
        "  %second = load i8, ptr %second.ptr\n"
        "  %is.colon = icmp eq i8 %second, 58\n"
        "  br i1 %is.colon, label %check.third, label %return.false\n"
        "check.third:\n"
        "  %third.ptr = getelementptr inbounds i8, ptr %path, i64 2\n"
        "  %third = load i8, ptr %third.ptr\n"
        "  %third.slash = icmp eq i8 %third, 47\n"
        "  %third.backslash = icmp eq i8 %third, 92\n"
        "  %third.sep = or i1 %third.slash, %third.backslash\n"
        "  ret i1 %third.sep\n"
        "return.true:\n"
        "  ret i1 true\n"
        "return.false:\n"
        "  ret i1 false\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, ptr } @cn_path_parent(ptr %path) {\n"
        "entry:\n"
        "  %cursor.slot = alloca ptr\n"
        "  %last.sep.slot = alloca ptr\n"
        "  store ptr %path, ptr %cursor.slot\n"
        "  store ptr null, ptr %last.sep.slot\n"
        "  br label %loop\n"
        "loop:\n"
        "  %cursor = load ptr, ptr %cursor.slot\n"
        "  %ch = load i8, ptr %cursor\n"
        "  %is.end = icmp eq i8 %ch, 0\n"
        "  br i1 %is.end, label %finish, label %check.sep\n"
        "check.sep:\n"
        "  %is.slash = icmp eq i8 %ch, 47\n"
        "  %is.backslash = icmp eq i8 %ch, 92\n"
        "  %is.sep = or i1 %is.slash, %is.backslash\n"
        "  br i1 %is.sep, label %mark.sep, label %advance\n"
        "mark.sep:\n"
        "  store ptr %cursor, ptr %last.sep.slot\n"
        "  br label %advance\n"
        "advance:\n"
        "  %next.cursor = getelementptr inbounds i8, ptr %cursor, i64 1\n"
        "  store ptr %next.cursor, ptr %cursor.slot\n"
        "  br label %loop\n"
        "finish:\n"
        "  %last.sep = load ptr, ptr %last.sep.slot\n"
        "  %has.sep = icmp ne ptr %last.sep, null\n"
        "  br i1 %has.sep, label %copy.parent, label %return.err\n"
        "copy.parent:\n"
        "  %path.int = ptrtoint ptr %path to i64\n"
        "  %sep.int = ptrtoint ptr %last.sep to i64\n"
        "  %length = sub i64 %sep.int, %path.int\n"
        "  %is.root = icmp eq i64 %length, 0\n"
        "  br i1 %is.root, label %copy.root, label %copy.range\n"
        "copy.root:\n"
        "  %root.copy = call ptr @cn_dup_range(ptr %path, i64 1)\n"
        "  br label %return.ok.root\n"
        "return.ok.root:\n"
        "  %ok.root = insertvalue { i1, ptr } zeroinitializer, i1 true, 0\n"
        "  %value.root = insertvalue { i1, ptr } %ok.root, ptr %root.copy, 1\n"
        "  ret { i1, ptr } %value.root\n"
        "copy.range:\n"
        "  %parent.copy = call ptr @cn_dup_range(ptr %path, i64 %length)\n"
        "  %ok = insertvalue { i1, ptr } zeroinitializer, i1 true, 0\n"
        "  %value.ok = insertvalue { i1, ptr } %ok, ptr %parent.copy, 1\n"
        "  ret { i1, ptr } %value.ok\n"
        "return.err:\n"
        "  ret { i1, ptr } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private i1 @cn_fs_exists(ptr %path) {\n"
        "entry:\n"
        "  %mode = getelementptr inbounds [3 x i8], ptr @.cn.mode.read, i64 0, i64 0\n"
        "  %file = call ptr @fopen(ptr %path, ptr %mode)\n"
        "  %exists = icmp ne ptr %file, null\n"
        "  br i1 %exists, label %close, label %missing\n"
        "close:\n"
        "  %closed = call i32 @fclose(ptr %file)\n"
        "  ret i1 true\n"
        "missing:\n"
        "  ret i1 false\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, ptr } @cn_fs_cwd() {\n"
        "entry:\n"
        "  %buffer = call ptr @malloc(i64 4096)\n"
        "  %has.buffer = icmp ne ptr %buffer, null\n"
        "  br i1 %has.buffer, label %read.cwd, label %return.err\n"
        "read.cwd:\n",
        stream
    );
#ifdef _WIN32
    fputs("  %cwd = call ptr @_getcwd(ptr %buffer, i32 4096)\n", stream);
#else
    fputs("  %cwd = call ptr @getcwd(ptr %buffer, i64 4096)\n", stream);
#endif
    fputs(
        "  %has.cwd = icmp ne ptr %cwd, null\n"
        "  br i1 %has.cwd, label %return.ok, label %free.err\n"
        "return.ok:\n"
        "  call void @cn_track_str(ptr %buffer)\n"
        "  %ok = insertvalue { i1, ptr } zeroinitializer, i1 true, 0\n"
        "  %value.ok = insertvalue { i1, ptr } %ok, ptr %buffer, 1\n"
        "  ret { i1, ptr } %value.ok\n"
        "free.err:\n"
        "  call void @free(ptr %buffer)\n"
        "  ret { i1, ptr } zeroinitializer\n"
        "return.err:\n"
        "  ret { i1, ptr } zeroinitializer\n"
        "}\n\n",
        stream
    );
#ifdef _WIN32
    fputs(
        "define private { i1, i1 } @cn_fs_create_dir(ptr %path) {\n"
        "entry:\n"
        "  %code = call i32 @_mkdir(ptr %path)\n"
        "  %success = icmp eq i32 %code, 0\n"
        "  br i1 %success, label %return.ok, label %return.err\n"
        "return.ok:\n"
        "  %ok = insertvalue { i1, i1 } zeroinitializer, i1 true, 0\n"
        "  %value.ok = insertvalue { i1, i1 } %ok, i1 true, 1\n"
        "  ret { i1, i1 } %value.ok\n"
        "return.err:\n"
        "  ret { i1, i1 } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, i1 } @cn_fs_remove_dir(ptr %path) {\n"
        "entry:\n"
        "  %code = call i32 @_rmdir(ptr %path)\n"
        "  %success = icmp eq i32 %code, 0\n"
        "  br i1 %success, label %return.ok, label %return.err\n"
        "return.ok:\n"
        "  %ok = insertvalue { i1, i1 } zeroinitializer, i1 true, 0\n"
        "  %value.ok = insertvalue { i1, i1 } %ok, i1 true, 1\n"
        "  ret { i1, i1 } %value.ok\n"
        "return.err:\n"
        "  ret { i1, i1 } zeroinitializer\n"
        "}\n\n",
        stream
    );
#else
    fputs(
        "define private { i1, i1 } @cn_fs_create_dir(ptr %path) {\n"
        "entry:\n"
        "  %code = call i32 @mkdir(ptr %path, i32 511)\n"
        "  %success = icmp eq i32 %code, 0\n"
        "  br i1 %success, label %return.ok, label %return.err\n"
        "return.ok:\n"
        "  %ok = insertvalue { i1, i1 } zeroinitializer, i1 true, 0\n"
        "  %value.ok = insertvalue { i1, i1 } %ok, i1 true, 1\n"
        "  ret { i1, i1 } %value.ok\n"
        "return.err:\n"
        "  ret { i1, i1 } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, i1 } @cn_fs_remove_dir(ptr %path) {\n"
        "entry:\n"
        "  %code = call i32 @rmdir(ptr %path)\n"
        "  %success = icmp eq i32 %code, 0\n"
        "  br i1 %success, label %return.ok, label %return.err\n"
        "return.ok:\n"
        "  %ok = insertvalue { i1, i1 } zeroinitializer, i1 true, 0\n"
        "  %value.ok = insertvalue { i1, i1 } %ok, i1 true, 1\n"
        "  ret { i1, i1 } %value.ok\n"
        "return.err:\n"
        "  ret { i1, i1 } zeroinitializer\n"
        "}\n\n",
        stream
    );
#endif
    fputs(
        "define private { i1, ptr } @cn_fs_read_text(ptr %path) {\n"
        "entry:\n"
        "  %mode = getelementptr inbounds [3 x i8], ptr @.cn.mode.read, i64 0, i64 0\n"
        "  %file = call ptr @fopen(ptr %path, ptr %mode)\n"
        "  %has.file = icmp ne ptr %file, null\n"
        "  br i1 %has.file, label %alloc, label %return.err\n"
        "alloc:\n"
        "  %buffer = call ptr @malloc(i64 64)\n"
        "  %has.buffer = icmp ne ptr %buffer, null\n"
        "  br i1 %has.buffer, label %init, label %close.err\n"
        "init:\n"
        "  %buffer.slot = alloca ptr\n"
        "  %length.slot = alloca i64\n"
        "  %capacity.slot = alloca i64\n"
        "  store ptr %buffer, ptr %buffer.slot\n"
        "  store i64 0, ptr %length.slot\n"
        "  store i64 64, ptr %capacity.slot\n"
        "  br label %loop\n"
        "loop:\n"
        "  %ch = call i32 @fgetc(ptr %file)\n"
        "  %is.eof = icmp eq i32 %ch, -1\n"
        "  br i1 %is.eof, label %finish, label %ensure.capacity\n"
        "ensure.capacity:\n"
        "  %length = load i64, ptr %length.slot\n"
        "  %capacity = load i64, ptr %capacity.slot\n"
        "  %needed.base = add i64 %length, 1\n"
        "  %needed = add i64 %needed.base, 1\n"
        "  %has.space = icmp ule i64 %needed, %capacity\n"
        "  br i1 %has.space, label %store.char, label %grow\n"
        "grow:\n"
        "  %new.capacity = mul i64 %capacity, 2\n"
        "  %current.buffer = load ptr, ptr %buffer.slot\n"
        "  %resized = call ptr @realloc(ptr %current.buffer, i64 %new.capacity)\n"
        "  %has.resized = icmp ne ptr %resized, null\n"
        "  br i1 %has.resized, label %grow.ok, label %free.err\n"
        "grow.ok:\n"
        "  store ptr %resized, ptr %buffer.slot\n"
        "  store i64 %new.capacity, ptr %capacity.slot\n"
        "  br label %store.char\n"
        "store.char:\n"
        "  %buffer.now = load ptr, ptr %buffer.slot\n"
        "  %length.now = load i64, ptr %length.slot\n"
        "  %char.ptr = getelementptr inbounds i8, ptr %buffer.now, i64 %length.now\n"
        "  %char.byte = trunc i32 %ch to i8\n"
        "  store i8 %char.byte, ptr %char.ptr\n"
        "  %next.length = add i64 %length.now, 1\n"
        "  store i64 %next.length, ptr %length.slot\n"
        "  br label %loop\n"
        "finish:\n"
        "  %buffer.final = load ptr, ptr %buffer.slot\n"
        "  %length.final = load i64, ptr %length.slot\n"
        "  %end.ptr = getelementptr inbounds i8, ptr %buffer.final, i64 %length.final\n"
        "  store i8 0, ptr %end.ptr\n"
        "  %close.ok = call i32 @fclose(ptr %file)\n"
        "  call void @cn_track_str(ptr %buffer.final)\n"
        "  %ok = insertvalue { i1, ptr } zeroinitializer, i1 true, 0\n"
        "  %value.ok = insertvalue { i1, ptr } %ok, ptr %buffer.final, 1\n"
        "  ret { i1, ptr } %value.ok\n"
        "free.err:\n"
        "  %buffer.err = load ptr, ptr %buffer.slot\n"
        "  call void @free(ptr %buffer.err)\n"
        "  br label %close.err\n"
        "close.err:\n"
        "  %close.err.code = call i32 @fclose(ptr %file)\n"
        "  ret { i1, ptr } zeroinitializer\n"
        "return.err:\n"
        "  ret { i1, ptr } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, i64 } @cn_fs_file_size(ptr %path) {\n"
        "entry:\n"
        "  %mode = getelementptr inbounds [3 x i8], ptr @.cn.mode.read, i64 0, i64 0\n"
        "  %file = call ptr @fopen(ptr %path, ptr %mode)\n"
        "  %has.file = icmp ne ptr %file, null\n"
        "  br i1 %has.file, label %count.setup, label %return.err\n"
        "count.setup:\n"
        "  %count.slot = alloca i64\n"
        "  store i64 0, ptr %count.slot\n"
        "  br label %count.loop\n"
        "count.loop:\n"
        "  %ch = call i32 @fgetc(ptr %file)\n"
        "  %is.eof = icmp eq i32 %ch, -1\n"
        "  br i1 %is.eof, label %count.finish, label %count.next\n"
        "count.next:\n"
        "  %count = load i64, ptr %count.slot\n"
        "  %next.count = add i64 %count, 1\n"
        "  store i64 %next.count, ptr %count.slot\n"
        "  br label %count.loop\n"
        "count.finish:\n"
        "  %close.code = call i32 @fclose(ptr %file)\n"
        "  %closed.ok = icmp eq i32 %close.code, 0\n"
        "  br i1 %closed.ok, label %return.ok, label %return.err\n"
        "return.ok:\n"
        "  %count.final = load i64, ptr %count.slot\n"
        "  %ok = insertvalue { i1, i64 } zeroinitializer, i1 true, 0\n"
        "  %value.ok = insertvalue { i1, i64 } %ok, i64 %count.final, 1\n"
        "  ret { i1, i64 } %value.ok\n"
        "return.err:\n"
        "  ret { i1, i64 } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, i1 } @cn_fs_copy(ptr %from_path, ptr %to_path) {\n"
        "entry:\n"
        "  %read.mode = getelementptr inbounds [3 x i8], ptr @.cn.mode.read, i64 0, i64 0\n"
        "  %src = call ptr @fopen(ptr %from_path, ptr %read.mode)\n"
        "  %has.src = icmp ne ptr %src, null\n"
        "  br i1 %has.src, label %open.dst, label %return.err\n"
        "open.dst:\n"
        "  %write.mode = getelementptr inbounds [3 x i8], ptr @.cn.mode.write, i64 0, i64 0\n"
        "  %dst = call ptr @fopen(ptr %to_path, ptr %write.mode)\n"
        "  %has.dst = icmp ne ptr %dst, null\n"
        "  br i1 %has.dst, label %copy.setup, label %close.src.err\n"
        "copy.setup:\n"
        "  %byte.slot = alloca i8\n"
        "  br label %copy.loop\n"
        "copy.loop:\n"
        "  %ch = call i32 @fgetc(ptr %src)\n"
        "  %is.eof = icmp eq i32 %ch, -1\n"
        "  br i1 %is.eof, label %copy.finish, label %copy.write\n"
        "copy.write:\n"
        "  %byte = trunc i32 %ch to i8\n"
        "  store i8 %byte, ptr %byte.slot\n"
        "  %written = call i64 @fwrite(ptr %byte.slot, i64 1, i64 1, ptr %dst)\n"
        "  %write.ok = icmp eq i64 %written, 1\n"
        "  br i1 %write.ok, label %copy.loop, label %close.both.err\n"
        "copy.finish:\n"
        "  %close.dst = call i32 @fclose(ptr %dst)\n"
        "  %close.src = call i32 @fclose(ptr %src)\n"
        "  %dst.ok = icmp eq i32 %close.dst, 0\n"
        "  %src.ok = icmp eq i32 %close.src, 0\n"
        "  %success = and i1 %dst.ok, %src.ok\n"
        "  br i1 %success, label %return.ok, label %return.err\n"
        "close.both.err:\n"
        "  %close.dst.err = call i32 @fclose(ptr %dst)\n"
        "  %close.src.after = call i32 @fclose(ptr %src)\n"
        "  br label %return.err\n"
        "close.src.err:\n"
        "  %close.src.err.code = call i32 @fclose(ptr %src)\n"
        "  br label %return.err\n"
        "return.ok:\n"
        "  %ok = insertvalue { i1, i1 } zeroinitializer, i1 true, 0\n"
        "  %value.ok = insertvalue { i1, i1 } %ok, i1 true, 1\n"
        "  ret { i1, i1 } %value.ok\n"
        "return.err:\n"
        "  ret { i1, i1 } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, i1 } @cn_fs_write_text(ptr %path, ptr %data) {\n"
        "entry:\n"
        "  %mode = getelementptr inbounds [3 x i8], ptr @.cn.mode.write, i64 0, i64 0\n"
        "  %file = call ptr @fopen(ptr %path, ptr %mode)\n"
        "  %has.file = icmp ne ptr %file, null\n"
        "  br i1 %has.file, label %write, label %return.err\n"
        "write:\n"
        "  %length = call i64 @strlen(ptr %data)\n"
        "  %written = call i64 @fwrite(ptr %data, i64 1, i64 %length, ptr %file)\n"
        "  %close.code = call i32 @fclose(ptr %file)\n"
        "  %all.written = icmp eq i64 %written, %length\n"
        "  %closed.ok = icmp eq i32 %close.code, 0\n"
        "  %success = and i1 %all.written, %closed.ok\n"
        "  br i1 %success, label %return.ok, label %return.err\n"
        "return.ok:\n"
        "  %ok = insertvalue { i1, i1 } zeroinitializer, i1 true, 0\n"
        "  %value.ok = insertvalue { i1, i1 } %ok, i1 true, 1\n"
        "  ret { i1, i1 } %value.ok\n"
        "return.err:\n"
        "  ret { i1, i1 } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, i1 } @cn_fs_append_text(ptr %path, ptr %data) {\n"
        "entry:\n"
        "  %mode = getelementptr inbounds [3 x i8], ptr @.cn.mode.append, i64 0, i64 0\n"
        "  %file = call ptr @fopen(ptr %path, ptr %mode)\n"
        "  %has.file = icmp ne ptr %file, null\n"
        "  br i1 %has.file, label %write, label %return.err\n"
        "write:\n"
        "  %length = call i64 @strlen(ptr %data)\n"
        "  %written = call i64 @fwrite(ptr %data, i64 1, i64 %length, ptr %file)\n"
        "  %close.code = call i32 @fclose(ptr %file)\n"
        "  %all.written = icmp eq i64 %written, %length\n"
        "  %closed.ok = icmp eq i32 %close.code, 0\n"
        "  %success = and i1 %all.written, %closed.ok\n"
        "  br i1 %success, label %return.ok, label %return.err\n"
        "return.ok:\n"
        "  %ok = insertvalue { i1, i1 } zeroinitializer, i1 true, 0\n"
        "  %value.ok = insertvalue { i1, i1 } %ok, i1 true, 1\n"
        "  ret { i1, i1 } %value.ok\n"
        "return.err:\n"
        "  ret { i1, i1 } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, i1 } @cn_fs_rename(ptr %from_path, ptr %to_path) {\n"
        "entry:\n"
        "  %code = call i32 @rename(ptr %from_path, ptr %to_path)\n"
        "  %success = icmp eq i32 %code, 0\n"
        "  br i1 %success, label %return.ok, label %return.err\n"
        "return.ok:\n"
        "  %ok = insertvalue { i1, i1 } zeroinitializer, i1 true, 0\n"
        "  %value.ok = insertvalue { i1, i1 } %ok, i1 true, 1\n"
        "  ret { i1, i1 } %value.ok\n"
        "return.err:\n"
        "  ret { i1, i1 } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, i1 } @cn_fs_remove(ptr %path) {\n"
        "entry:\n"
        "  %code = call i32 @remove(ptr %path)\n"
        "  %success = icmp eq i32 %code, 0\n"
        "  br i1 %success, label %return.ok, label %return.err\n"
        "return.ok:\n"
        "  %ok = insertvalue { i1, i1 } zeroinitializer, i1 true, 0\n"
        "  %value.ok = insertvalue { i1, i1 } %ok, i1 true, 1\n"
        "  ret { i1, i1 } %value.ok\n"
        "return.err:\n"
        "  ret { i1, i1 } zeroinitializer\n"
        "}\n\n",
        stream
    );
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
        "define private void @cn_write_str(ptr %value) {\n"
        "entry:\n"
        "  %fmt = getelementptr inbounds [3 x i8], ptr @.cn.str_fmt, i64 0, i64 0\n"
        "  call i32 (ptr, ...) @printf(ptr %fmt, ptr %value)\n"
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
