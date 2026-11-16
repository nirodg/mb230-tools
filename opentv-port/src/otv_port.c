/*
 * OpenTV MB230 sidecar port (v0.1.5)
 *
 * CyanogenMod-style sidecar: run OpenTV userspace on stock vendor
 * kernel/drivers/libs. Display backends:
 *  - dfb window (preferred): sawman window on layer 0 with evidence-backed
 *    DFBWindowDescription layout, UPPER stacking; on-device confirmed
 *    visible and persistent across remote IR input and channel changes.
 *  - dfb layer-surface (fallback): draws to primary layer 0 surface
 *    (visible but contends with aurora repaints on input).
 *  - fbdev (--fbdev): raw /dev/fbN mmap drawing (may be invisible under
 *    compositor/scanout control).
 *
 * Input: probes /dev/ttyS2 UART IR and /dev/ntgpio (non-blocking).
 * WDT: optional --kick-wdt for Linux /dev/watchdog* (best-effort; platform
 * liveness may be MCU/STBC - see docs/REBOOT_AND_LIVENESS.md).
 *
 * Default mode: sidecar (no aurora kill). Takeover is experimental/risky.
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t g_run = 1;

typedef int DFBResult;
typedef DFBResult (*DFBInitFn)(int *argc, char **argv[]);
typedef DFBResult (*DFBCreateFn)(void **interface);
typedef const char* (*DFBErrorStringFn)(DFBResult result);
typedef DFBResult (*GetLayerFn)(void*, unsigned int, void**);
typedef DFBResult (*LayerSetCoopFn)(void*, int);
typedef DFBResult (*LayerGetSurfaceFn)(void*, void**);
typedef DFBResult (*CreateWindowFn)(void*, void*, void**);
typedef DFBResult (*WinGetSurfaceFn)(void*, void**);
typedef DFBResult (*WinSetOpacityFn)(void*, uint8_t);
typedef DFBResult (*WinRaiseFn)(void*);
typedef DFBResult (*WinReleaseFn)(void*);
typedef DFBResult (*SurfGetSizeFn)(void*, int*, int*);
typedef DFBResult (*SurfSetColorFn)(void*, unsigned char, unsigned char, unsigned char, unsigned char);
typedef DFBResult (*SurfFillRectFn)(void*, int, int, int, int);
typedef DFBResult (*SurfClearFn)(void*, unsigned char, unsigned char, unsigned char, unsigned char);
typedef DFBResult (*SurfFlipFn)(void*, void*, unsigned int);
typedef DFBResult (*SurfReleaseFn)(void*);

typedef struct {
    int ok;
    int backend; /* 1=window, 2=layer-surface */
    void *lib;
    void *dfb;
    void *layer;
    void *win;
    void *surf;
    int w, h;
    int ox, oy;
    DFBErrorStringFn err;
    SurfGetSizeFn getSize;
    SurfSetColorFn setColor;
    SurfFillRectFn fillRect;
    SurfClearFn clear;
    SurfFlipFn flip;
    SurfReleaseFn surfRelease;
    WinReleaseFn winRelease;
} DFB;

typedef struct {
    int fd;
    uint8_t *fb;
    size_t smem_len;
    int w, h, bpp, stride;
    long base;
} FB;

typedef struct {
    int enabled;
    int fd;
    int interval_ms;
    long last_kick_ms;
    char path[64];
    unsigned long kicks;
    unsigned long errors;
} Wdkick;

static void on_sig(int s) { (void)s; g_run = 0; }

static long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void logmsg(const char *msg) {
    long ts = (long)time(NULL);
    const char *paths[] = {"/tmp/otv_port.log", "/conf/opentv-port/otv_port.runtime.log", NULL};
    for (int i = 0; paths[i]; i++) {
        FILE *f = fopen(paths[i], "a");
        if (f) { fprintf(f, "[%ld] %s\n", ts, msg); fclose(f); }
    }
    fprintf(stderr, "[otv-port] %s\n", msg);
}

static const char *dfb_err(DFB *d, DFBResult r) { return (d && d->err) ? d->err(r) : "?"; }

