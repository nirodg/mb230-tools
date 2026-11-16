/* probe_egl3.c — isolates where the texture path breaks the scanout.
 * Phase A: plain glClear(red) + swap   (known to show)
 * Phase B: textured quad draw + swap   (the launcher's path)
 * Phase C: plain glClear(green) + swap (known to show)
 * The user reports what each phase shows on screen.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

typedef struct fbdev_window {
	unsigned short width;
	unsigned short height;
} fbdev_window;

#define W 1920
#define H 1080

static const char *vs =
	"attribute vec2 a_pos;\n"
	"varying vec2 v_uv;\n"
	"void main() {\n"
	"  v_uv = a_pos * 0.5 + 0.5;\n"
	"  gl_Position = vec4(a_pos, 0.0, 1.0);\n"
	"}\n";
static const char *fs =
	"precision mediump float;\n"
	"varying vec2 v_uv;\n"
	"uniform sampler2D u_tex;\n"
	"void main() {\n"
	"  vec4 c = texture2D(u_tex, v_uv);\n"
	"  gl_FragColor = vec4(c.b, c.g, c.r, c.a);\n"
	"}\n";

int main(void)
{
	uint32_t *pix = malloc((size_t)W * H * 4);
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
			/* bytes: left half pure BLUE, right half pure RED */
			pix[y * W + x] = (x < W / 2) ? 0xFF0000FFu : 0xFFFF0000u;

	EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	eglInitialize(dpy, NULL, NULL);
	const EGLint attrs[] = {
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE
	};
	EGLConfig cfg;
	EGLint n = 0;
	eglChooseConfig(dpy, attrs, &cfg, 1, &n);
	fbdev_window win = { W, H };
	EGLSurface surf = eglCreateWindowSurface(dpy, cfg,
						 (EGLNativeWindowType)&win, NULL);
	eglBindAPI(EGL_OPENGL_ES_API);
	const EGLint ca[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
	EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ca);
	eglMakeCurrent(dpy, surf, surf, ctx);

	/* Phase A: clear red + swap */
	glClearColor(1, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	eglSwapBuffers(dpy, surf);
	printf("[p3] PHASE A: red clear swapped — look at screen\n");
	fflush(stdout);
	sleep(5);

	/* Phase B: textured quad + swap */
	GLuint vsid = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vsid, 1, &vs, NULL);
	glCompileShader(vsid);
	GLuint fsid = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fsid, 1, &fs, NULL);
	glCompileShader(fsid);
	GLuint prog = glCreateProgram();
	glAttachShader(prog, vsid);
	glAttachShader(prog, fsid);
	glBindAttribLocation(prog, 0, "a_pos");
	glLinkProgram(prog);
	glUseProgram(prog);

	GLuint tex;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0,
		     GL_RGBA, GL_UNSIGNED_BYTE, pix);

	const float quad[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
	GLuint vbo;
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
	glViewport(0, 0, W, H);

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	eglSwapBuffers(dpy, surf);
	printf("[p3] PHASE B: texture draw swapped — look at screen\n");
	fflush(stdout);
	sleep(5);

	/* Phase C: clear green + swap */
	glClearColor(0, 1, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	eglSwapBuffers(dpy, surf);
	printf("[p3] PHASE C: green clear swapped — look at screen\n");
	fflush(stdout);
	sleep(5);

	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	eglSwapBuffers(dpy, surf);
	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroySurface(dpy, surf);
	eglDestroyContext(dpy, ctx);
	eglTerminate(dpy);
	printf("[p3] clean exit\n");
	return 0;
}
