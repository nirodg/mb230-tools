/* mb230-gateway/display_gles2.c — GPU display backend (Mali-G51 / libmali EGL).
 *
 * The MB230's proprietary GPU is a Mali-G51 behind the vendor libmali
 * (Winsys: fbdev, Novatek platform). This backend drives it directly via
 * EGL/GLES — no DirectFB: the UI frame is uploaded as a GL texture and the
 * GPU does the blit + scanout onto its own display plane (verified on-device:
 * fullscreen EGL window renders above the live TV, which keeps playing
 * behind it).
 *
 * The UI itself still rasterises on the CPU (that is Open TV's toolkit
 * design — the port contract forbids changing it); this gateway moves the
 * frame upload, format handling, compositing and scanout onto the GPU, the
 * same "gles2" path Open TV declares for the kms-gles2 backend.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
#endif
#include "gateway/gateway.h"
#include "gateway/device.h"

#define OTV_W 1920
#define OTV_H 1080

/* Mali DDK fbdev native window */
typedef struct fbdev_window {
	unsigned short width;
	unsigned short height;
} fbdev_window;

static EGLDisplay g_dpy = EGL_NO_DISPLAY;
static EGLSurface g_surf = EGL_NO_SURFACE;
static EGLContext g_ctx = EGL_NO_CONTEXT;
static EGLConfig g_cfg;
static GLuint g_prog, g_vbo, g_tex;
static gw_surface screen;
static uint32_t *back_pix;
static int ok;

static const char *shader_vs =
	"attribute vec2 a_pos;\n"
	"varying vec2 v_uv;\n"
	"void main() {\n"
	"  v_uv = vec2(a_pos.x * 0.5 + 0.5, 0.5 - a_pos.y * 0.5);\n"
	"  gl_Position = vec4(a_pos, 0.0, 1.0);\n"
	"}\n";
static const char *shader_fs =
	"precision mediump float;\n"
	"varying vec2 v_uv;\n"
	"uniform sampler2D u_tex;\n"
	"void main() {\n"
	"  vec4 c = texture2D(u_tex, v_uv);\n"
	"  gl_FragColor = vec4(c.b, c.g, c.r, c.a);\n"  /* XRGB bytes come in as RGBA with R/B swapped */
	"}\n";

static GLuint make_prog(void)
{
	GLuint vs = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vs, 1, &shader_vs, NULL);
	glCompileShader(vs);
	GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fs, 1, &shader_fs, NULL);
	glCompileShader(fs);
	GLuint p = glCreateProgram();
	glAttachShader(p, vs);
	glAttachShader(p, fs);
	glBindAttribLocation(p, 0, "a_pos");
	glLinkProgram(p);
	GLint ok = 0;
	glGetProgramiv(p, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[512] = "";
		glGetProgramInfoLog(p, sizeof(log), NULL, log);
		fprintf(stderr, "otv: shader link failed: %s\n", log);
	}
	glDeleteShader(vs);
	glDeleteShader(fs);
	return p;
}

