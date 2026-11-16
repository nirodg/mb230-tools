/* mb230-gateway/media_stub.c — placeholder media backend for the MB230 port.
 *
 * R1–R3 build without FFmpeg; the Video app shows the library but playback
 * is disabled until R4 wires the vendor's Opera ffmpeg 4.0
 * (/lib/opera/libavcodec.so.58 etc.). This stub satisfies media.h so the
 * launcher links and runs; it is a BUILD-TIME swap (the upstream ui/media.c
 * is simply not compiled in this port's Makefile), upstream stays untouched.
 */
#include "media.h"

double media_probe_duration(const char *path)
{
	(void)path;
	return 0;
}

Media *media_open(const char *path)
{
	(void)path;
	return NULL;
}

void media_close(Media *m)
{
	(void)m;
}

const uint32_t *media_frame(Media *m, double ts)
{
	(void)m;
	(void)ts;
	return NULL;
}

int media_w(const Media *m)
{
	(void)m;
	return 0;
}

int media_h(const Media *m)
{
	(void)m;
	return 0;
}