static int dfb_bind_surface_ops(DFB *d, void *surf) {
    if (!surf) return -1;
    unsigned long *sv = (unsigned long*)surf;
    d->surf = surf;
    d->getSize = (SurfGetSizeFn)sv[7];
    d->flip    = (SurfFlipFn)sv[20];
    d->clear   = (SurfClearFn)sv[23];
    d->setColor= (SurfSetColorFn)sv[26];
    d->fillRect= (SurfFillRectFn)sv[42];
    d->surfRelease = (SurfReleaseFn)sv[4];
    d->w = 1920; d->h = 1080;
    if (d->getSize) d->getSize(d->surf, &d->w, &d->h);
    return 0;
}

static int dfb_try_window(DFB *d, unsigned int layer_id) {
    /* Correct DFBWindowDescription layout for this vendor lib build
     * (IDirectFBDisplayLayer_CreateWindow @0x4b358244, libdirectfb-1.7.so.1):
     * flags=d[0], caps=d[1], w=d[2], h=d[3], pf=d[4], x=d[5], y=d[6], scaps=d[7], stacking=d[10]
     * flag bits: 0 CAPS,1 WIDTH,2 HEIGHT,3 PIXELFORMAT,4 POSX,5 POSY,6 SURFACE_CAPS,9 STACKING.
     * On-device: stacking=UPPER(2), caps 0x1/0x11/0x2 visible+persistent on layer 0. */
    (void)layer_id;
    if (!d->layer) return -1;
    unsigned long *lv = (unsigned long*)d->layer;
    CreateWindowFn createWindow = (CreateWindowFn)lv[33];
    if (!createWindow) { logmsg("dfb: layer CreateWindow unavailable"); return -1; }

    const unsigned int stackings[] = {2u, 1u};
    const unsigned int capsvals[]  = {0x1u, 0x11u, 0x2u};
    for (int si = 0; si < 2; si++) {
        for (int ci = 0; ci < 3; ci++) {
            uint32_t desc[64]; memset(desc, 0, sizeof(desc));
            unsigned int flags = (1u<<0)|(1u<<1)|(1u<<2)|(1u<<3)|(1u<<4)|(1u<<5)|(1u<<6)|(1u<<9);
            desc[0] = flags;
            desc[1] = capsvals[ci];
            desc[2] = 800;      /* width */
            desc[3] = 500;      /* height */
            desc[4] = 0;        /* pixelformat */
            desc[5] = 40;       /* posx */
            desc[6] = 40;       /* posy */
            desc[7] = 0;        /* surface_caps */
            desc[10]= stackings[si];
            void *win = NULL;
            DFBResult r = createWindow(d->layer, desc, &win);
            if (r || !win) {
                char m[192]; snprintf(m,sizeof(m),"dfb: CreateWindow try caps=0x%x stacking=%u failed %d %s", desc[1], desc[10], r, dfb_err(d,r));
                logmsg(m);
                continue;
            }
            unsigned long *wv = (unsigned long*)win;
            WinGetSurfaceFn getSurf = (WinGetSurfaceFn)wv[10];
            WinSetOpacityFn setOpacity = (WinSetOpacityFn)wv[22];
            WinRaiseFn raiseToTop = (WinRaiseFn)wv[33];
            d->winRelease = (WinReleaseFn)wv[4];
            void *surf = NULL;
            r = getSurf ? getSurf(win, &surf) : -1;
            if (r || !surf) {
                char m[160]; snprintf(m,sizeof(m),"dfb: window GetSurface failed %d %s", r, dfb_err(d,r)); logmsg(m);
                if (d->winRelease) d->winRelease(win);
                d->winRelease = NULL;
                continue;
            }
            if (setOpacity) setOpacity(win, 255);
            if (raiseToTop) raiseToTop(win);
            dfb_bind_surface_ops(d, surf);
            d->win = win;
            d->backend = 1;
            d->ox = 0; d->oy = 0;
            char m[192]; snprintf(m,sizeof(m),"dfb: ok window %dx%d @%d,%d stacking=%u caps=0x%x (layer 0)", (int)desc[2],(int)desc[3],(int)desc[5],(int)desc[6],(unsigned)desc[10],(unsigned)desc[1]); logmsg(m);
            return 0;
        }
    }
    return -1;
}