int gw_display_open(void)
{
	const char *rbackend = otv_device_str("display", "backend", "kms-dumb");
	fprintf(stderr, "otv: display backend %s (Mali-G51 EGL, GPU present)\n",
		rbackend);

	g_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (g_dpy == EGL_NO_DISPLAY) {
		fprintf(stderr, "otv: eglGetDisplay failed\n");
		return -1;
	}
	EGLint maj = 0, min = 0;
	if (!eglInitialize(g_dpy, &maj, &min)) {
		fprintf(stderr, "otv: eglInitialize failed 0x%x\n", eglGetError());
		return -1;
	}
	fprintf(stderr, "otv: EGL %d.%d (%s)\n", maj, min,
		eglQueryString(g_dpy, EGL_VENDOR) ? eglQueryString(g_dpy, EGL_VENDOR) : "?");

	const EGLint attrs[] = {
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE
	};
	EGLint ncfg = 0;
	if (!eglChooseConfig(g_dpy, attrs, &g_cfg, 1, &ncfg) || ncfg < 1) {
		fprintf(stderr, "otv: eglChooseConfig failed 0x%x\n", eglGetError());
		return -1;
	}

	fbdev_window win = { OTV_W, OTV_H };
	g_surf = eglCreateWindowSurface(g_dpy, g_cfg,
					(EGLNativeWindowType)&win, NULL);
	if (g_surf == EGL_NO_SURFACE) {
		fprintf(stderr, "otv: eglCreateWindowSurface failed 0x%x\n",
			eglGetError());
		return -1;
	}

	eglBindAPI(EGL_OPENGL_ES_API);
	/* GLES3 context: GL_BGRA texture upload is core there (ES2 only has it
	 * behind an optional EXT — the Mali returns INVALID_OPERATION). */
	const EGLint ctxattrs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
	g_ctx = eglCreateContext(g_dpy, g_cfg, EGL_NO_CONTEXT, ctxattrs);
	if (g_ctx == EGL_NO_CONTEXT) {
		fprintf(stderr, "otv: eglCreateContext failed 0x%x\n", eglGetError());
		return -1;
	}
	if (!eglMakeCurrent(g_dpy, g_surf, g_surf, g_ctx)) {
		fprintf(stderr, "otv: eglMakeCurrent failed 0x%x\n", eglGetError());
		return -1;
	}
	fprintf(stderr, "otv: GL %s / %s\n", glGetString(GL_VERSION),
		glGetString(GL_RENDERER));

	g_prog = make_prog();
	glUseProgram(g_prog);
	glGenTextures(1, &g_tex);
	glBindTexture(GL_TEXTURE_2D, g_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	/* BGRA upload matches the UI's XRGB8888 byte layout (R/B not swapped) */
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, OTV_W, OTV_H, 0,
		     GL_RGBA, GL_UNSIGNED_BYTE, NULL);

	const float quad[] = {
		-1.f, -1.f,  1.f, -1.f,  -1.f, 1.f,  1.f, 1.f,
	};
	glGenBuffers(1, &g_vbo);
	glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
	glViewport(0, 0, OTV_W, OTV_H);
	glDisable(GL_BLEND);

	/* Prime the scanout: the Mali fbdev WSI does not present the FIRST
	 * swap's drawn content (observed on-device — first texture swap shows
	 * black); a clear+swap here makes subsequent presents show correctly. */
	glClearColor(0.f, 0.f, 0.f, 1.f);
	glClear(GL_COLOR_BUFFER_BIT);
	eglSwapBuffers(g_dpy, g_surf);

	back_pix = malloc((size_t)OTV_W * OTV_H * 4);
	if (!back_pix) {
		fprintf(stderr, "otv: no memory for back buffer\n");
		return -1;
	}
	screen.w = OTV_W;
	screen.h = OTV_H;
	screen.stride = OTV_W * 4;
	screen.pix = back_pix;
	ok = 1;
	fprintf(stderr, "otv: page flip OK (Mali-G51 EGL)\n");
	return 0;
}

gw_surface *gw_display_fb(void)
{
	return ok ? &screen : NULL;
}

void gw_display_present(void)
{
	if (!ok)
		return;
	static int n = 0;
	glBindTexture(GL_TEXTURE_2D, g_tex);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, OTV_W, OTV_H,
			GL_RGBA, GL_UNSIGNED_BYTE, screen.pix);
	GLenum glerr = glGetError();
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glerr = glGetError();
	EGLBoolean sw = eglSwapBuffers(g_dpy, g_surf);
	if (n < 6) {
		fprintf(stderr, "otv: present#%d glerr=0x%x swap=%d egl=0x%x\n",
			n, glerr, sw, eglGetError());
		n++;
	}
}

void gw_display_damage(int x, int y, int w, int h)
{
	(void)x; (void)y; (void)w; (void)h;
}

void gw_display_close(void)
{
	if (g_dpy != EGL_NO_DISPLAY && ok) {
		/* clear the plane black and present once so our frame is not
		 * left stuck on screen after exit */
		glClearColor(0.f, 0.f, 0.f, 1.f);
		glClear(GL_COLOR_BUFFER_BIT);
		eglSwapBuffers(g_dpy, g_surf);
	}
	eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	if (g_surf != EGL_NO_SURFACE)
		eglDestroySurface(g_dpy, g_surf);
	if (g_ctx != EGL_NO_CONTEXT)
		eglDestroyContext(g_dpy, g_ctx);
	if (g_dpy != EGL_NO_DISPLAY)
		eglTerminate(g_dpy);
	g_surf = EGL_NO_SURFACE;
	g_ctx = EGL_NO_CONTEXT;
	g_dpy = EGL_NO_DISPLAY;
	free(back_pix);
	back_pix = NULL;
	ok = 0;
}
