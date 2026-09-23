// Window
// ======

#ifdef __OBJC__

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

@interface BendView : NSView <NSWindowDelegate> {
  @public
  NSMutableData* evs;
  u64            flags;
}
@end

@implementation BendView

- (CALayer*)makeBackingLayer {
  return [CAMetalLayer layer];
}

- (BOOL)acceptsFirstResponder {
  return YES;
}

- (BOOL)acceptsFirstMouse:(NSEvent*)ev {
  return YES;
}

- (BOOL)isFlipped {
  return YES;
}

- (void)push:(u32)kind a:(u32)a b:(u32)b c:(u32)c d:(u32)d {
  u32 ev[5] = { kind, a, b, c, d };
  [evs appendBytes:ev length:sizeof ev];
}

- (void)key:(NSEvent*)ev down:(BOOL)down {
  NSString* s = [ev.charactersIgnoringModifiers lowercaseString];
  u32 code = s.length > 0 ? [s characterAtIndex:0] : 65536 + ev.keyCode;
  [self push:0 a:code b:down c:0 d:0];
}

- (void)keyDown:(NSEvent*)ev {
  [self key:ev down:YES];
}

- (void)keyUp:(NSEvent*)ev {
  [self key:ev down:NO];
}

- (void)flagsChanged:(NSEvent*)ev {
  u64 now = ev.modifierFlags;
  [self push:0 a:65536 + ev.keyCode b:(now & ~flags) != 0 c:0 d:0];
  flags = now;
}

- (NSPoint)at:(NSEvent*)ev {
  CGSize  size = ((CAMetalLayer*)self.layer).drawableSize;
  NSPoint p    = [self convertPoint:ev.locationInWindow fromView:nil];
  return NSMakePoint(fmax(0, fmin(floor(p.x), size.width - 1)),
    fmax(0, fmin(floor(p.y), size.height - 1)));
}

- (void)mouse:(NSEvent*)ev down:(BOOL)down {
  NSPoint p = [self at:ev];
  [self push:1 a:p.x b:p.y c:(u32)ev.buttonNumber d:down];
}

- (void)move:(NSEvent*)ev {
  NSPoint p = [self at:ev];
  [self push:2 a:p.x b:p.y c:0 d:0];
}

- (void)mouseDown:(NSEvent*)ev {
  [self mouse:ev down:YES];
}

- (void)mouseUp:(NSEvent*)ev {
  [self mouse:ev down:NO];
}

- (void)rightMouseDown:(NSEvent*)ev {
  [self mouse:ev down:YES];
}

- (void)rightMouseUp:(NSEvent*)ev {
  [self mouse:ev down:NO];
}

- (void)otherMouseDown:(NSEvent*)ev {
  [self mouse:ev down:YES];
}

- (void)otherMouseUp:(NSEvent*)ev {
  [self mouse:ev down:NO];
}

- (void)mouseMoved:(NSEvent*)ev {
  [self move:ev];
}

- (void)mouseDragged:(NSEvent*)ev {
  [self move:ev];
}

- (void)rightMouseDragged:(NSEvent*)ev {
  [self move:ev];
}

- (void)otherMouseDragged:(NSEvent*)ev {
  [self move:ev];
}

- (BOOL)windowShouldClose:(NSWindow*)sender {
  [self push:3 a:0 b:0 c:0 d:0];
  return NO;
}

@end

static id<MTLDevice> window_dev;

