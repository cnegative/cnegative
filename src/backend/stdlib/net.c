#include "cnegative/llvm_runtime.h"

static void cn_llvm_emit_runtime_net_helpers(FILE *stream) {
    fputs("%cn_std_x2E_net__UdpPacket = type { ptr, i64, ptr }\n", stream);
    fputs("@.cn.net.localhost = private unnamed_addr constant [10 x i8] c\"localhost\\00\"\n", stream);
    fputs("@.cn.net.loopback = private unnamed_addr constant [10 x i8] c\"127.0.0.1\\00\"\n", stream);
    fputs("@.cn.net.ipv4_fmt = private unnamed_addr constant [12 x i8] c\"%u.%u.%u.%u\\00\"\n", stream);
#ifdef _WIN32
    fputs("@.cn.net.ready = internal global i1 false\n", stream);
    fputs("@.cn.net.ok = internal global i1 false\n", stream);
#endif
    fputs("%cn_sockaddr_in = type { i16, i16, i32, [8 x i8] }\n", stream);

#ifdef _WIN32
    fputs(
        "define private i1 @cn_net_ensure_init() {\n"
        "entry:\n"
        "  %ready = load i1, ptr @.cn.net.ready\n"
        "  br i1 %ready, label %return.cached, label %start\n"
        "start:\n"
        "  %wsa = alloca [512 x i8]\n"
        "  %wsa.ptr = getelementptr inbounds [512 x i8], ptr %wsa, i64 0, i64 0\n"
        "  %code = call i32 @WSAStartup(i16 514, ptr %wsa.ptr)\n"
        "  %ok = icmp eq i32 %code, 0\n"
        "  store i1 true, ptr @.cn.net.ready\n"
        "  store i1 %ok, ptr @.cn.net.ok\n"
        "  ret i1 %ok\n"
        "return.cached:\n"
        "  %cached = load i1, ptr @.cn.net.ok\n"
        "  ret i1 %cached\n"
        "}\n\n",
        stream
    );
#else
    fputs(
        "define private i1 @cn_net_ensure_init() {\n"
        "entry:\n"
        "  ret i1 true\n"
        "}\n\n",
        stream
    );
#endif

    fputs(
        "define private { i1, i16 } @cn_net_resolve_port(i64 %port) {\n"
        "entry:\n"
        "  %nonnegative = icmp sge i64 %port, 0\n"
        "  %within.range = icmp sle i64 %port, 65535\n"
        "  %valid = and i1 %nonnegative, %within.range\n"
        "  br i1 %valid, label %return.ok, label %return.err\n"
        "return.ok:\n"
        "  %port16 = trunc i64 %port to i16\n"
        "  %network = call i16 @htons(i16 %port16)\n"
        "  %ok = insertvalue { i1, i16 } zeroinitializer, i1 true, 0\n"
        "  %value.ok = insertvalue { i1, i16 } %ok, i16 %network, 1\n"
        "  ret { i1, i16 } %value.ok\n"
        "return.err:\n"
        "  ret { i1, i16 } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, i32 } @cn_net_resolve_host(ptr %host, i1 %allow_any) {\n"
        "entry:\n"
        "  %first = load i8, ptr %host\n"
        "  %is.empty = icmp eq i8 %first, 0\n"
        "  br i1 %is.empty, label %empty, label %check.localhost\n"
        "empty:\n"
        "  br i1 %allow_any, label %return.any, label %return.err\n"
        "return.any:\n"
        "  %any.ok = insertvalue { i1, i32 } zeroinitializer, i1 true, 0\n"
        "  %any.value = insertvalue { i1, i32 } %any.ok, i32 0, 1\n"
        "  ret { i1, i32 } %any.value\n"
        "check.localhost:\n"
        "  %localhost.ptr = getelementptr inbounds [10 x i8], ptr @.cn.net.localhost, i64 0, i64 0\n"
        "  %localhost.cmp = call i32 @strcmp(ptr %host, ptr %localhost.ptr)\n"
        "  %is.localhost = icmp eq i32 %localhost.cmp, 0\n"
        "  br i1 %is.localhost, label %return.localhost, label %check.ipv4\n"
        "return.localhost:\n"
        "  %loopback.ptr = getelementptr inbounds [10 x i8], ptr @.cn.net.loopback, i64 0, i64 0\n"
        "  %loopback.addr = call i32 @inet_addr(ptr %loopback.ptr)\n"
        "  %local.ok = insertvalue { i1, i32 } zeroinitializer, i1 true, 0\n"
        "  %local.value = insertvalue { i1, i32 } %local.ok, i32 %loopback.addr, 1\n"
        "  ret { i1, i32 } %local.value\n"
        "check.ipv4:\n"
        "  %ipv4 = call i1 @cn_net_is_ipv4(ptr %host)\n"
        "  br i1 %ipv4, label %return.ipv4, label %return.err\n"
        "return.ipv4:\n"
        "  %ipv4.addr = call i32 @inet_addr(ptr %host)\n"
        "  %ipv4.ok = insertvalue { i1, i32 } zeroinitializer, i1 true, 0\n"
        "  %ipv4.value = insertvalue { i1, i32 } %ipv4.ok, i32 %ipv4.addr, 1\n"
        "  ret { i1, i32 } %ipv4.value\n"
        "return.err:\n"
        "  ret { i1, i32 } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private void @cn_net_init_sockaddr(ptr %addr, i16 %port, i32 %ip) {\n"
        "entry:\n"
        "  %family.ptr = getelementptr inbounds %cn_sockaddr_in, ptr %addr, i32 0, i32 0\n"
        "  %port.ptr = getelementptr inbounds %cn_sockaddr_in, ptr %addr, i32 0, i32 1\n"
        "  %ip.ptr = getelementptr inbounds %cn_sockaddr_in, ptr %addr, i32 0, i32 2\n"
        "  %zero.ptr = getelementptr inbounds %cn_sockaddr_in, ptr %addr, i32 0, i32 3\n"
        "  store i16 2, ptr %family.ptr\n"
        "  store i16 %port, ptr %port.ptr\n"
        "  store i32 %ip, ptr %ip.ptr\n"
        "  store [8 x i8] zeroinitializer, ptr %zero.ptr\n"
        "  ret void\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private i64 @cn_net_sockaddr_port(ptr %addr) {\n"
        "entry:\n"
        "  %port.ptr = getelementptr inbounds %cn_sockaddr_in, ptr %addr, i32 0, i32 1\n"
        "  %network = load i16, ptr %port.ptr\n"
        "  %host = call i16 @ntohs(i16 %network)\n"
        "  %wide = zext i16 %host to i64\n"
        "  ret i64 %wide\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private ptr @cn_net_dup_sockaddr_host(ptr %addr) {\n"
        "entry:\n"
        "  %buffer = call ptr @malloc(i64 16)\n"
        "  %has.buffer = icmp ne ptr %buffer, null\n"
        "  br i1 %has.buffer, label %format, label %return.err\n"
        "format:\n"
        "  %fmt = getelementptr inbounds [12 x i8], ptr @.cn.net.ipv4_fmt, i64 0, i64 0\n"
        "  %ip.ptr = getelementptr inbounds %cn_sockaddr_in, ptr %addr, i32 0, i32 2\n"
        "  %octet0.ptr = getelementptr inbounds i8, ptr %ip.ptr, i64 0\n"
        "  %octet1.ptr = getelementptr inbounds i8, ptr %ip.ptr, i64 1\n"
        "  %octet2.ptr = getelementptr inbounds i8, ptr %ip.ptr, i64 2\n"
        "  %octet3.ptr = getelementptr inbounds i8, ptr %ip.ptr, i64 3\n"
        "  %octet0.raw = load i8, ptr %octet0.ptr\n"
        "  %octet1.raw = load i8, ptr %octet1.ptr\n"
        "  %octet2.raw = load i8, ptr %octet2.ptr\n"
        "  %octet3.raw = load i8, ptr %octet3.ptr\n"
        "  %octet0 = zext i8 %octet0.raw to i32\n"
        "  %octet1 = zext i8 %octet1.raw to i32\n"
        "  %octet2 = zext i8 %octet2.raw to i32\n"
        "  %octet3 = zext i8 %octet3.raw to i32\n"
        "  %written = call i32 (ptr, i64, ptr, ...) @snprintf(ptr %buffer, i64 16, ptr %fmt, i32 %octet0, i32 %octet1, i32 %octet2, i32 %octet3)\n"
        "  %written.ok = icmp sge i32 %written, 0\n"
        "  %fits = icmp slt i32 %written, 16\n"
        "  %success = and i1 %written.ok, %fits\n"
        "  br i1 %success, label %return.ok, label %free.err\n"
        "return.ok:\n"
        "  ret ptr %buffer\n"
        "free.err:\n"
        "  call void @free(ptr %buffer)\n"
        "  br label %return.err\n"
        "return.err:\n"
        "  ret ptr null\n"
        "}\n\n",
        stream
    );

#ifdef _WIN32
    fputs(
        "define private i1 @cn_net_enable_reuseaddr(i64 %socket_handle) {\n"
        "entry:\n"
        "  %value.slot = alloca i32\n"
        "  store i32 1, ptr %value.slot\n"
        "  %code = call i32 @setsockopt(i64 %socket_handle, i32 65535, i32 4, ptr %value.slot, i32 4)\n"
        "  %success = icmp eq i32 %code, 0\n"
        "  ret i1 %success\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, i64 } @cn_net_open_socket() {\n"
        "entry:\n"
        "  %handle = call i64 @socket(i32 2, i32 1, i32 0)\n"
        "  %valid = icmp ne i64 %handle, -1\n"
        "  br i1 %valid, label %return.ok, label %return.err\n"
        "return.ok:\n"
        "  %ok = insertvalue { i1, i64 } zeroinitializer, i1 true, 0\n"
        "  %value.ok = insertvalue { i1, i64 } %ok, i64 %handle, 1\n"
        "  ret { i1, i64 } %value.ok\n"
        "return.err:\n"
        "  ret { i1, i64 } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, i64 } @cn_net_open_datagram_socket() {\n"
        "entry:\n"
        "  %handle = call i64 @socket(i32 2, i32 2, i32 0)\n"
        "  %valid = icmp ne i64 %handle, -1\n"
        "  br i1 %valid, label %return.ok, label %return.err\n"
        "return.ok:\n"
        "  %ok = insertvalue { i1, i64 } zeroinitializer, i1 true, 0\n"
        "  %value.ok = insertvalue { i1, i64 } %ok, i64 %handle, 1\n"
        "  ret { i1, i64 } %value.ok\n"
        "return.err:\n"
        "  ret { i1, i64 } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private i1 @cn_net_connect_native(i64 %socket_handle, ptr %addr) {\n"
        "entry:\n"
        "  %code = call i32 @connect(i64 %socket_handle, ptr %addr, i32 16)\n"
        "  %success = icmp eq i32 %code, 0\n"
        "  ret i1 %success\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private i1 @cn_net_bind_native(i64 %socket_handle, ptr %addr) {\n"
        "entry:\n"
        "  %code = call i32 @bind(i64 %socket_handle, ptr %addr, i32 16)\n"
        "  %success = icmp eq i32 %code, 0\n"
        "  ret i1 %success\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private i1 @cn_net_listen_native(i64 %socket_handle) {\n"
        "entry:\n"
        "  %code = call i32 @listen(i64 %socket_handle, i32 16)\n"
        "  %success = icmp eq i32 %code, 0\n"
        "  ret i1 %success\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, i64 } @cn_net_accept_native(i64 %listener) {\n"
        "entry:\n"
        "  %handle = call i64 @accept(i64 %listener, ptr null, ptr null)\n"
        "  %valid = icmp ne i64 %handle, -1\n"
        "  br i1 %valid, label %return.ok, label %return.err\n"
        "return.ok:\n"
        "  %ok = insertvalue { i1, i64 } zeroinitializer, i1 true, 0\n"
        "  %value.ok = insertvalue { i1, i64 } %ok, i64 %handle, 1\n"
        "  ret { i1, i64 } %value.ok\n"
        "return.err:\n"
        "  ret { i1, i64 } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private i1 @cn_net_close_native(i64 %socket_handle) {\n"
        "entry:\n"
        "  %code = call i32 @closesocket(i64 %socket_handle)\n"
        "  %success = icmp eq i32 %code, 0\n"
        "  ret i1 %success\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, i64 } @cn_net_send_all(i64 %socket_handle, ptr %data) {\n"
        "entry:\n"
        "  %length = call i64 @strlen(ptr %data)\n"
        "  %total.slot = alloca i64\n"
        "  store i64 0, ptr %total.slot\n"
        "  br label %loop\n"
        "loop:\n"
        "  %total = load i64, ptr %total.slot\n"
        "  %remaining = sub i64 %length, %total\n"
        "  %done = icmp eq i64 %remaining, 0\n"
        "  br i1 %done, label %return.ok, label %send.next\n"
        "send.next:\n"
        "  %buffer = getelementptr inbounds i8, ptr %data, i64 %total\n"
        "  %remaining.i32 = trunc i64 %remaining to i32\n"
        "  %written.raw = call i32 @send(i64 %socket_handle, ptr %buffer, i32 %remaining.i32, i32 0)\n"
        "  %written.ok = icmp sgt i32 %written.raw, 0\n"
        "  br i1 %written.ok, label %advance, label %return.err\n"
        "advance:\n"
        "  %written = sext i32 %written.raw to i64\n"
        "  %next.total = add i64 %total, %written\n"
        "  store i64 %next.total, ptr %total.slot\n"
        "  br label %loop\n"
        "return.ok:\n"
        "  %final.total = load i64, ptr %total.slot\n"
        "  %ok = insertvalue { i1, i64 } zeroinitializer, i1 true, 0\n"
        "  %value.ok = insertvalue { i1, i64 } %ok, i64 %final.total, 1\n"
        "  ret { i1, i64 } %value.ok\n"
        "return.err:\n"
        "  ret { i1, i64 } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private i64 @cn_net_send_to_native(i64 %socket_handle, ptr %data, i64 %length, ptr %addr) {\n"
        "entry:\n"
        "  %length.i32 = trunc i64 %length to i32\n"
        "  %written.raw = call i32 @sendto(i64 %socket_handle, ptr %data, i32 %length.i32, i32 0, ptr %addr, i32 16)\n"
        "  %written = sext i32 %written.raw to i64\n"
        "  ret i64 %written\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, ptr } @cn_net_recv_chunk(i64 %socket_handle, i64 %max_bytes) {\n"
        "entry:\n"
        "  %positive = icmp sgt i64 %max_bytes, 0\n"
        "  br i1 %positive, label %alloc, label %return.err\n"
        "alloc:\n"
        "  %size = add i64 %max_bytes, 1\n"
        "  %buffer = call ptr @malloc(i64 %size)\n"
        "  %has.buffer = icmp ne ptr %buffer, null\n"
        "  br i1 %has.buffer, label %recv.next, label %return.err\n"
        "recv.next:\n"
        "  %max.i32 = trunc i64 %max_bytes to i32\n"
        "  %received.raw = call i32 @recv(i64 %socket_handle, ptr %buffer, i32 %max.i32, i32 0)\n"
        "  %received.ok = icmp sge i32 %received.raw, 0\n"
        "  br i1 %received.ok, label %finish, label %free.err\n"
        "finish:\n"
        "  %received = sext i32 %received.raw to i64\n"
        "  %end.ptr = getelementptr inbounds i8, ptr %buffer, i64 %received\n"
        "  store i8 0, ptr %end.ptr\n"
        "  call void @cn_track_str(ptr %buffer)\n"
        "  %ok = insertvalue { i1, ptr } zeroinitializer, i1 true, 0\n"
        "  %value.ok = insertvalue { i1, ptr } %ok, ptr %buffer, 1\n"
        "  ret { i1, ptr } %value.ok\n"
        "free.err:\n"
        "  call void @free(ptr %buffer)\n"
        "  br label %return.err\n"
        "return.err:\n"
        "  ret { i1, ptr } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private i64 @cn_net_recv_from_native(i64 %socket_handle, ptr %buffer, i64 %max_bytes, ptr %addr, ptr %addr_len) {\n"
        "entry:\n"
        "  %max.i32 = trunc i64 %max_bytes to i32\n"
        "  %received.raw = call i32 @recvfrom(i64 %socket_handle, ptr %buffer, i32 %max.i32, i32 0, ptr %addr, ptr %addr_len)\n"
        "  %received = sext i32 %received.raw to i64\n"
        "  ret i64 %received\n"
        "}\n\n",
        stream
    );
#else
#if defined(__APPLE__)
    fputs(
        "define private i1 @cn_net_enable_reuseaddr(i64 %socket_handle) {\n"
        "entry:\n"
        "  %value.slot = alloca i32\n"
        "  %socket.i32 = trunc i64 %socket_handle to i32\n"
        "  store i32 1, ptr %value.slot\n"
        "  %code = call i32 @setsockopt(i32 %socket.i32, i32 65535, i32 4, ptr %value.slot, i32 4)\n"
        "  %success = icmp eq i32 %code, 0\n"
        "  ret i1 %success\n"
        "}\n\n",
        stream
    );
#else
    fputs(
        "define private i1 @cn_net_enable_reuseaddr(i64 %socket_handle) {\n"
        "entry:\n"
        "  %value.slot = alloca i32\n"
        "  %socket.i32 = trunc i64 %socket_handle to i32\n"
        "  store i32 1, ptr %value.slot\n"
        "  %code = call i32 @setsockopt(i32 %socket.i32, i32 1, i32 2, ptr %value.slot, i32 4)\n"
        "  %success = icmp eq i32 %code, 0\n"
        "  ret i1 %success\n"
        "}\n\n",
        stream
    );
#endif
    fputs(
        "define private { i1, i64 } @cn_net_open_socket() {\n"
        "entry:\n"
        "  %handle.raw = call i32 @socket(i32 2, i32 1, i32 0)\n"
        "  %valid = icmp sge i32 %handle.raw, 0\n"
        "  br i1 %valid, label %return.ok, label %return.err\n"
        "return.ok:\n"
        "  %handle = sext i32 %handle.raw to i64\n"
        "  %ok = insertvalue { i1, i64 } zeroinitializer, i1 true, 0\n"
        "  %value.ok = insertvalue { i1, i64 } %ok, i64 %handle, 1\n"
        "  ret { i1, i64 } %value.ok\n"
        "return.err:\n"
        "  ret { i1, i64 } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, i64 } @cn_net_open_datagram_socket() {\n"
        "entry:\n"
        "  %handle.raw = call i32 @socket(i32 2, i32 2, i32 0)\n"
        "  %valid = icmp sge i32 %handle.raw, 0\n"
        "  br i1 %valid, label %return.ok, label %return.err\n"
        "return.ok:\n"
        "  %handle = sext i32 %handle.raw to i64\n"
        "  %ok = insertvalue { i1, i64 } zeroinitializer, i1 true, 0\n"
        "  %value.ok = insertvalue { i1, i64 } %ok, i64 %handle, 1\n"
        "  ret { i1, i64 } %value.ok\n"
        "return.err:\n"
        "  ret { i1, i64 } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private i1 @cn_net_connect_native(i64 %socket_handle, ptr %addr) {\n"
        "entry:\n"
        "  %socket.i32 = trunc i64 %socket_handle to i32\n"
        "  %code = call i32 @connect(i32 %socket.i32, ptr %addr, i32 16)\n"
        "  %success = icmp eq i32 %code, 0\n"
        "  ret i1 %success\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private i1 @cn_net_bind_native(i64 %socket_handle, ptr %addr) {\n"
        "entry:\n"
        "  %socket.i32 = trunc i64 %socket_handle to i32\n"
        "  %code = call i32 @bind(i32 %socket.i32, ptr %addr, i32 16)\n"
        "  %success = icmp eq i32 %code, 0\n"
        "  ret i1 %success\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private i1 @cn_net_listen_native(i64 %socket_handle) {\n"
        "entry:\n"
        "  %socket.i32 = trunc i64 %socket_handle to i32\n"
        "  %code = call i32 @listen(i32 %socket.i32, i32 16)\n"
        "  %success = icmp eq i32 %code, 0\n"
        "  ret i1 %success\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, i64 } @cn_net_accept_native(i64 %listener) {\n"
        "entry:\n"
        "  %listener.i32 = trunc i64 %listener to i32\n"
        "  %handle.raw = call i32 @accept(i32 %listener.i32, ptr null, ptr null)\n"
        "  %valid = icmp sge i32 %handle.raw, 0\n"
        "  br i1 %valid, label %return.ok, label %return.err\n"
        "return.ok:\n"
        "  %handle = sext i32 %handle.raw to i64\n"
        "  %ok = insertvalue { i1, i64 } zeroinitializer, i1 true, 0\n"
        "  %value.ok = insertvalue { i1, i64 } %ok, i64 %handle, 1\n"
        "  ret { i1, i64 } %value.ok\n"
        "return.err:\n"
        "  ret { i1, i64 } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private i1 @cn_net_close_native(i64 %socket_handle) {\n"
        "entry:\n"
        "  %socket.i32 = trunc i64 %socket_handle to i32\n"
        "  %code = call i32 @close(i32 %socket.i32)\n"
        "  %success = icmp eq i32 %code, 0\n"
        "  ret i1 %success\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, i64 } @cn_net_send_all(i64 %socket_handle, ptr %data) {\n"
        "entry:\n"
        "  %length = call i64 @strlen(ptr %data)\n"
        "  %total.slot = alloca i64\n"
        "  %socket.i32 = trunc i64 %socket_handle to i32\n"
        "  store i64 0, ptr %total.slot\n"
        "  br label %loop\n"
        "loop:\n"
        "  %total = load i64, ptr %total.slot\n"
        "  %remaining = sub i64 %length, %total\n"
        "  %done = icmp eq i64 %remaining, 0\n"
        "  br i1 %done, label %return.ok, label %send.next\n"
        "send.next:\n"
        "  %buffer = getelementptr inbounds i8, ptr %data, i64 %total\n"
        "  %written.raw = call i64 @send(i32 %socket.i32, ptr %buffer, i64 %remaining, i32 0)\n"
        "  %written.ok = icmp sgt i64 %written.raw, 0\n"
        "  br i1 %written.ok, label %advance, label %return.err\n"
        "advance:\n"
        "  %next.total = add i64 %total, %written.raw\n"
        "  store i64 %next.total, ptr %total.slot\n"
        "  br label %loop\n"
        "return.ok:\n"
        "  %final.total = load i64, ptr %total.slot\n"
        "  %ok = insertvalue { i1, i64 } zeroinitializer, i1 true, 0\n"
        "  %value.ok = insertvalue { i1, i64 } %ok, i64 %final.total, 1\n"
        "  ret { i1, i64 } %value.ok\n"
        "return.err:\n"
        "  ret { i1, i64 } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private i64 @cn_net_send_to_native(i64 %socket_handle, ptr %data, i64 %length, ptr %addr) {\n"
        "entry:\n"
        "  %socket.i32 = trunc i64 %socket_handle to i32\n"
        "  %written = call i64 @sendto(i32 %socket.i32, ptr %data, i64 %length, i32 0, ptr %addr, i32 16)\n"
        "  ret i64 %written\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, ptr } @cn_net_recv_chunk(i64 %socket_handle, i64 %max_bytes) {\n"
        "entry:\n"
        "  %positive = icmp sgt i64 %max_bytes, 0\n"
        "  br i1 %positive, label %alloc, label %return.err\n"
        "alloc:\n"
        "  %size = add i64 %max_bytes, 1\n"
        "  %buffer = call ptr @malloc(i64 %size)\n"
        "  %has.buffer = icmp ne ptr %buffer, null\n"
        "  br i1 %has.buffer, label %recv.next, label %return.err\n"
        "recv.next:\n"
        "  %socket.i32 = trunc i64 %socket_handle to i32\n"
        "  %received = call i64 @recv(i32 %socket.i32, ptr %buffer, i64 %max_bytes, i32 0)\n"
        "  %received.ok = icmp sge i64 %received, 0\n"
        "  br i1 %received.ok, label %finish, label %free.err\n"
        "finish:\n"
        "  %end.ptr = getelementptr inbounds i8, ptr %buffer, i64 %received\n"
        "  store i8 0, ptr %end.ptr\n"
        "  call void @cn_track_str(ptr %buffer)\n"
        "  %ok = insertvalue { i1, ptr } zeroinitializer, i1 true, 0\n"
        "  %value.ok = insertvalue { i1, ptr } %ok, ptr %buffer, 1\n"
        "  ret { i1, ptr } %value.ok\n"
        "free.err:\n"
        "  call void @free(ptr %buffer)\n"
        "  br label %return.err\n"
        "return.err:\n"
        "  ret { i1, ptr } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private i64 @cn_net_recv_from_native(i64 %socket_handle, ptr %buffer, i64 %max_bytes, ptr %addr, ptr %addr_len) {\n"
        "entry:\n"
        "  %socket.i32 = trunc i64 %socket_handle to i32\n"
        "  %received = call i64 @recvfrom(i32 %socket.i32, ptr %buffer, i64 %max_bytes, i32 0, ptr %addr, ptr %addr_len)\n"
        "  ret i64 %received\n"
        "}\n\n",
        stream
    );
#endif
}

