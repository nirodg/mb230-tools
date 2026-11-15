/*
 * OpenTV MB230 sidecar port - first tryable version (v0.1)
 *
 * CyanogenMod-style sidecar goal: run OpenTV userspace on top of stock
 * vendor kernel/drivers/libs. This binary is a minimal launcher/UI shell:
 *  - draws a simple status UI to /dev/fb0 (fbdev) and can probe input
 *    devices (/dev/ttyS2 UART IR, /dev/ntgpio) without requiring evdev.
 *  - optional Linux watchdog kicker (--kick-wdt): opens /dev/watchdog or
 *    /dev/watchdog0 and writes periodically to keep it alive (best-effort;
 *    on MB230 the primary liveness/reset path may be MCU/STBC WDT handled
 *    by aurora/platform libs - see docs/REBOOT_AND_LIVENESS.md).
 *
 * Modes of use:
 *  - sidecar (default, safe-ish): run alongside stock aurora for logging/UI
 *    experiments; display/input may be owned by aurora so visibility/events
 *    are not guaranteed.
 *  - takeover (experimental, risky): stop aurora/idtv/appmgr then run this as
 *    foreground UI. If no process kicks the platform watchdog, the TV may
 *    reboot (~20-30s). Use --kick-wdt and/or a keeper strategy as documented.
 *
 * Build: static ARMv7 EABI5 hard-float (see build-docker.sh).
 */

#define _GNU_SOURCE
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
static volatile sig_atomic_t g_redraw = 1;

static void on_sig(int s) {
    (void)s;
    g_run = 0;
}

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

static long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void logmsg(const char *msg) {
    FILE *f = fopen("/tmp/otv_port.log", "a");
    if (f) {
        fprintf(f, "[%ld] %s\n", (long)time(NULL), msg);
        fclose(f);
    }
    fprintf(stderr, "[otv-port] %s\n", msg);
}

static void wdkick_open(Wdkick *wd) {
    const char *paths[] = {"/dev/watchdog", "/dev/watchdog0", "/dev/misc/watchdog", NULL};
    wd->fd = -1;
    wd->path[0] = '\0';
    for (int i = 0; paths[i]; i++) {
        int fd = open(paths[i], O_WRONLY);
        if (fd >= 0) {
            wd->fd = fd;
            snprintf(wd->path, sizeof(wd->path), "%s", paths[i]);
            char buf[128];
            snprintf(buf, sizeof(buf), "watchdog opened: %s", wd->path);
            logmsg(buf);
            return;
        }
    }
    logmsg("watchdog not available (/dev/watchdog* open failed); relying on platform WDT behavior");
}

static void wdkick_tick(Wdkick *wd, long tms) {
    if (!wd->enabled) return;
    if (wd->fd < 0) return;
    if (tms - wd->last_kick_ms < wd->interval_ms) return;
    ssize_t n = write(wd->fd, "\0", 1);
    if (n <= 0) {
        /* try ioctl keepalive style fallback not used; count error and retry reopen */
        wd->errors++;
        if ((wd->errors % 20) == 1) logmsg("watchdog kick write failed");
    } else {
        wd->kicks++;
    }
    wd->last_kick_ms = tms;
}

static void wdkick_close(Wdkick *wd) {
    if (wd->fd >= 0) {
        /* best-effort: write 'V' (magic close) only if explicitly desired in future;
           default: just close to avoid accidental disable semantics differences */
        close(wd->fd);
        wd->fd = -1;
    }
}

static void put_px(FB *fb, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (!fb->fb) return;
    if (x < 0 || y < 0 || x >= fb->w || y >= fb->h) return;
    uint8_t *p = fb->fb + fb->base + (long)y * fb->stride + (long)x * fb->bpp;
    if (fb->bpp == 4) {
        p[0] = b; p[1] = g; p[2] = r; p[3] = 0xff;
    } else if (fb->bpp == 3) {
        p[0] = b; p[1] = g; p[2] = r;
    } else if (fb->bpp == 2) {
        uint16_t v = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        p[0] = v & 0xff; p[1] = (v >> 8) & 0xff;
    }
}

static void fill_rect(FB *fb, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    int x2 = x + w, y2 = y + h;
    if (x < 0) x = 0; if (y < 0) y = 0;
    if (x2 > fb->w) x2 = fb->w; if (y2 > fb->h) y2 = fb->h;
    for (int yy = y; yy < y2; yy++) {
        for (int xx = x; xx < x2; xx++) put_px(fb, xx, yy, r, g, b);
    }
}

