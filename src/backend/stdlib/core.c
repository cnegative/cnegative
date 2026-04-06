#include "cnegative/llvm_runtime.h"

void cn_llvm_emit_runtime_decls(FILE *stream) {
    fputs("declare i32 @printf(ptr, ...)\n", stream);
    fputs("declare i32 @puts(ptr)\n", stream);
    fputs("declare i32 @getchar()\n", stream);
    fputs("declare i32 @putchar(i32)\n", stream);
    fputs("declare i32 @fflush(ptr)\n", stream);
    fputs("declare i64 @strlen(ptr)\n", stream);
    fputs("declare i32 @strcmp(ptr, ptr)\n", stream);
    fputs("declare ptr @strstr(ptr, ptr)\n", stream);
    fputs("declare i32 @memcmp(ptr, ptr, i64)\n", stream);
    fputs("declare ptr @memcpy(ptr, ptr, i64)\n", stream);
    fputs("declare ptr @memmove(ptr, ptr, i64)\n", stream);
    fputs("declare ptr @malloc(i64)\n", stream);
    fputs("declare ptr @realloc(ptr, i64)\n", stream);
    fputs("declare i32 @snprintf(ptr, i64, ptr, ...)\n", stream);
    fputs("declare void @free(ptr)\n", stream);
    fputs("declare i32 @atexit(ptr)\n", stream);
    fputs("declare ptr @fopen(ptr, ptr)\n", stream);
    fputs("declare i32 @fclose(ptr)\n", stream);
    fputs("declare i32 @fgetc(ptr)\n", stream);
    fputs("declare i64 @fwrite(ptr, i64, i64, ptr)\n", stream);
    fputs("declare ptr @getenv(ptr)\n", stream);
    fputs("declare i32 @remove(ptr)\n", stream);
    fputs("declare i32 @rename(ptr, ptr)\n", stream);
    fputs("declare void @exit(i32)\n", stream);
#ifdef _WIN32
    fputs("declare i32 @_getch()\n", stream);
    fputs("declare i32 @_kbhit()\n", stream);
    fputs("declare ptr @_getcwd(ptr, i32)\n", stream);
    fputs("declare ptr @GetStdHandle(i32)\n", stream);
    fputs("declare i32 @GetConsoleMode(ptr, ptr)\n", stream);
    fputs("declare i32 @SetConsoleMode(ptr, i32)\n", stream);
    fputs("declare i32 @GetConsoleScreenBufferInfo(ptr, ptr)\n", stream);
    fputs("declare void @Sleep(i32)\n", stream);
    fputs("declare void @GetSystemTimeAsFileTime(ptr)\n", stream);
    fputs("declare i32 @WSAStartup(i16, ptr)\n", stream);
    fputs("declare i64 @socket(i32, i32, i32)\n", stream);
    fputs("declare i32 @setsockopt(i64, i32, i32, ptr, i32)\n", stream);
    fputs("declare i32 @connect(i64, ptr, i32)\n", stream);
    fputs("declare i32 @bind(i64, ptr, i32)\n", stream);
    fputs("declare i32 @listen(i64, i32)\n", stream);
    fputs("declare i64 @accept(i64, ptr, ptr)\n", stream);
    fputs("declare i32 @send(i64, ptr, i32, i32)\n", stream);
    fputs("declare i32 @sendto(i64, ptr, i32, i32, ptr, i32)\n", stream);
    fputs("declare i32 @recv(i64, ptr, i32, i32)\n", stream);
    fputs("declare i32 @recvfrom(i64, ptr, i32, i32, ptr, ptr)\n", stream);
    fputs("declare i32 @closesocket(i64)\n", stream);
    fputs("declare i16 @htons(i16)\n", stream);
    fputs("declare i16 @ntohs(i16)\n", stream);
    fputs("declare i32 @inet_addr(ptr)\n", stream);
    fputs("declare i32 @_mkdir(ptr)\n", stream);
    fputs("declare i32 @_rmdir(ptr)\n", stream);
    fputs("%cn_filetime = type { i32, i32 }\n", stream);
#else
    fputs("declare i32 @isatty(i32)\n", stream);
    fputs("declare ptr @signal(i32, ptr)\n", stream);
    fputs("declare i64 @read(i32, ptr, i64)\n", stream);
    fputs("declare i32 @poll(ptr, i64, i32)\n", stream);
    fputs("declare i32 @ioctl(i32, i64, ptr)\n", stream);
    fputs("declare i32 @tcgetattr(i32, ptr)\n", stream);
    fputs("declare i32 @tcsetattr(i32, i32, ptr)\n", stream);
    fputs("declare void @cfmakeraw(ptr)\n", stream);
    fputs("declare i32 @wcwidth(i32)\n", stream);
    fputs("declare i32 @timespec_get(ptr, i32)\n", stream);
    fputs("declare ptr @getcwd(ptr, i64)\n", stream);
    fputs("declare i32 @usleep(i32)\n", stream);
    fputs("declare i32 @socket(i32, i32, i32)\n", stream);
    fputs("declare i32 @setsockopt(i32, i32, i32, ptr, i32)\n", stream);
    fputs("declare i32 @connect(i32, ptr, i32)\n", stream);
    fputs("declare i32 @bind(i32, ptr, i32)\n", stream);
    fputs("declare i32 @listen(i32, i32)\n", stream);
    fputs("declare i32 @accept(i32, ptr, ptr)\n", stream);
    fputs("declare i64 @send(i32, ptr, i64, i32)\n", stream);
    fputs("declare i64 @sendto(i32, ptr, i64, i32, ptr, i32)\n", stream);
    fputs("declare i64 @recv(i32, ptr, i64, i32)\n", stream);
    fputs("declare i64 @recvfrom(i32, ptr, i64, i32, ptr, ptr)\n", stream);
    fputs("declare i32 @close(i32)\n", stream);
    fputs("declare i16 @htons(i16)\n", stream);
    fputs("declare i16 @ntohs(i16)\n", stream);
    fputs("declare i32 @inet_addr(ptr)\n", stream);
    fputs("declare i32 @mkdir(ptr, i32)\n", stream);
    fputs("declare i32 @rmdir(ptr)\n", stream);
    fputs("%cn_pollfd = type { i32, i16, i16 }\n", stream);
    fputs("%cn_timespec = type { i64, i64 }\n", stream);
#endif
}

void cn_llvm_emit_runtime_core(FILE *stream) {
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
}