static int dfb_init(DFB *d) {
    memset(d, 0, sizeof(*d));
    /* Join running mp session; no-sighandler avoids DirectFB's fatal signal
     * trapping on Ctrl-C/SIGTERM which can destabilize shared fusion session. */
    setenv("DFBARGS", "system=fbdev,wm=sawman,no-cursor,no-sighandler,module-dir=/vendor/lib/dfb/directfb-1.7-1", 0);
    const char *paths[] = {"/vendor/lib/dfb/libdirectfb-1.7.so.1", "/mtd_apexe/board/lib/dfb/libdirectfb-1.7.so.1", "libdirectfb-1.7.so.1", NULL};
    for (int i = 0; paths[i]; i++) {
        d->lib = dlopen(paths[i], RTLD_NOW|RTLD_GLOBAL);
        if (d->lib) { char m[160]; snprintf(m,sizeof(m),"dfb: dlopen ok %s", paths[i]); logmsg(m); break; }
    }
    if (!d->lib) { char m[256]; snprintf(m,sizeof(m),"dfb: dlopen failed: %s", dlerror()); logmsg(m); return -1; }
    DFBInitFn pInit = (DFBInitFn)dlsym(d->lib, "DirectFBInit");
    DFBCreateFn pCreate = (DFBCreateFn)dlsym(d->lib, "DirectFBCreate");
    d->err = (DFBErrorStringFn)dlsym(d->lib, "DirectFBErrorString");
    if (!pInit || !pCreate) { logmsg("dfb: missing DirectFBInit/Create"); return -1; }
    int ac = 1; char *avv[] = {"otv_port", NULL}; char **av = avv;
    DFBResult r = pInit(&ac, &av);
    if (r) { char m[128]; snprintf(m,sizeof(m),"dfb: DirectFBInit failed %d %s", r, dfb_err(d,r)); logmsg(m); return -1; }
    r = pCreate(&d->dfb);
    if (r || !d->dfb) { char m[128]; snprintf(m,sizeof(m),"dfb: DirectFBCreate failed %d %s", r, dfb_err(d,r)); logmsg(m); return -1; }
    GetLayerFn getLayer = (GetLayerFn)(((unsigned long*)d->dfb)[14]);
    r = getLayer ? getLayer(d->dfb, 0, &d->layer) : -1;
    if (r || !d->layer) { char m[128]; snprintf(m,sizeof(m),"dfb: GetDisplayLayer(0) failed %d %s", r, dfb_err(d,r)); logmsg(m); return -1; }

    /* Preferred: sawman window overlay (persistent across IR/channel).
     * Try before changing layer coop to match proven probe behavior. */
    if (dfb_try_window(d, 0) == 0) { d->ok = 1; return 0; }

    /* Fallback: primary layer surface (visible but contends with aurora repaints). */
    logmsg("dfb: window backend failed, trying layer-surface fallback");
    unsigned long *lv = (unsigned long*)d->layer;
    LayerSetCoopFn setCoop = (LayerSetCoopFn)lv[11];
    if (setCoop) setCoop(d->layer, 1 /*SHARED*/);
    LayerGetSurfaceFn getSurf = (LayerGetSurfaceFn)lv[9];
    void *lsurf = NULL;
    r = getSurf ? getSurf(d->layer, &lsurf) : -1;
    if (r || !lsurf) { char m[128]; snprintf(m,sizeof(m),"dfb: GetSurface failed %d %s", r, dfb_err(d,r)); logmsg(m); return -1; }
    dfb_bind_surface_ops(d, lsurf);
    d->backend = 2;
    d->ox = 0; d->oy = 0;
    d->ok = 1;
    char m[128]; snprintf(m,sizeof(m),"dfb: ok layer-surface %dx%d (primary layer 0)", d->w, d->h); logmsg(m);
    return 0;
}