static void draw_border(FB *fb, int x, int y, int w, int h, int t, uint8_t r, uint8_t g, uint8_t b) {
    fill_rect(fb, x, y, w, t, r, g, b);
    fill_rect(fb, x, y + h - t, w, t, r, g, b);
    fill_rect(fb, x, y, t, h, r, g, b);
    fill_rect(fb, x + w - t, y, t, h, r, g, b);
}

/* 5x7 bitmap font for basic UI text */
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

static void draw_text(FB *fb, int x, int y, int scale, const char *s, uint8_t r, uint8_t g, uint8_t b) {
    int cx = x;
    for (; s && *s; s++) {
        const uint8_t *rows = glyph_for(*s);
        for (int ry = 0; ry < 7; ry++) {
            for (int rx = 0; rx < 5; rx++) {
                if (rows[ry] & (1 << (4 - rx))) {
                    fill_rect(fb, cx + rx * scale, y + ry * scale, scale, scale, r, g, b);
                }
            }
        }
        cx += 6 * scale;
    }
}

static int set_raw(int fd) {
    struct termios tio;
    if (tcgetattr(fd, &tio) != 0) return -1;
    cfmakeraw(&tio);
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;
    return tcsetattr(fd, TCSANOW, &tio);
}

static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage: %s [--kick-wdt] [--wdt-interval-ms N] [--no-ui] [--no-input]\n"
        "  --kick-wdt           open/write /dev/watchdog* periodically (best-effort)\n"
        "  --wdt-interval-ms N  kick interval (default 1000)\n"
        "  --no-ui              do not draw to framebuffer\n"
        "  --no-input           do not probe ttyS2/ntgpio\n",
        argv0);
}

