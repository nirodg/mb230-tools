/* mb230-gateway/display_dfb.c — DirectFB display backend for the Vestel 17MB230.
 *
 * The MB230 has no DRM/KMS: display is owned by the vendor DirectFB 1.7
 * (fusion + sawman) session that aurora.elf runs in. This backend JOINS that
 * session (sidecar model — aurora stays alive for liveness) and renders a
 * fullscreen UPPER-stacking sawman window, uploading the UI's XRGB8888
 * software frame with IDirectFBSurface::Write() + Flip().
 *
 * DirectFB interface vtable offsets (validated against otv_port's proven
 * usage and the DirectFB 1.7.1 public interface declaration, +5 base):
 *   surface: GetSize=7  GetPixelFormat=9  Flip=20  Write=65  Release=4
 *   window:  Release=4  GetSurface=10  SetOpacity=22  RaiseToTop=33
 *   layer:   CreateWindow=33
 *   dfb:     GetDisplayLayer=14
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "gateway/gateway.h"
#include "gateway/device.h"

#define OTV_W 1920
#define OTV_H 1080

typedef int DFBResult;

typedef DFBResult (*DFBInitFn)(int *argc, char **argv[]);
typedef DFBResult (*DFBCreateFn)(void **interface);
typedef DFBResult (*GetLayerFn)(void *, unsigned int, void **);
typedef DFBResult (*CreateWindowFn)(void *, void *, void **);
typedef DFBResult (*WinGetSurfaceFn)(void *, void **);
typedef DFBResult (*WinSetOpacityFn)(void *, unsigned char);
typedef DFBResult (*WinRaiseFn)(void *);
typedef DFBResult (*WinReleaseFn)(void *);
typedef DFBResult (*SurfGetSizeFn)(void *, int *, int *);
typedef DFBResult (*SurfLockFn)(void *, unsigned int, void **, int *);
typedef DFBResult (*SurfUnlockFn)(void *);
typedef DFBResult (*SurfFlipFn)(void *, void *, unsigned int);
typedef DFBResult (*SurfGetPixelFormatFn)(void *, unsigned int *);
typedef DFBResult (*SurfReleaseFn)(void *);

static void *g_lib;
static void *g_dfb;
static void *g_layer;
static void *g_win;
static void *g_surf;
static SurfGetSizeFn g_get_size;
static SurfLockFn g_lock;
static SurfUnlockFn g_unlock;
static SurfFlipFn g_flip;
static SurfGetPixelFormatFn g_get_pf;
static SurfReleaseFn g_surf_release;
static WinReleaseFn g_win_release;
static WinSetOpacityFn g_set_opacity;
static WinRaiseFn g_raise;

static gw_surface screen;
static uint32_t *back_pix;
static int ok;

int gw_display_open(void)
{
	const char *rbackend = otv_device_str("display", "backend", "kms-dumb");
	fprintf(stderr, "otv: display backend %s (DirectFB sawman, sidecar)\n",
		rbackend);

	/* join the running vendor DirectFB session */
	const char *libpaths[] = {
		"/vendor/lib/dfb/libdirectfb-1.7.so.1",
		"/mtd_apexe/board/lib/dfb/libdirectfb-1.7.so.1",
		"libdirectfb-1.7.so.1",
		NULL,
	};
	for (int i = 0; libpaths[i]; i++) {
		g_lib = dlopen(libpaths[i], RTLD_NOW | RTLD_GLOBAL);
		if (g_lib) {
			fprintf(stderr, "otv: dlopen %s\n", libpaths[i]);
			break;
		}
	}
	if (!g_lib) {
		fprintf(stderr, "otv: cannot dlopen libdirectfb: %s\n", dlerror());
		return -1;
	}

	DFBInitFn pInit = (DFBInitFn)dlsym(g_lib, "DirectFBInit");
	DFBCreateFn pCreate = (DFBCreateFn)dlsym(g_lib, "DirectFBCreate");
	if (!pInit || !pCreate) {
		fprintf(stderr, "otv: missing DirectFBInit/DirectFBCreate\n");
		return -1;
	}

	/* join mp session; no-sighandler avoids DirectFB's fatal signal trapping
	 * which destabilises the shared fusion session (otv_port lesson) */
	setenv("DFBARGS",
	       "system=fbdev,wm=sawman,no-cursor,no-sighandler,"
	       "module-dir=/vendor/lib/dfb/directfb-1.7-1", 0);

	int ac = 1;
	char *avv[] = { "otv-launcher", NULL };
	char **av = avv;
	DFBResult r = pInit(&ac, &av);
	if (r) {
		fprintf(stderr, "otv: DirectFBInit failed %d\n", r);
		return -1;
	}
	r = pCreate(&g_dfb);
	if (r || !g_dfb) {
		fprintf(stderr, "otv: DirectFBCreate failed %d\n", r);
		return -1;
	}

	GetLayerFn getLayer = (GetLayerFn)(((unsigned long *)g_dfb)[14]);
	r = getLayer ? getLayer(g_dfb, 0, &g_layer) : -1;
	if (r || !g_layer) {
		fprintf(stderr, "otv: GetDisplayLayer(0) failed %d\n", r);
		return -1;
	}

	/* fullscreen UPPER-stacking window (proven layout from otv_port:
	 * flags=d[0], caps=d[1], w=d[2], h=d[3], pf=d[4], x=d[5], y=d[6],
	 * scaps=d[7], stacking=d[10]; flag bits 0,1,2,3,4,5,6,9) */
	CreateWindowFn createWindow = (CreateWindowFn)(((unsigned long *)g_layer)[33]);
	if (!createWindow) {
		fprintf(stderr, "otv: layer CreateWindow unavailable\n");
		return -1;
	}
	const unsigned int stackings[] = { 2u, 1u };
	const unsigned int capsvals[] = { 0x1u, 0x11u, 0x2u };
	int made = 0;
	for (int si = 0; si < 2 && !made; si++) {
		for (int ci = 0; ci < 3 && !made; ci++) {
			uint32_t desc[64];
			memset(desc, 0, sizeof(desc));
			unsigned int flags = (1u << 0) | (1u << 1) | (1u << 2) |
					     (1u << 3) | (1u << 4) | (1u << 5) |
					     (1u << 6) | (1u << 9);
			desc[0] = flags;
			desc[1] = capsvals[ci];
			desc[2] = OTV_W;
			desc[3] = OTV_H;
			desc[4] = 0; /* pixelformat */
			desc[5] = 0; /* posx */
			desc[6] = 0; /* posy */
			desc[7] = 0; /* surface_caps */
			desc[10] = stackings[si];
			r = createWindow(g_layer, desc, &g_win);
			if (r || !g_win) {
				fprintf(stderr,
					"otv: CreateWindow caps=0x%x stack=%u failed %d\n",
					desc[1], desc[10], r);
				continue;
			}
			made = 1;
		}
	}
	if (!made) {
		fprintf(stderr, "otv: no window created\n");
		return -1;
	}

	unsigned long *wv = (unsigned long *)g_win;
	WinGetSurfaceFn getSurf = (WinGetSurfaceFn)wv[10];
	g_set_opacity = (WinSetOpacityFn)wv[22];
	g_raise = (WinRaiseFn)wv[33];
	g_win_release = (WinReleaseFn)wv[4];
	r = getSurf ? getSurf(g_win, &g_surf) : -1;
	if (r || !g_surf) {
		fprintf(stderr, "otv: window GetSurface failed %d\n", r);
		return -1;
	}

	unsigned long *sv = (unsigned long *)g_surf;
	g_get_size = (SurfGetSizeFn)sv[7];
	g_get_pf = (SurfGetPixelFormatFn)sv[9];
	g_lock = (SurfLockFn)sv[17];
	g_unlock = (SurfUnlockFn)sv[19];
	g_flip = (SurfFlipFn)sv[20];
	g_surf_release = (SurfReleaseFn)sv[4];

	if (g_set_opacity)
		g_set_opacity(g_win, 255);
	if (g_raise)
		g_raise(g_win);

	int w = OTV_W, h = OTV_H;
	if (g_get_size)
		g_get_size(g_surf, &w, &h);
	unsigned int pf = 0;
	if (g_get_pf)
		g_get_pf(g_surf, &pf);
	fprintf(stderr, "otv: DirectFB window %dx%d pixelformat 0x%x\n", w, h, pf);
	/* the UI writes XRGB8888 with 0xFF in the top byte: byte-compatible with
	 * DSPF_RGB32 (XRGB) and DSPF_ARGB (alpha=0xFF) alike */

	back_pix = malloc((size_t)OTV_W * OTV_H * 4);
	if (!back_pix) {
		fprintf(stderr, "otv: no memory for the back buffer\n");
		return -1;
	}
	screen.w = OTV_W;
	screen.h = OTV_H;
	screen.stride = OTV_W * 4;
	screen.pix = back_pix;
	ok = 1;
	fprintf(stderr, "otv: page flip OK (DirectFB)\n");
	return 0;
}