static void dfb_shutdown(DFB *d) {
    /* Gentle, ordered cleanup for shared fusion/sawman session:
     * clear+flip to remove overlay, then release window surface/window. */
    if (d->ok && d->surf && d->clear && d->flip) {
        d->clear(d->surf, 0, 0, 0, 0);
        d->flip(d->surf, NULL, 0);
    }
    if (d->backend == 1) {
        if (d->surf && d->surfRelease) d->surfRelease(d->surf);
        if (d->win && d->winRelease) d->winRelease(d->win);
    }
    d->surf = NULL; d->win = NULL; d->ok = 0;
}

static void dfb_fill(DFB *d, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!d->ok || !d->setColor || !d->fillRect) return;
    if (w <= 0 || h <= 0) return;
    d->setColor(d->surf, r,g,b,a);
    d->fillRect(d->surf, x,y,w,h);
}

static void wdkick_open(Wdkick *wd) {
    const char *paths[] = {"/dev/watchdog", "/dev/watchdog0", "/dev/misc/watchdog", NULL};
    wd->fd = -1; wd->path[0] = '\0';
    for (int i = 0; paths[i]; i++) {
        int fd = open(paths[i], O_WRONLY);
        if (fd >= 0) { wd->fd = fd; snprintf(wd->path, sizeof(wd->path), "%s", paths[i]); char b[128]; snprintf(b,sizeof(b),"watchdog opened: %s", wd->path); logmsg(b); return; }
    }
    logmsg("watchdog not available (/dev/watchdog*); relying on platform WDT behavior");
}

static void wdkick_tick(Wdkick *wd, long tms) {
    if (!wd->enabled || wd->fd < 0) return;
    if (tms - wd->last_kick_ms < wd->interval_ms) return;
    ssize_t n = write(wd->fd, "\0", 1);
    if (n <= 0) { wd->errors++; if ((wd->errors % 20) == 1) logmsg("watchdog kick write failed"); }
    else wd->kicks++;
    wd->last_kick_ms = tms;
}

static void wdkick_close(Wdkick *wd) { if (wd->fd >= 0) { close(wd->fd); wd->fd = -1; } }

static void put_px(FB *fb, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (!fb->fb) return;
    if (x < 0 || y < 0 || x >= fb->w || y >= fb->h) return;
    uint8_t *p = fb->fb + fb->base + (long)y * fb->stride + (long)x * fb->bpp;
    if (fb->bpp == 4) { p[0]=b; p[1]=g; p[2]=r; p[3]=0xff; }
    else if (fb->bpp == 3) { p[0]=b; p[1]=g; p[2]=r; }
    else if (fb->bpp == 2) { uint16_t v=(uint16_t)(((r>>3)<<11)|((g>>2)<<5)|(b>>3)); p[0]=v&0xff; p[1]=(v>>8)&0xff; }
}

static void fb_fill_rect(FB *fb, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    int x2=x+w, y2=y+h; if (x<0)x=0; if(y<0)y=0; if(x2>fb->w)x2=fb->w; if(y2>fb->h)y2=fb->h;
    for (int yy=y; yy<y2; yy++) for (int xx=x; xx<x2; xx++) put_px(fb,xx,yy,r,g,b);
}

