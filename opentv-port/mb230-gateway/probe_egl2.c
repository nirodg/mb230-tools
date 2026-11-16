/* probe_egl2.c — replicates the launcher's GPU path exactly:
 * texture upload (BGRA) + shader quad + swap, with a distinctive pattern
 * (left half red, right half blue) so visibility is unambiguous.
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
			/* BGRA bytes: left red, right blue */
			pix[y * W + x] = (x < W / 2) ? 0xFF0000FFu : 0xFFFF0000u;

	EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (!eglInitialize(dpy, NULL, NULL)) {
		printf("[p2] init failed 0x%x\n", eglGetError());
		return 1;
	}
	const EGLint attrs[] = {
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE
	};
	EGLConfig cfg;
	EGLint n = 0;
	if (!eglChooseConfig(dpy, attrs, &cfg, 1, &n)) {
		printf("[p2] config failed 0x%x\n", eglGetError());
		return 1;
	}
	fbdev_window win = { W, H };
	EGLSurface surf = eglCreateWindowSurface(dpy, cfg,
						 (EGLNativeWindowType)&win, NULL);
	if (surf == EGL_NO_SURFACE) {
		printf("[p2] surface failed 0x%x\n", eglGetError());
		return 1;
	}
	eglBindAPI(EGL_OPENGL_ES_API);
	const EGLint ca[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
	EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ca);
	eglMakeCurrent(dpy, surf, surf, ctx);

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
	GLint lk = 0;
	glGetProgramiv(prog, GL_LINK_STATUS, &lk);
	printf("[p2] link=%d\n", lk);
	glUseProgram(prog);

	GLuint tex;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0,
		     GL_RGBA, GL_UNSIGNED_BYTE, pix);
	printf("[p2] upload glerr=0x%x\n", glGetError());

	const float quad[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
	GLuint vbo;
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
	glViewport(0, 0, W, H);
	glDisable(GL_BLEND);

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	printf("[p2] draw glerr=0x%x\n", glGetError());
	{
		unsigned char px[4] = {0,0,0,0};
		unsigned char px2[4] = {0,0,0,0};
		glReadPixels(W/4, H/2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
		glReadPixels(W*3/4, H/2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px2);
		printf("[p2] leftpx r=%d g=%d b=%d a=%d | rightpx r=%d g=%d b=%d a=%d\n",
		       px[0], px[1], px[2], px[3], px2[0], px2[1], px2[2], px2[3]);
	}
	printf("[p2] swap=%d egl=0x%x\n", eglSwapBuffers(dpy, surf), eglGetError());
	fflush(stdout);
	sleep(12);

	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	eglSwapBuffers(dpy, surf);
	printf("[p2] black swap done\n");
	fflush(stdout);
	sleep(2);

	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroySurface(dpy, surf);
	eglDestroyContext(dpy, ctx);
	eglTerminate(dpy);
	printf("[p2] clean exit\n");
	return 0;
}