gw_surface *gw_display_fb(void)
{
	return ok ? &screen : NULL;
}

void gw_display_present(void)
{
	if (!ok || !g_surf)
		return;
	static int n = 0;
	void *ptr = NULL;
	int pitch = 0;
	DFBResult lr = -999, ur = -999, fr = -999;
	if (g_lock)
		lr = g_lock(g_surf, 2 /* DSLF_WRITE */, &ptr, &pitch);
	if (lr == 0 && ptr && pitch > 0) {
		/* fast path: the surface pitch equals our stride (XRGB8888 rows) */
		if (pitch == screen.stride) {
			memcpy(ptr, screen.pix, (size_t)OTV_W * OTV_H * 4);
		} else {
			uint8_t *dst = (uint8_t *)ptr;
			uint8_t *src = (uint8_t *)screen.pix;
			int row = OTV_W * 4;
			if (pitch < row)
				row = pitch;
			for (int y = 0; y < OTV_H; y++)
				memcpy(dst + (size_t)y * pitch,
				       src + (size_t)y * screen.stride, (size_t)row);
		}
		if (g_unlock)
			ur = g_unlock(g_surf);
	}
	if (g_flip)
		fr = g_flip(g_surf, NULL, 0);
	if (n < 6) {
		fprintf(stderr, "otv: present#%d Lock rc=%d pitch=%d Unlock rc=%d Flip rc=%d\n",
			n, lr, pitch, ur, fr);
		n++;
	}
}

void gw_display_damage(int x, int y, int w, int h)
{
	(void)x; (void)y; (void)w; (void)h;
}

void gw_display_close(void)
{
	if (g_surf && g_surf_release)
		g_surf_release(g_surf);
	g_surf = NULL;
	if (g_win && g_win_release)
		g_win_release(g_win);
	g_win = NULL;
	free(back_pix);
	back_pix = NULL;
	ok = 0;
}