int main(int argc, char **argv) {
    int ui = 1, probe_input = 1;
    Wdkick wd; memset(&wd, 0, sizeof(wd));
    wd.interval_ms = 1000;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--kick-wdt") == 0) wd.enabled = 1;
        else if (strcmp(argv[i], "--no-ui") == 0) ui = 0;
        else if (strcmp(argv[i], "--no-input") == 0) probe_input = 0;
        else if (strcmp(argv[i], "--wdt-interval-ms") == 0 && i + 1 < argc) { wd.interval_ms = atoi(argv[++i]); if (wd.interval_ms < 100) wd.interval_ms = 100; }
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) { usage(argv[0]); return 0; }
    }

    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);

    logmsg("starting OpenTV MB230 port v0.1");
    if (wd.enabled) wdkick_open(&wd);

    FB fb; memset(&fb, 0, sizeof(fb));
    fb.fd = -1;
    if (ui) {
        const char *fbdev = getenv("FRAMEBUFFER"); if (!fbdev) fbdev = "/dev/fb0";
        fb.fd = open(fbdev, O_RDWR);
        if (fb.fd >= 0) {
            struct fb_var_screeninfo v; struct fb_fix_screeninfo f;
            if (ioctl(fb.fd, FBIOGET_VSCREENINFO, &v) == 0 && ioctl(fb.fd, FBIOGET_FSCREENINFO, &f) == 0) {
                fb.w = v.xres; fb.h = v.yres; fb.bpp = v.bits_per_pixel / 8; fb.stride = f.line_length;
                fb.smem_len = f.smem_len;
                fb.fb = mmap(0, f.smem_len, PROT_READ|PROT_WRITE, MAP_SHARED, fb.fd, 0);
                if (fb.fb == MAP_FAILED) { fb.fb = NULL; logmsg("fb mmap failed"); }
                else {
                    fb.base = (long)v.yoffset * fb.stride + (long)v.xoffset * fb.bpp;
                    char msg[160]; snprintf(msg, sizeof(msg), "fb ok %dx%d %dbpp stride=%d", fb.w, fb.h, v.bits_per_pixel, fb.stride);
                    logmsg(msg);
                }
            } else {
                logmsg("fb ioctl failed");
            }
        } else {
            logmsg("open /dev/fb0 failed (continuing without UI)");
        }
    }

    int ttyfd = -1, gpiofd = -1;
    if (probe_input) {
        ttyfd = open("/dev/ttyS2", O_RDONLY | O_NONBLOCK);
        gpiofd = open("/dev/ntgpio", O_RDONLY | O_NONBLOCK);
        if (ttyfd >= 0) { set_raw(ttyfd); logmsg("input: /dev/ttyS2 opened"); }
        else logmsg("input: /dev/ttyS2 unavailable (aurora may own it)");
        if (gpiofd >= 0) logmsg("input: /dev/ntgpio opened");
    }

    unsigned long events = 0, frames = 0;
    long last_any = 0, start = now_ms();
    char last_src[16] = "-";
    unsigned char last_bytes[16]; int last_n = 0;

    while (g_run) {
        long t = now_ms();
        wdkick_tick(&wd, t);

        if (ui && fb.fb) {
            fill_rect(&fb, 0, 0, fb.w, fb.h, 10, 14, 24);
            draw_border(&fb, 40, 40, fb.w - 80, fb.h - 80, 4, 70, 120, 200);
            draw_text(&fb, 80, 80, 7, "OPENTV PORT", 245, 247, 251);
            draw_text(&fb, 80, 150, 3, "MB230 VESTEL SIDECAR - V0.1", 140, 190, 255);
            draw_text(&fb, 80, 210, 3, "CYANOGENMOD-STYLE: VENDOR BASE + OPENTV USERSPACE", 180, 220, 180);

            char line[96];
            snprintf(line, sizeof(line), "UPTIME: %ld S   FRAMES: %lu", (t - start) / 1000, ++frames);
            draw_text(&fb, 80, 300, 3, line, 230, 230, 150);
            snprintf(line, sizeof(line), "INPUT EVENTS: %lu   LAST: %s", events, last_src);
            draw_text(&fb, 80, 340, 3, line, 230, 230, 150);
            if (wd.enabled) {
                snprintf(line, sizeof(line), "WDT KICK: %s  KICKS=%lu ERR=%lu", wd.fd >= 0 ? wd.path : "NONE", wd.kicks, wd.errors);
                draw_text(&fb, 80, 380, 3, line, 255, 200, 120);
            } else {
                draw_text(&fb, 80, 380, 3, "WDT KICK: OFF (AURORA/PLATFORM MAY PROVIDE LIVENESS)", 200, 180, 120);
            }
            if (last_n > 0) {
                char hex[96] = "LAST BYTES: ";
                int off = 12;
                for (int i = 0; i < last_n && i < 8 && off < (int)sizeof(hex)-4; i++) off += snprintf(hex + off, sizeof(hex)-off, "%02X ", last_bytes[i]);
                draw_text(&fb, 80, 440, 3, hex, 180, 220, 220);
            }
            if (t - last_any < 1200) draw_text(&fb, 80, 520, 4, "INPUT RECEIVED", 80, 255, 140);
            else draw_text(&fb, 80, 520, 4, "WAITING / SIDECAR READY", 170, 170, 190);
            draw_text(&fb, 80, fb.h - 120, 2, "RUN VIA TELNET. TAKEOVER MODE MAY REBOOT IF WDT NOT SATISFIED.", 220, 150, 150);
            draw_text(&fb, 80, fb.h - 90, 2, "SEE DOCS/REBOOT_AND_LIVENESS.MD", 160, 160, 160);
        }

        fd_set rfds; FD_ZERO(&rfds);
        int maxfd = -1;
        if (ttyfd >= 0) { FD_SET(ttyfd, &rfds); if (ttyfd > maxfd) maxfd = ttyfd; }
        if (gpiofd >= 0) { FD_SET(gpiofd, &rfds); if (gpiofd > maxfd) maxfd = gpiofd; }
        struct timeval tv; tv.tv_sec = 0; tv.tv_usec = 120000;
        int sel = (maxfd >= 0) ? select(maxfd + 1, &rfds, NULL, NULL, &tv) : 0;
        if (sel == 0 && maxfd < 0) usleep(120000);
        if (sel > 0) {
            unsigned char buf[64];
            if (ttyfd >= 0 && FD_ISSET(ttyfd, &rfds)) {
                int n = read(ttyfd, buf, sizeof(buf));
                if (n > 0) {
                    events++; last_any = now_ms(); last_n = n < 16 ? n : 16; memcpy(last_bytes, buf, last_n);
                    snprintf(last_src, sizeof(last_src), "TTYS2");
                    char m[128]; snprintf(m, sizeof(m), "ttyS2 %d bytes", n); logmsg(m);
                }
            }
            if (gpiofd >= 0 && FD_ISSET(gpiofd, &rfds)) {
                int n = read(gpiofd, buf, sizeof(buf));
                if (n > 0) {
                    events++; last_any = now_ms(); last_n = n < 16 ? n : 16; memcpy(last_bytes, buf, last_n);
                    snprintf(last_src, sizeof(last_src), "GPIO");
                    char m[128]; snprintf(m, sizeof(m), "ntgpio %d bytes", n); logmsg(m);
                }
            }
        }
    }

    logmsg("stopping");
    if (ttyfd >= 0) close(ttyfd);
    if (gpiofd >= 0) close(gpiofd);
    if (fb.fb) munmap(fb.fb, fb.smem_len);
    if (fb.fd >= 0) close(fb.fd);
    wdkick_close(&wd);
    return 0;
}