void cn_llvm_emit_runtime_net(FILE *stream) {
    cn_llvm_emit_runtime_net_helpers(stream);
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
    fputs(
        "define private { i1, i64 } @cn_net_tcp_connect(ptr %host, i64 %port) {\n"
        "entry:\n"
        "  %ready = call i1 @cn_net_ensure_init()\n"
        "  br i1 %ready, label %resolve.port, label %return.err\n"
        "resolve.port:\n"
        "  %port.result = call { i1, i16 } @cn_net_resolve_port(i64 %port)\n"
        "  %port.ok = extractvalue { i1, i16 } %port.result, 0\n"
        "  br i1 %port.ok, label %resolve.host, label %return.err\n"
        "resolve.host:\n"
        "  %host.result = call { i1, i32 } @cn_net_resolve_host(ptr %host, i1 false)\n"
        "  %host.ok = extractvalue { i1, i32 } %host.result, 0\n"
        "  br i1 %host.ok, label %open.socket, label %return.err\n"
        "open.socket:\n"
        "  %socket.result = call { i1, i64 } @cn_net_open_socket()\n"
        "  %socket.ok = extractvalue { i1, i64 } %socket.result, 0\n"
        "  br i1 %socket.ok, label %connect.next, label %return.err\n"
        "connect.next:\n"
        "  %socket.handle = extractvalue { i1, i64 } %socket.result, 1\n"
        "  %port.value = extractvalue { i1, i16 } %port.result, 1\n"
        "  %host.value = extractvalue { i1, i32 } %host.result, 1\n"
        "  %addr = alloca %cn_sockaddr_in\n"
        "  call void @cn_net_init_sockaddr(ptr %addr, i16 %port.value, i32 %host.value)\n"
        "  %connected = call i1 @cn_net_connect_native(i64 %socket.handle, ptr %addr)\n"
        "  br i1 %connected, label %return.ok, label %close.err\n"
        "return.ok:\n"
        "  %ok = insertvalue { i1, i64 } zeroinitializer, i1 true, 0\n"
        "  %value.ok = insertvalue { i1, i64 } %ok, i64 %socket.handle, 1\n"
        "  ret { i1, i64 } %value.ok\n"
        "close.err:\n"
        "  %closed = call i1 @cn_net_close_native(i64 %socket.handle)\n"
        "  br label %return.err\n"
        "return.err:\n"
        "  ret { i1, i64 } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, i64 } @cn_net_tcp_listen(ptr %host, i64 %port) {\n"
        "entry:\n"
        "  %ready = call i1 @cn_net_ensure_init()\n"
        "  br i1 %ready, label %resolve.port, label %return.err\n"
        "resolve.port:\n"
        "  %port.result = call { i1, i16 } @cn_net_resolve_port(i64 %port)\n"
        "  %port.ok = extractvalue { i1, i16 } %port.result, 0\n"
        "  br i1 %port.ok, label %resolve.host, label %return.err\n"
        "resolve.host:\n"
        "  %host.result = call { i1, i32 } @cn_net_resolve_host(ptr %host, i1 true)\n"
        "  %host.ok = extractvalue { i1, i32 } %host.result, 0\n"
        "  br i1 %host.ok, label %open.socket, label %return.err\n"
        "open.socket:\n"
        "  %socket.result = call { i1, i64 } @cn_net_open_socket()\n"
        "  %socket.ok = extractvalue { i1, i64 } %socket.result, 0\n"
        "  br i1 %socket.ok, label %reuse.next, label %return.err\n"
        "reuse.next:\n"
        "  %socket.handle = extractvalue { i1, i64 } %socket.result, 1\n"
        "  %reuse.ok = call i1 @cn_net_enable_reuseaddr(i64 %socket.handle)\n"
        "  br i1 %reuse.ok, label %bind.next, label %close.err\n"
        "bind.next:\n"
        "  %port.value = extractvalue { i1, i16 } %port.result, 1\n"
        "  %host.value = extractvalue { i1, i32 } %host.result, 1\n"
        "  %addr = alloca %cn_sockaddr_in\n"
        "  call void @cn_net_init_sockaddr(ptr %addr, i16 %port.value, i32 %host.value)\n"
        "  %bound = call i1 @cn_net_bind_native(i64 %socket.handle, ptr %addr)\n"
        "  br i1 %bound, label %listen.next, label %close.err\n"
        "listen.next:\n"
        "  %listening = call i1 @cn_net_listen_native(i64 %socket.handle)\n"
        "  br i1 %listening, label %return.ok, label %close.err\n"
        "return.ok:\n"
        "  %ok = insertvalue { i1, i64 } zeroinitializer, i1 true, 0\n"
        "  %value.ok = insertvalue { i1, i64 } %ok, i64 %socket.handle, 1\n"
        "  ret { i1, i64 } %value.ok\n"
        "close.err:\n"
        "  %closed = call i1 @cn_net_close_native(i64 %socket.handle)\n"
        "  br label %return.err\n"
        "return.err:\n"
        "  ret { i1, i64 } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, i64 } @cn_net_udp_bind(ptr %host, i64 %port) {\n"
        "entry:\n"
        "  %ready = call i1 @cn_net_ensure_init()\n"
        "  br i1 %ready, label %resolve.port, label %return.err\n"
        "resolve.port:\n"
        "  %port.result = call { i1, i16 } @cn_net_resolve_port(i64 %port)\n"
        "  %port.ok = extractvalue { i1, i16 } %port.result, 0\n"
        "  br i1 %port.ok, label %resolve.host, label %return.err\n"
        "resolve.host:\n"
        "  %host.result = call { i1, i32 } @cn_net_resolve_host(ptr %host, i1 true)\n"
        "  %host.ok = extractvalue { i1, i32 } %host.result, 0\n"
        "  br i1 %host.ok, label %open.socket, label %return.err\n"
        "open.socket:\n"
        "  %socket.result = call { i1, i64 } @cn_net_open_datagram_socket()\n"
        "  %socket.ok = extractvalue { i1, i64 } %socket.result, 0\n"
        "  br i1 %socket.ok, label %bind.next, label %return.err\n"
        "bind.next:\n"
        "  %socket.handle = extractvalue { i1, i64 } %socket.result, 1\n"
        "  %port.value = extractvalue { i1, i16 } %port.result, 1\n"
        "  %host.value = extractvalue { i1, i32 } %host.result, 1\n"
        "  %addr = alloca %cn_sockaddr_in\n"
        "  call void @cn_net_init_sockaddr(ptr %addr, i16 %port.value, i32 %host.value)\n"
        "  %bound = call i1 @cn_net_bind_native(i64 %socket.handle, ptr %addr)\n"
        "  br i1 %bound, label %return.ok, label %close.err\n"
        "return.ok:\n"
        "  %ok = insertvalue { i1, i64 } zeroinitializer, i1 true, 0\n"
        "  %value.ok = insertvalue { i1, i64 } %ok, i64 %socket.handle, 1\n"
        "  ret { i1, i64 } %value.ok\n"
        "close.err:\n"
        "  %closed = call i1 @cn_net_close_native(i64 %socket.handle)\n"
        "  br label %return.err\n"
        "return.err:\n"
        "  ret { i1, i64 } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, i64 } @cn_net_accept(i64 %listener) {\n"
        "entry:\n"
        "  %ready = call i1 @cn_net_ensure_init()\n"
        "  br i1 %ready, label %accept.next, label %return.err\n"
        "accept.next:\n"
        "  %accepted = call { i1, i64 } @cn_net_accept_native(i64 %listener)\n"
        "  ret { i1, i64 } %accepted\n"
        "return.err:\n"
        "  ret { i1, i64 } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, i64 } @cn_net_send(i64 %socket_handle, ptr %data) {\n"
        "entry:\n"
        "  %ready = call i1 @cn_net_ensure_init()\n"
        "  br i1 %ready, label %send.next, label %return.err\n"
        "send.next:\n"
        "  %sent = call { i1, i64 } @cn_net_send_all(i64 %socket_handle, ptr %data)\n"
        "  ret { i1, i64 } %sent\n"
        "return.err:\n"
        "  ret { i1, i64 } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, i64 } @cn_net_udp_send_to(i64 %socket_handle, ptr %host, i64 %port, ptr %data) {\n"
        "entry:\n"
        "  %ready = call i1 @cn_net_ensure_init()\n"
        "  br i1 %ready, label %resolve.port, label %return.err\n"
        "resolve.port:\n"
        "  %port.result = call { i1, i16 } @cn_net_resolve_port(i64 %port)\n"
        "  %port.ok = extractvalue { i1, i16 } %port.result, 0\n"
        "  br i1 %port.ok, label %resolve.host, label %return.err\n"
        "resolve.host:\n"
        "  %host.result = call { i1, i32 } @cn_net_resolve_host(ptr %host, i1 false)\n"
        "  %host.ok = extractvalue { i1, i32 } %host.result, 0\n"
        "  br i1 %host.ok, label %send.next, label %return.err\n"
        "send.next:\n"
        "  %port.value = extractvalue { i1, i16 } %port.result, 1\n"
        "  %host.value = extractvalue { i1, i32 } %host.result, 1\n"
        "  %addr = alloca %cn_sockaddr_in\n"
        "  call void @cn_net_init_sockaddr(ptr %addr, i16 %port.value, i32 %host.value)\n"
        "  %length = call i64 @strlen(ptr %data)\n"
        "  %written = call i64 @cn_net_send_to_native(i64 %socket_handle, ptr %data, i64 %length, ptr %addr)\n"
        "  %written.ok = icmp sge i64 %written, 0\n"
        "  %complete = icmp eq i64 %written, %length\n"
        "  %sent.ok = and i1 %written.ok, %complete\n"
        "  br i1 %sent.ok, label %return.ok, label %return.err\n"
        "return.ok:\n"
        "  %ok = insertvalue { i1, i64 } zeroinitializer, i1 true, 0\n"
        "  %value.ok = insertvalue { i1, i64 } %ok, i64 %written, 1\n"
        "  ret { i1, i64 } %value.ok\n"
        "return.err:\n"
        "  ret { i1, i64 } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, ptr } @cn_net_recv(i64 %socket_handle, i64 %max_bytes) {\n"
        "entry:\n"
        "  %ready = call i1 @cn_net_ensure_init()\n"
        "  br i1 %ready, label %recv.next, label %return.err\n"
        "recv.next:\n"
        "  %received = call { i1, ptr } @cn_net_recv_chunk(i64 %socket_handle, i64 %max_bytes)\n"
        "  ret { i1, ptr } %received\n"
        "return.err:\n"
        "  ret { i1, ptr } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, %cn_std_x2E_net__UdpPacket } @cn_net_udp_recv_from(i64 %socket_handle, i64 %max_bytes) {\n"
        "entry:\n"
        "  %ready = call i1 @cn_net_ensure_init()\n"
        "  br i1 %ready, label %check.size, label %return.err\n"
        "check.size:\n"
        "  %positive = icmp sgt i64 %max_bytes, 0\n"
        "  br i1 %positive, label %alloc, label %return.err\n"
        "alloc:\n"
        "  %size = add i64 %max_bytes, 1\n"
        "  %buffer = call ptr @malloc(i64 %size)\n"
        "  %has.buffer = icmp ne ptr %buffer, null\n"
        "  br i1 %has.buffer, label %recv.next, label %return.err\n"
        "recv.next:\n"
        "  %addr = alloca %cn_sockaddr_in\n"
        "  %addr.len = alloca i32\n"
        "  store i32 16, ptr %addr.len\n"
        "  %received = call i64 @cn_net_recv_from_native(i64 %socket_handle, ptr %buffer, i64 %max_bytes, ptr %addr, ptr %addr.len)\n"
        "  %received.ok = icmp sge i64 %received, 0\n"
        "  br i1 %received.ok, label %finish, label %free.buffer\n"
        "finish:\n"
        "  %end.ptr = getelementptr inbounds i8, ptr %buffer, i64 %received\n"
        "  store i8 0, ptr %end.ptr\n"
        "  %sender.host = call ptr @cn_net_dup_sockaddr_host(ptr %addr)\n"
        "  %has.sender.host = icmp ne ptr %sender.host, null\n"
        "  br i1 %has.sender.host, label %return.ok, label %free.buffer\n"
        "return.ok:\n"
        "  %sender.port = call i64 @cn_net_sockaddr_port(ptr %addr)\n"
        "  call void @cn_track_str(ptr %buffer)\n"
        "  call void @cn_track_str(ptr %sender.host)\n"
        "  %packet.host = insertvalue %cn_std_x2E_net__UdpPacket zeroinitializer, ptr %sender.host, 0\n"
        "  %packet.port = insertvalue %cn_std_x2E_net__UdpPacket %packet.host, i64 %sender.port, 1\n"
        "  %packet.data = insertvalue %cn_std_x2E_net__UdpPacket %packet.port, ptr %buffer, 2\n"
        "  %ok = insertvalue { i1, %cn_std_x2E_net__UdpPacket } zeroinitializer, i1 true, 0\n"
        "  %value.ok = insertvalue { i1, %cn_std_x2E_net__UdpPacket } %ok, %cn_std_x2E_net__UdpPacket %packet.data, 1\n"
        "  ret { i1, %cn_std_x2E_net__UdpPacket } %value.ok\n"
        "free.buffer:\n"
        "  call void @free(ptr %buffer)\n"
        "  br label %return.err\n"
        "return.err:\n"
        "  ret { i1, %cn_std_x2E_net__UdpPacket } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, i1 } @cn_net_close(i64 %socket_handle) {\n"
        "entry:\n"
        "  %ready = call i1 @cn_net_ensure_init()\n"
        "  br i1 %ready, label %close.next, label %return.err\n"
        "close.next:\n"
        "  %closed = call i1 @cn_net_close_native(i64 %socket_handle)\n"
        "  br i1 %closed, label %return.ok, label %return.err\n"
        "return.ok:\n"
        "  %ok = insertvalue { i1, i1 } zeroinitializer, i1 true, 0\n"
        "  %value.ok = insertvalue { i1, i1 } %ok, i1 true, 1\n"
        "  ret { i1, i1 } %value.ok\n"
        "return.err:\n"
        "  ret { i1, i1 } zeroinitializer\n"
        "}\n\n",
        stream
    );
}