/* 5x7 bitmap font */
typedef struct { char c; uint8_t row[7]; } Glyph;
static const Glyph font[] = {
    {' ', {0,0,0,0,0,0,0}}, {'A',{0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}}, {'B',{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}},
    {'C',{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}}, {'D',{0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}}, {'E',{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}},
    {'F',{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}}, {'G',{0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}}, {'H',{0x11,0x11,0x11,0x1F,0x11,0x11,0x11}},
    {'I',{0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}}, {'J',{0x01,0x01,0x01,0x01,0x11,0x11,0x0E}}, {'K',{0x11,0x12,0x14,0x18,0x14,0x12,0x11}},
    {'L',{0x10,0x10,0x10,0x10,0x10,0x10,0x1F}}, {'M',{0x11,0x1B,0x15,0x15,0x11,0x11,0x11}}, {'N',{0x11,0x19,0x15,0x13,0x11,0x11,0x11}},
    {'O',{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}}, {'P',{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}}, {'Q',{0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}},
    {'R',{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}}, {'S',{0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}}, {'T',{0x1F,0x04,0x04,0x04,0x04,0x04,0x04}},
    {'U',{0x11,0x11,0x11,0x11,0x11,0x11,0x0E}}, {'V',{0x11,0x11,0x11,0x11,0x11,0x0A,0x04}}, {'W',{0x11,0x11,0x11,0x15,0x15,0x15,0x0A}},
    {'X',{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}}, {'Y',{0x11,0x11,0x0A,0x04,0x04,0x04,0x04}}, {'Z',{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}},
    {'0',{0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}}, {'1',{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}}, {'2',{0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}},
    {'3',{0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E}}, {'4',{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}}, {'5',{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}},
    {'6',{0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}}, {'7',{0x1F,0x01,0x02,0x04,0x08,0x08,0x08}}, {'8',{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}},
    {'9',{0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}},
    {':',{0,0x04,0,0,0x04,0,0}}, {'-',{0,0,0,0x1F,0,0,0}}, {'_',{0,0,0,0,0,0,0x1F}}, {'.',{0,0,0,0,0,0,0x04}},
    {'/',{0x01,0x01,0x02,0x04,0x08,0x10,0x10}}, {'(',{0x02,0x04,0x08,0x08,0x08,0x04,0x02}}, {')',{0x08,0x04,0x02,0x02,0x02,0x04,0x08}},
    {'[',{0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}}, {']',{0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}}, {'!',{0x04,0x04,0x04,0x04,0x04,0,0x04}},
    {'=',{0,0,0x1F,0,0x1F,0,0}}, {'+',{0,0x04,0x04,0x1F,0x04,0x04,0}}, {'?',{0x0E,0x11,0x01,0x02,0x04,0,0x04}},
    {0,{0}}
};

static const uint8_t *glyph_for(char c) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    for (int i = 0; font[i].c; i++) if (font[i].c == c) return font[i].row;
    return font[0].row;
}

static void draw_text_common(int x, int y, int scale, const char *s, uint8_t r, uint8_t g, uint8_t b,
                             void (*rect)(void*,int,int,int,int,uint8_t,uint8_t,uint8_t), void *ctx) {
    int cx = x;
    for (; s && *s; s++) {
        const uint8_t *rows = glyph_for(*s);
        for (int ry = 0; ry < 7; ry++) for (int rx = 0; rx < 5; rx++)
            if (rows[ry] & (1 << (4 - rx))) rect(ctx, cx + rx*scale, y + ry*scale, scale, scale, r,g,b);
        cx += 6 * scale;
    }
}

static void fb_rect_cb(void *ctx, int x,int y,int w,int h,uint8_t r,uint8_t g,uint8_t b){ fb_fill_rect((FB*)ctx,x,y,w,h,r,g,b); }
static void dfb_rect_cb2(void *ctx, int x,int y,int w,int h,uint8_t r,uint8_t g,uint8_t b){ dfb_fill((DFB*)ctx,x,y,w,h,r,g,b,255); }
static void dfb_rect_a(DFB *d, int x,int y,int w,int h,uint8_t r,uint8_t g,uint8_t b,uint8_t a){ dfb_fill(d,x,y,w,h,r,g,b,a); }

static int set_raw(int fd) {
    struct termios tio; if (tcgetattr(fd,&tio)!=0) return -1;
    cfmakeraw(&tio); tio.c_cc[VMIN]=0; tio.c_cc[VTIME]=0; return tcsetattr(fd,TCSANOW,&tio);
}

static void usage(const char *a) {
    fprintf(stderr,
        "Usage: %s [--dfb] [--fbdev] [--no-ui] [--no-input] [--fb PATH] [--fb-index N]\n"
        "           [--kick-wdt] [--wdt-interval-ms N] [--redraw-ms N]\n"
        "  --dfb            render via DirectFB (window overlay preferred, layer-surface fallback)\n"
        "  --fbdev          render via raw framebuffer mmap (fallback)\n"
        "  --kick-wdt       best-effort Linux /dev/watchdog* kick (platform WDT may be MCU/STBC)\n"
        "  --wdt-interval-ms N  kick interval (default 1000)\n"
        "  --redraw-ms N    UI redraw interval ms (default 500, reduces compositor contention)\n"
        "  --no-ui          no display rendering\n"
        "  --no-input       do not probe ttyS2/ntgpio\n"
        "  --fb PATH / --fb-index N  fbdev device (for --fbdev)\n", a);
}