static u32 window_make(const char* title, u32 w, u32 h, intptr_t* out,
  const char** why) {
  if (w < 1 || h < 1 || w > 16384 || h > 16384) {
    return EINVAL;
  }
  if (NSScreen.screens.count == 0) {
    *why = "Window.open: no display (build a native binary with bend <file> -o <out> and run it from a desktop session)";
    return ENOTSUP;
  }
  if (window_dev == nil) {
    window_dev = gpu_buf != nil ? gpu_dev : MTLCreateSystemDefaultDevice();
  }
  if (window_dev == nil) {
    *why = "Window.open: no Metal device";
    return ENXIO;
  }
  if (NSApp == nil) {
    [NSApplication sharedApplication];
    NSApp.activationPolicy = NSApplicationActivationPolicyRegular;
    [NSApp finishLaunching];
  }
  @autoreleasepool {
    NSWindow* win = [[NSWindow alloc]
      initWithContentRect:NSMakeRect(0, 0, 1, 1)
      styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
        | NSWindowStyleMaskMiniaturizable
      backing:NSBackingStoreBuffered defer:NO];
    win.releasedWhenClosed = NO;
    win.acceptsMouseMovedEvents = YES;
    win.title = [NSString stringWithCString:title
      encoding:NSISOLatin1StringEncoding];
    [win setContentSize:NSMakeSize(w, h)];
    BendView* view = [[BendView alloc] initWithFrame:win.contentLayoutRect];
    view->evs   = [NSMutableData new];
    view->flags = NSEvent.modifierFlags;
    view.wantsLayer = YES;
    CAMetalLayer* layer = (CAMetalLayer*)view.layer;
    layer.device = window_dev;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = NO;
    layer.drawableSize = CGSizeMake(w, h);
    layer.displaySyncEnabled = YES;
    layer.maximumDrawableCount = 2;
    win.contentView = view;
    win.delegate = view;
    [win makeFirstResponder:view];
    [win center];
    [win makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    *out = (intptr_t)CFBridgingRetain(win);
  }
  return 0;
}

#elif defined(__linux__)

// The X11 window: its own connection (so its queue holds only its
// events), the frame's image and the events pumped since the last
// frame, five words each (kind, a, b, c, d) as on the Mac. The same
// block sits in window_frame.c and window_close.c under this guard.
#ifndef BendWin
#define BendWin BendWin
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

typedef struct {
  Display* dpy;
  Window   win;
  Atom     del;
  XImage*  img;
  u32      n;
  u32      cap;
  u32*     evs;
} BendWin;
#endif

static u32 window_make(const char* title, u32 w, u32 h, intptr_t* out,
  const char** why) {
  if (w < 1 || h < 1 || w > 16384 || h > 16384) {
    return EINVAL;
  }
  Display* dpy = XOpenDisplay(NULL);
  if (dpy == NULL) {
    *why = "Window.open: no display (build a native binary with bend <file> -o <out> and run it from a desktop session)";
    return ENOTSUP;
  }
  int scr = DefaultScreen(dpy);
  if (DefaultDepth(dpy, scr) < 24) {
    XCloseDisplay(dpy);
    *why = "Window.open: the display has no 24-bit visual";
    return ENOTSUP;
  }
  BendWin* win = io_mem(calloc(1, sizeof *win));
  win->dpy = dpy;
  win->win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr), 0, 0, w, h, 0, 0,
    BlackPixel(dpy, scr));
  win->del = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
  win->img = XCreateImage(dpy, DefaultVisual(dpy, scr), DefaultDepth(dpy, scr),
    ZPixmap, 0, io_mem(calloc(w * h, 4)), w, h, 32, w * 4);
  win->img->byte_order = LSBFirst;
  XSizeHints hints = { .flags = PMinSize | PMaxSize, .min_width = w,
    .min_height = h, .max_width = w, .max_height = h };
  XSetWMNormalHints(dpy, win->win, &hints);
  XSetWMProtocols(dpy, win->win, &win->del, 1);
  XStoreName(dpy, win->win, title);
  XSelectInput(dpy, win->win, KeyPressMask | KeyReleaseMask | ButtonPressMask
    | ButtonReleaseMask | PointerMotionMask);
  XMapRaised(dpy, win->win);
  XFlush(dpy);
  *out = (intptr_t)win;
  return 0;
}

#elif defined(_WIN32)

#ifndef BendWin
#define BendWin BendWin
typedef struct {
  HWND hwnd;
  u32  w;
  u32  h;
  u32* pix;
  u32  n;
  u32  cap;
  u32* evs;
} BendWin;
#endif

static void window_push(BendWin* win, u32 kind, u32 a, u32 b, u32 c, u32 d) {
  if (win->n == win->cap) {
    win->cap = win->cap == 0 ? 64 : win->cap * 2;
    win->evs = io_mem(realloc(win->evs, win->cap * 20));
  }
  u32 ev[5] = { kind, a, b, c, d };
  memcpy(win->evs + win->n * 5, ev, sizeof ev);
  win->n += 1;
}

static u32 window_clip(int v, u32 most) {
  return v < 0 ? 0 : (u32)v < most ? (u32)v : most - 1;
}

