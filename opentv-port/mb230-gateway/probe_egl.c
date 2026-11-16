/* probe_egl.c — EGL/GLES probe for the MB230's vendor Mali driver.
 *
 * Discovers how the vendor libmali (winsys=fbdev, platform=nvt) wants its
 * window surfaces: display init, config, fbdev_window {w,h}, context, one
 * colored swap. Every step logs to stdout. Run on the TV; observe the
 * screen and the log to learn the GPU init sequence for the gateway.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

/* Mali fbdev native window (the DDK's fbdev_window.h) */
typedef struct fbdev_window {
	unsigned short width;
	unsigned short height;
} fbdev_window;

static const char *err_name(EGLint e)
{
	switch (e) {
	case EGL_SUCCESS: return "SUCCESS";
	case EGL_NOT_INITIALIZED: return "NOT_INITIALIZED";
	case EGL_BAD_ACCESS: return "BAD_ACCESS";
	case EGL_BAD_ALLOC: return "BAD_ALLOC";
	case EGL_BAD_ATTRIBUTE: return "BAD_ATTRIBUTE";
	case EGL_BAD_CONFIG: return "BAD_CONFIG";
	case EGL_BAD_CONTEXT: return "BAD_CONTEXT";
	case EGL_BAD_CURRENT_SURFACE: return "BAD_CURRENT_SURFACE";
	case EGL_BAD_DISPLAY: return "BAD_DISPLAY";
	case EGL_BAD_MATCH: return "BAD_MATCH";
	case EGL_BAD_NATIVE_PIXMAP: return "BAD_NATIVE_PIXMAP";
	case EGL_BAD_NATIVE_WINDOW: return "BAD_NATIVE_WINDOW";
	case EGL_BAD_PARAMETER: return "BAD_PARAMETER";
	case EGL_BAD_SURFACE: return "BAD_SURFACE";
	case EGL_CONTEXT_LOST: return "CONTEXT_LOST";
	default: return "?";
	}
}

static void chk(const char *what)
{
	EGLint e = eglGetError();
	if (e != EGL_SUCCESS)
		printf("[egl] %s: %s (0x%x)\n", what, err_name(e), e);
	else
		printf("[egl] %s: ok\n", what);
}

int main(int argc, char **argv)
{
	int w = argc > 1 ? atoi(argv[1]) : 1920;
	int h = argc > 2 ? atoi(argv[2]) : 1080;

	printf("[egl] probe start (window %dx%d)\n", w, h);

	EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	printf("[egl] GetDisplay: %p\n", (void *)dpy);
	if (dpy == EGL_NO_DISPLAY)
		return 1;

	EGLint maj = 0, min = 0;
	if (!eglInitialize(dpy, &maj, &min)) {
		chk("Initialize");
		return 1;
	}
	printf("[egl] Initialize ok: %d.%d  vendor=%s\n", maj, min,
	       eglQueryString(dpy, EGL_VENDOR) ? eglQueryString(dpy, EGL_VENDOR) : "?");

	const EGLint attrs[] = {
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE
	};
	EGLConfig cfg;
	EGLint ncfg = 0;
	if (!eglChooseConfig(dpy, attrs, &cfg, 1, &ncfg) || ncfg < 1) {
		chk("ChooseConfig RGBA8888");
		/* fallback 565 */
		const EGLint a16[] = {
			EGL_RED_SIZE, 5, EGL_GREEN_SIZE, 6, EGL_BLUE_SIZE, 5,
			EGL_ALPHA_SIZE, 0,
			EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
			EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
			EGL_NONE
		};
		if (!eglChooseConfig(dpy, a16, &cfg, 1, &ncfg) || ncfg < 1) {
			chk("ChooseConfig RGB565");
			return 1;
		}
		printf("[egl] using RGB565 config\n");
	} else {
		printf("[egl] using RGBA8888 config\n");
	}

	fbdev_window win = { (unsigned short)w, (unsigned short)h };
	EGLSurface surf = eglCreateWindowSurface(dpy, cfg,
						 (EGLNativeWindowType)&win, NULL);
	printf("[egl] CreateWindowSurface: %p\n", (void *)surf);
	if (surf == EGL_NO_SURFACE) {
		chk("CreateWindowSurface");
		return 1;
	}

	eglBindAPI(EGL_OPENGL_ES_API);
	const EGLint ctxattrs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxattrs);
	printf("[egl] CreateContext: %p\n", (void *)ctx);
	if (ctx == EGL_NO_CONTEXT) {
		chk("CreateContext");
		return 1;
	}

	if (!eglMakeCurrent(dpy, surf, surf, ctx)) {
		chk("MakeCurrent");
		return 1;
	}
	printf("[egl] MakeCurrent ok — GL_VERSION=%s GL_RENDERER=%s\n",
	       (const char *)glGetString(GL_VERSION),
	       (const char *)glGetString(GL_RENDERER));

	glViewport(0, 0, w, h);
	glClearColor(0.9f, 0.1f, 0.1f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	printf("[egl] cleared red, swapping...\n");
	eglSwapBuffers(dpy, surf);
	chk("SwapBuffers");

	sleep(6);

	glClearColor(0.1f, 0.9f, 0.1f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	eglSwapBuffers(dpy, surf);
	printf("[egl] second swap (green) done\n");
	sleep(6);

	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroySurface(dpy, surf);
	eglDestroyContext(dpy, ctx);
	eglTerminate(dpy);
	printf("[egl] probe done, clean exit\n");
	return 0;
}
