#include "cnegative/llvm_runtime.h"

void cn_llvm_emit_runtime_x11(FILE *stream) {
#if defined(__linux__)
    fputs("%cn_x11_window = type { ptr, i64, i1 }\n", stream);
    fputs("@.cn.x11.wm_delete = private unnamed_addr constant [17 x i8] c\"WM_DELETE_WINDOW\\00\"\n", stream);
    fputs("declare ptr @XOpenDisplay(ptr)\n", stream);
    fputs("declare i32 @XDefaultScreen(ptr)\n", stream);
    fputs("declare i64 @XRootWindow(ptr, i32)\n", stream);
    fputs("declare i64 @XBlackPixel(ptr, i32)\n", stream);
    fputs("declare i64 @XWhitePixel(ptr, i32)\n", stream);
    fputs("declare i64 @XCreateSimpleWindow(ptr, i64, i32, i32, i32, i32, i32, i64, i64)\n", stream);
    fputs("declare i32 @XStoreName(ptr, i64, ptr)\n", stream);
    fputs("declare i64 @XInternAtom(ptr, ptr, i32)\n", stream);
    fputs("declare i32 @XSetWMProtocols(ptr, i64, ptr, i32)\n", stream);
    fputs("declare i32 @XSelectInput(ptr, i64, i64)\n", stream);
    fputs("declare i32 @XMapWindow(ptr, i64)\n", stream);
    fputs("declare i32 @XPending(ptr)\n", stream);
    fputs("declare i32 @XNextEvent(ptr, ptr)\n", stream);
    fputs("declare i32 @XFlush(ptr)\n", stream);
    fputs("declare i32 @XDestroyWindow(ptr, i64)\n", stream);
    fputs("declare i32 @XCloseDisplay(ptr)\n", stream);
    fputs(
        "define private { i1, i64 } @cn_x11_open_window(ptr %title, i64 %width, i64 %height) {\n"
        "entry:\n"
        "  %width.ok = icmp sgt i64 %width, 0\n"
        "  %height.ok = icmp sgt i64 %height, 0\n"
        "  %size.ok = and i1 %width.ok, %height.ok\n"
        "  br i1 %size.ok, label %open, label %return.err\n"
        "open:\n"
        "  %display = call ptr @XOpenDisplay(ptr null)\n"
        "  %has.display = icmp ne ptr %display, null\n"
        "  br i1 %has.display, label %create, label %return.err\n"
        "create:\n"
        "  %screen = call i32 @XDefaultScreen(ptr %display)\n"
        "  %root = call i64 @XRootWindow(ptr %display, i32 %screen)\n"
        "  %black = call i64 @XBlackPixel(ptr %display, i32 %screen)\n"
        "  %white = call i64 @XWhitePixel(ptr %display, i32 %screen)\n"
        "  %width32 = trunc i64 %width to i32\n"
        "  %height32 = trunc i64 %height to i32\n"
        "  %window = call i64 @XCreateSimpleWindow(ptr %display, i64 %root, i32 0, i32 0, i32 %width32, i32 %height32, i32 1, i64 %black, i64 %white)\n"
        "  %has.window = icmp ne i64 %window, 0\n"
        "  br i1 %has.window, label %setup, label %close.display\n"
        "setup:\n"
        "  %named = call i32 @XStoreName(ptr %display, i64 %window, ptr %title)\n"
        "  %wm.name = getelementptr inbounds [17 x i8], ptr @.cn.x11.wm_delete, i64 0, i64 0\n"
        "  %atom = call i64 @XInternAtom(ptr %display, ptr %wm.name, i32 0)\n"
        "  %atom.slot = alloca i64\n"
        "  store i64 %atom, ptr %atom.slot\n"
        "  %protocols = call i32 @XSetWMProtocols(ptr %display, i64 %window, ptr %atom.slot, i32 1)\n"
        "  %selected = call i32 @XSelectInput(ptr %display, i64 %window, i64 163841)\n"
        "  %mapped = call i32 @XMapWindow(ptr %display, i64 %window)\n"
        "  %flushed = call i32 @XFlush(ptr %display)\n"
        "  %handle = call ptr @malloc(i64 24)\n"
        "  %has.handle = icmp ne ptr %handle, null\n"
        "  br i1 %has.handle, label %fill, label %destroy.window\n"
        "fill:\n"
        "  %display.ptr = getelementptr inbounds %cn_x11_window, ptr %handle, i32 0, i32 0\n"
        "  %window.ptr = getelementptr inbounds %cn_x11_window, ptr %handle, i32 0, i32 1\n"
        "  %open.ptr = getelementptr inbounds %cn_x11_window, ptr %handle, i32 0, i32 2\n"
        "  store ptr %display, ptr %display.ptr\n"
        "  store i64 %window, ptr %window.ptr\n"
        "  store i1 true, ptr %open.ptr\n"
        "  %handle.int = ptrtoint ptr %handle to i64\n"
        "  %ok = insertvalue { i1, i64 } zeroinitializer, i1 true, 0\n"
        "  %value.ok = insertvalue { i1, i64 } %ok, i64 %handle.int, 1\n"
        "  ret { i1, i64 } %value.ok\n"
        "destroy.window:\n"
        "  %destroy = call i32 @XDestroyWindow(ptr %display, i64 %window)\n"
        "  br label %close.display\n"
        "close.display:\n"
        "  %closed = call i32 @XCloseDisplay(ptr %display)\n"
        "  br label %return.err\n"
        "return.err:\n"
        "  ret { i1, i64 } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, i1 } @cn_x11_pump(i64 %handle.value) {\n"
        "entry:\n"
        "  %event = alloca [24 x i64]\n"
        "  %valid = icmp ne i64 %handle.value, 0\n"
        "  br i1 %valid, label %load, label %return.err\n"
        "load:\n"
        "  %handle = inttoptr i64 %handle.value to ptr\n"
        "  %display.ptr = getelementptr inbounds %cn_x11_window, ptr %handle, i32 0, i32 0\n"
        "  %window.ptr = getelementptr inbounds %cn_x11_window, ptr %handle, i32 0, i32 1\n"
        "  %open.ptr = getelementptr inbounds %cn_x11_window, ptr %handle, i32 0, i32 2\n"
        "  %display = load ptr, ptr %display.ptr\n"
        "  %window = load i64, ptr %window.ptr\n"
        "  %open = load i1, ptr %open.ptr\n"
        "  br i1 %open, label %poll, label %return.closed\n"
        "poll:\n"
        "  %pending = call i32 @XPending(ptr %display)\n"
        "  %has.pending = icmp sgt i32 %pending, 0\n"
        "  br i1 %has.pending, label %read, label %flush\n"
        "read:\n"
        "  %event.ptr = getelementptr inbounds [24 x i64], ptr %event, i64 0, i64 0\n"
        "  %next = call i32 @XNextEvent(ptr %display, ptr %event.ptr)\n"
        "  %event.type = load i32, ptr %event.ptr\n"
        "  %is.destroy = icmp eq i32 %event.type, 17\n"
        "  %is.client = icmp eq i32 %event.type, 33\n"
        "  %should.close = or i1 %is.destroy, %is.client\n"
        "  br i1 %should.close, label %mark.closed, label %poll\n"
        "mark.closed:\n"
        "  store i1 false, ptr %open.ptr\n"
        "  br label %flush\n"
        "flush:\n"
        "  %flushed = call i32 @XFlush(ptr %display)\n"
        "  %open.after = load i1, ptr %open.ptr\n"
        "  %ok = insertvalue { i1, i1 } zeroinitializer, i1 true, 0\n"
        "  %value.ok = insertvalue { i1, i1 } %ok, i1 %open.after, 1\n"
        "  ret { i1, i1 } %value.ok\n"
        "return.closed:\n"
        "  %closed.ok = insertvalue { i1, i1 } zeroinitializer, i1 true, 0\n"
        "  ret { i1, i1 } %closed.ok\n"
        "return.err:\n"
        "  ret { i1, i1 } zeroinitializer\n"
        "}\n\n",
        stream
    );
    fputs(
        "define private { i1, i1 } @cn_x11_close(i64 %handle.value) {\n"
        "entry:\n"
        "  %valid = icmp ne i64 %handle.value, 0\n"
        "  br i1 %valid, label %load, label %return.err\n"
        "load:\n"
        "  %handle = inttoptr i64 %handle.value to ptr\n"
        "  %display.ptr = getelementptr inbounds %cn_x11_window, ptr %handle, i32 0, i32 0\n"
        "  %window.ptr = getelementptr inbounds %cn_x11_window, ptr %handle, i32 0, i32 1\n"
        "  %open.ptr = getelementptr inbounds %cn_x11_window, ptr %handle, i32 0, i32 2\n"
        "  %display = load ptr, ptr %display.ptr\n"
        "  %window = load i64, ptr %window.ptr\n"
        "  %open = load i1, ptr %open.ptr\n"
        "  br i1 %open, label %destroy, label %close.display\n"
        "destroy:\n"
        "  store i1 false, ptr %open.ptr\n"
        "  %destroyed = call i32 @XDestroyWindow(ptr %display, i64 %window)\n"
        "  br label %close.display\n"
        "close.display:\n"
        "  %closed = call i32 @XCloseDisplay(ptr %display)\n"
        "  call void @free(ptr %handle)\n"
        "  %ok = insertvalue { i1, i1 } zeroinitializer, i1 true, 0\n"
        "  %value.ok = insertvalue { i1, i1 } %ok, i1 true, 1\n"
        "  ret { i1, i1 } %value.ok\n"
        "return.err:\n"
        "  ret { i1, i1 } zeroinitializer\n"
        "}\n\n",
        stream
    );
#else
    (void)stream;
#endif
}