int main(int argc, char **argv) {
    int ui = 1, probe_input = 1, want_dfb = 1, force_fbdev = 0;
    int redraw_ms = 500; /* gentle redraw to reduce fusion/compositor contention */
    Wdkick wd; memset(&wd,0,sizeof(wd)); wd.interval_ms = 1000;
    const char *fbdev = getenv("FRAMEBUFFER"); char fbpath[32]; fbpath[0]='\0';

    for (int i=1;i<argc;i++) {
        if (!strcmp(argv[i],"--dfb")) { want_dfb = 1; force_fbdev = 0; }
        else if (!strcmp(argv[i],"--fbdev")) { force_fbdev = 1; want_dfb = 0; }
        else if (!strcmp(argv[i],"--kick-wdt")) wd.enabled = 1;
        else if (!strcmp(argv[i],"--no-ui")) ui = 0;
        else if (!strcmp(argv[i],"--no-input")) probe_input = 0;
        else if (!strcmp(argv[i],"--fb") && i+1<argc) fbdev = argv[++i];
        else if (!strcmp(argv[i],"--fb-index") && i+1<argc) { snprintf(fbpath,sizeof(fbpath),"/dev/fb%s",argv[++i]); fbdev=fbpath; }
        else if (!strcmp(argv[i],"--wdt-interval-ms") && i+1<argc) { wd.interval_ms=atoi(argv[++i]); if (wd.interval_ms<100) wd.interval_ms=100; }
        else if (!strcmp(argv[i],"--redraw-ms") && i+1<argc) { redraw_ms=atoi(argv[++i]); if (redraw_ms<50) redraw_ms=50; if (redraw_ms>5000) redraw_ms=5000; }
        else if (!strcmp(argv[i],"-h") || !strcmp(argv[i],"--help")) { usage(argv[0]); return 0; }
    }
    if (!fbdev) fbdev = "/dev/fb0";

    /* install our handlers early; dfb init uses no-sighandler to avoid
     * DirectFB hijacking/aborting on SIGINT/SIGTERM in shared session */
    signal(SIGINT,on_sig); signal(SIGTERM,on_sig);
    signal(SIGHUP,on_sig);
    { char m[160]; snprintf(m,sizeof(m),"starting OpenTV MB230 port v0.1.6 (sidecar, redraw %dms)", redraw_ms); logmsg(m); }
    if (wd.enabled) wdkick_open(&wd);

    DFB dfb; memset(&dfb,0,sizeof(dfb));
    FB fb; memset(&fb,0,sizeof(fb)); fb.fd=-1;
    int use_dfb = 0;

    if (ui && want_dfb && !force_fbdev) {
        if (dfb_init(&dfb) == 0) use_dfb = 1;
        else logmsg("dfb init failed, falling back to fbdev");
    }
    if (ui && !use_dfb) {
        fb.fd = open(fbdev, O_RDWR);
        if (fb.fd >= 0) {
            struct fb_var_screeninfo v; struct fb_fix_screeninfo f;
            if (ioctl(fb.fd,FBIOGET_VSCREENINFO,&v)==0 && ioctl(fb.fd,FBIOGET_FSCREENINFO,&f)==0) {
                fb.w=v.xres; fb.h=v.yres; fb.bpp=v.bits_per_pixel/8; fb.stride=f.line_length; fb.smem_len=f.smem_len;
                fb.fb=mmap(0,f.smem_len,PROT_READ|PROT_WRITE,MAP_SHARED,fb.fd,0);
                if (fb.fb==MAP_FAILED){ fb.fb=NULL; logmsg("fb mmap failed"); }
                else { fb.base=(long)v.yoffset*fb.stride+(long)v.xoffset*fb.bpp; char m[192]; snprintf(m,sizeof(m),"fb ok %s %dx%d %dbpp stride=%d",fbdev,fb.w,fb.h,v.bits_per_pixel,fb.stride); logmsg(m); }
            } else { char m[128]; snprintf(m,sizeof(m),"fb ioctl failed on %s",fbdev); logmsg(m); }
        } else { char m[128]; snprintf(m,sizeof(m),"open %s failed (no UI)",fbdev); logmsg(m); }
    }

    int ttyfd=-1, gpiofd=-1;
    if (probe_input) {
        ttyfd=open("/dev/ttyS2",O_RDONLY|O_NONBLOCK);
        gpiofd=open("/dev/ntgpio",O_RDONLY|O_NONBLOCK);
        if (ttyfd>=0){ set_raw(ttyfd); logmsg("input: /dev/ttyS2 opened"); } else logmsg("input: /dev/ttyS2 unavailable (aurora may own it)");
        if (gpiofd>=0) logmsg("input: /dev/ntgpio opened");
    }

    unsigned long events=0, frames=0; long last_any=0, last_draw=0, start=now_ms(); char last_src[16]="-"; unsigned char last_bytes[16]; int last_n=0;

    while (g_run) {
        long t = now_ms();
        wdkick_tick(&wd, t);

        if (ui && (use_dfb ? dfb.ok : (fb.fb!=NULL)) && (t - last_draw >= redraw_ms)) {
            last_draw = t;
            int W = use_dfb ? dfb.w : fb.w;
            int H = use_dfb ? dfb.h : fb.h;
            void (*R)(void*,int,int,int,int,uint8_t,uint8_t,uint8_t) = use_dfb ? NULL : fb_rect_cb;
            void *ctx = use_dfb ? (void*)&dfb : (void*)&fb;
            /* Draw UI panel. For window backend, surface is the window (overlay
             * persists across aurora input/channel repaints). For layer-surface,
             * repaint a semi-transparent panel each frame on shared primary layer. */
            int px = (use_dfb && dfb.backend == 1) ? 0 : 40;
            int py = (use_dfb && dfb.backend == 1) ? 0 : 40;
            int pw = W - (px ? 80 : 0);
            int ph = 580;
            if (ph > H - 20) ph = H - 20;
            if (pw < 100) pw = W;
            if (use_dfb) {
                dfb_rect_a(&dfb, 0, 0, W, H, 0, 0, 0, 0); /* transparent clear */
                dfb_rect_a(&dfb, px, py, pw, ph, 10, 14, 24, 210);
                dfb_rect_a(&dfb, px, py, pw, 4, 70, 120, 200, 230);
                dfb_rect_a(&dfb, px, py+ph-4, pw, 4, 70, 120, 200, 230);
                dfb_rect_a(&dfb, px, py, 4, ph, 70, 120, 200, 230);
                dfb_rect_a(&dfb, px+pw-4, py, 4, ph, 70, 120, 200, 230);
            } else {
                R(ctx, 0, 0, W, H, 10, 14, 24);
                R(ctx, px, py, pw, ph, 10, 14, 24);
                R(ctx, px, py, pw, 4, 70, 120, 200);
                R(ctx, px, py+ph-4, pw, 4, 70, 120, 200);
                R(ctx, px, py, 4, ph, 70, 120, 200);
                R(ctx, px+pw-4, py, 4, ph, 70, 120, 200);
            }
            int tx = px + 40, ty = py + 30;
            void (*Rtext)(void*,int,int,int,int,uint8_t,uint8_t,uint8_t) = use_dfb ? dfb_rect_cb2 : fb_rect_cb;
            draw_text_common(tx, ty, 6, "OPENTV PORT", 245,247,251, Rtext, ctx);
            draw_text_common(tx, ty+70, 3, "MB230 VESTEL SIDECAR - V0.1.5", 140,190,255, Rtext, ctx);
            draw_text_common(tx, ty+120, 3, "VENDOR BASE + OPENTV USERSPACE", 180,220,180, Rtext, ctx);
            char line[96];
            const char *rname = "FBDEV";
            if (use_dfb) rname = (dfb.backend==1) ? "DFB-WIN" : "DFB-LAYER";
            snprintf(line,sizeof(line),"UPTIME: %ld S  FRAMES: %lu  RENDER: %s  DRAW:%dMS", (t-start)/1000, ++frames, rname, redraw_ms);
            draw_text_common(tx, ty+190, 3, line, 230,230,150, Rtext, ctx);
            snprintf(line,sizeof(line),"INPUT EVENTS: %lu   LAST: %s", events, last_src);
            draw_text_common(tx, ty+230, 3, line, 230,230,150, Rtext, ctx);
            if (wd.enabled) snprintf(line,sizeof(line),"WDT KICK: %s  KICKS=%lu ERR=%lu", wd.fd>=0?wd.path:"NONE", wd.kicks, wd.errors);
            else snprintf(line,sizeof(line),"WDT KICK: OFF (PLATFORM/AURORA LIVENESS)");
            draw_text_common(tx, ty+270, 3, line, 255,200,120, Rtext, ctx);
            if (last_n>0) {
                char hex[96]="LAST BYTES: "; int off=12;
                for (int i=0;i<last_n && i<8 && off<(int)sizeof(hex)-4;i++) off+=snprintf(hex+off,sizeof(hex)-off,"%02X ",last_bytes[i]);
                draw_text_common(tx, ty+330, 3, hex, 180,220,220, Rtext, ctx);
            }
            if (t-last_any < 1200) draw_text_common(tx, ty+400, 4, "INPUT RECEIVED", 80,255,140, Rtext, ctx);
            else draw_text_common(tx, ty+400, 4, "SIDECAR READY", 170,170,190, Rtext, ctx);
            draw_text_common(tx, py+ph-30, 2, "AURORA ALIVE. WINDOW OVERLAY PERSISTS ACROSS INPUT.", 220,180,150, Rtext, ctx);
            if (use_dfb && dfb.flip) dfb.flip(dfb.surf, NULL, 0);
        }

        fd_set rfds; FD_ZERO(&rfds); int maxfd=-1;
        if (ttyfd>=0){ FD_SET(ttyfd,&rfds); if(ttyfd>maxfd) maxfd=ttyfd; }
        if (gpiofd>=0){ FD_SET(gpiofd,&rfds); if(gpiofd>maxfd) maxfd=gpiofd; }
        long wait_ms = redraw_ms - (now_ms() - last_draw);
        if (wait_ms < 20) wait_ms = 20;
        if (wait_ms > 200) wait_ms = 200;
        struct timeval tv; tv.tv_sec=0; tv.tv_usec = wait_ms * 1000;
        int sel = (maxfd>=0)?select(maxfd+1,&rfds,NULL,NULL,&tv):0;
        if (sel==0 && maxfd<0) usleep(wait_ms * 1000);
        if (sel>0) {
            unsigned char buf[64];
            if (ttyfd>=0 && FD_ISSET(ttyfd,&rfds)) { int n=read(ttyfd,buf,sizeof(buf)); if(n>0){ events++; last_any=now_ms(); last_n=n<16?n:16; memcpy(last_bytes,buf,last_n); snprintf(last_src,sizeof(last_src),"TTYS2"); char m[128]; snprintf(m,sizeof(m),"ttyS2 %d bytes",n); logmsg(m);} }
            if (gpiofd>=0 && FD_ISSET(gpiofd,&rfds)) { int n=read(gpiofd,buf,sizeof(buf)); if(n>0){ events++; last_any=now_ms(); last_n=n<16?n:16; memcpy(last_bytes,buf,last_n); snprintf(last_src,sizeof(last_src),"GPIO"); char m[128]; snprintf(m,sizeof(m),"ntgpio %d bytes",n); logmsg(m);} }
        }
    }

    logmsg("stopping");
    if (ttyfd>=0) close(ttyfd);
    if (gpiofd>=0) close(gpiofd);
    if (use_dfb) dfb_shutdown(&dfb);
    if (fb.fb) munmap(fb.fb, fb.smem_len);
    if (fb.fd>=0) close(fb.fd);
    wdkick_close(&wd);
    return 0;
}