static u32 window_vk(WPARAM vk) {
  switch (vk) {
    case VK_ESCAPE: return 27;
    case VK_RETURN: return 13;
    case VK_TAB:    return 9;
    case VK_BACK:   return 127;
    case VK_UP:     return 63232;
    case VK_DOWN:   return 63233;
    case VK_LEFT:   return 63234;
    case VK_RIGHT:  return 63235;
    case VK_INSERT: return 63271;
    case VK_DELETE: return 63272;
    case VK_HOME:   return 63273;
    case VK_END:    return 63275;
    case VK_PRIOR:  return 63276;
    case VK_NEXT:   return 63277;
    case VK_F1: case VK_F2: case VK_F3: case VK_F4: case VK_F5: case VK_F6:
    case VK_F7: case VK_F8: case VK_F9: case VK_F10: case VK_F11: case VK_F12:
      return 63236 + (u32)(vk - VK_F1);
    case VK_SHIFT:   return 65592;
    case VK_CONTROL: return 65595;
    case VK_MENU:    return 65594;
    case VK_CAPITAL: return 65593;
    case VK_LWIN:    return 65591;
    case VK_RWIN:    return 65590;
    default: break;
  }
  if (vk >= 'A' && vk <= 'Z') {
    return (u32)(vk - 'A' + 'a');
  }
  if (vk >= '0' && vk <= '9') {
    return (u32)vk;
  }
  return 65536 + (u32)vk;
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  BendWin* win = (BendWin*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
  if (win == NULL) {
    return DefWindowProcW(hwnd, msg, wp, lp);
  }
  switch (msg) {
    case WM_CLOSE:
      window_push(win, 3, 0, 0, 0, 0);
      return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
      window_push(win, 0, window_vk(wp), 1, 0, 0);
      return 0;
    case WM_KEYUP:
    case WM_SYSKEYUP:
      window_push(win, 0, window_vk(wp), 0, 0, 0);
      return 0;
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
    case WM_LBUTTONUP:   case WM_RBUTTONUP:   case WM_MBUTTONUP: {
      int x = (short)LOWORD(lp);
      int y = (short)HIWORD(lp);
      u32 b = msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP ? 0
        : msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP ? 2 : 1;
      u32 down = msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN
        || msg == WM_MBUTTONDOWN;
      window_push(win, 1, window_clip(x, win->w), window_clip(y, win->h), b, down);
      return 0;
    }
    case WM_MOUSEMOVE:
      window_push(win, 2, window_clip((short)LOWORD(lp), win->w),
        window_clip((short)HIWORD(lp), win->h), 0, 0);
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      BeginPaint(hwnd, &ps);
      EndPaint(hwnd, &ps);
      return 0;
    }
    default:
      return DefWindowProcW(hwnd, msg, wp, lp);
  }
}

static u32 window_make(const char* title, u32 w, u32 h, intptr_t* out,
  const char** why) {
  if (w < 1 || h < 1 || w > 16384 || h > 16384) {
    return EINVAL;
  }
  static ATOM cls;
  if (cls == 0) {
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = L"BendWin";
    cls = RegisterClassW(&wc);
    if (cls == 0) {
      *why = "Window.open: RegisterClass failed";
      return ENOTSUP;
    }
  }
  RECT r = { 0, 0, (LONG)w, (LONG)h };
  AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX, FALSE);
  int nw = MultiByteToWideChar(CP_UTF8, 0, title, -1, NULL, 0);
  wchar_t* wt = io_mem(malloc((nw > 0 ? nw : 1) * sizeof(wchar_t)));
  if (nw > 0) {
    MultiByteToWideChar(CP_UTF8, 0, title, -1, wt, nw);
  } else {
    wt[0] = 0;
  }
  HWND hwnd = CreateWindowExW(0, L"BendWin", wt,
    WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE,
    CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
    NULL, NULL, GetModuleHandleW(NULL), NULL);
  free(wt);
  if (hwnd == NULL) {
    *why = "Window.open: no display (build a native binary with bend <file> -o <out> and run it from a desktop session)";
    return ENOTSUP;
  }
  BendWin* win = io_mem(calloc(1, sizeof *win));
  win->hwnd = hwnd;
  win->w = w;
  win->h = h;
  win->pix = io_mem(calloc((size_t)w * h, 4));
  SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)win);
  ShowWindow(hwnd, SW_SHOW);
  UpdateWindow(hwnd);
  *out = (intptr_t)win;
  return 0;
}

#else

static u32 window_make(const char* title, u32 w, u32 h, intptr_t* out,
  const char** why) {
  *why = "Window.open: no display (build a native binary with bend <file> -o <out> and run it from a desktop session)";
  return ENOTSUP;
}

#endif

Term window_open_run(Env e, Term* f, IoWork* w) {
  uint64_t n = 0;
  char* title = io_cstr(e, f[0], &n);
  intptr_t out;
  const char* why = NULL;
  u32 q = io_nul(title, n) ? EILSEQ
    : window_make(title, (u32)f[1], (u32)f[2], &out, &why);
  free(title);
  if (q != 0) {
    return io_fail(e, q, why);
  }
  return io_done(e, io_hand(out));
}

static void __attribute__((constructor)) window_open_use(void) {
  io_eff(CID_WINDOW_OPEN, window_open_run, 0);
}
