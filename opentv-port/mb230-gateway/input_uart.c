/* mb230-gateway/input_uart.c — UART-IR input backend for the Vestel 17MB230.
 *
 * The MB230 remote is decoded by aurora.elf (userspace UART IR, no evdev).
 * This backend receives the decoded keys that the in-memory mempatch hook
 * forwards over a unix datagram socket ('K' + 4-byte LE key id), maps them
 * to the gateway key model, and applies/restores the block patch around
 * the launcher's lifetime so aurora stays alive (liveness) but does not act
 * on non-allowlisted keys.
 *
 * Key ids from ice_keymapping_db.xml: Up=522 Down=523 Left=524 Right=525
 * Select=526 Exit=528 Menu=557 MediaBrowser=591 VolumeUp=544 VolumeDown=545
 * Mute=546 ProgramUp=547 ProgramDown=548. Release = press + 2000.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include "gateway/gateway.h"

#define OTV_W 1920
#define OTV_H 1080
#define SOCK_PATH "/tmp/otv_port.sock"
#define MEMPATCH "/conf/opentv-port/mempatch"
#define EXIT_HOLD_MS 700   /* long-press Exit = quit the launcher to aurora */

static int sfd = -1;
static long g_exit_press_ms = 0;   /* 528 press timestamp (hold detect) */

static long now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void restore_patch(void)
{
	if (access(MEMPATCH, X_OK) == 0) {
		char cmd[160];
		snprintf(cmd, sizeof(cmd),
			 "%s $(pidof aurora.elf) restore >/dev/null 2>&1", MEMPATCH);
		system(cmd);
	}
}

/* full teardown: drop the DirectFB window (unfreeze the screen) and restore
 * aurora's input before the process dies */
static void cleanup_all(void)
{
	restore_patch();
	gw_display_close();
}

static void on_term(int s)
{
	(void)s;
	fprintf(stderr, "otv: signal %d — restoring aurora input\n", s);
	cleanup_all();
	_exit(0);
}

static gw_key ice_to_gwk(int k)
{
	switch (k) {
	case 522: return GWK_UP;
	case 523: return GWK_DOWN;
	case 524: return GWK_LEFT;
	case 525: return GWK_RIGHT;
	case 526: return GWK_OK;
	case 528: return GWK_BACK;
	case 557: return GWK_MENU;
	case 591: return GWK_HOME;
	case 544: return GWK_VOLUP;
	case 545: return GWK_VOLDOWN;
	case 546: return GWK_MUTE;
	case 547: return GWK_CHUP;
	case 548: return GWK_CHDOWN;
	default:  return GWK_NONE;
	}
}

int gw_input_open(void)
{
	unlink(SOCK_PATH);
	sfd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (sfd < 0) {
		fprintf(stderr, "otv: no input socket\n");
		return -1;
	}
	struct sockaddr_un sa;
	memset(&sa, 0, sizeof(sa));
	sa.sun_family = AF_UNIX;
	snprintf(sa.sun_path, sizeof(sa.sun_path), "%s", SOCK_PATH);
	if (bind(sfd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
		fprintf(stderr, "otv: bind %s failed: %s\n", SOCK_PATH, strerror(errno));
		close(sfd);
		sfd = -1;
		return -1;
	}
	/* block aurora's keys and forward them here (in-memory patch, volatile;
	 * restored in gw_input_close() or by the signal/atexit handlers) */
	if (access(MEMPATCH, X_OK) == 0) {
		char cmd[160];
		snprintf(cmd, sizeof(cmd),
			 "%s $(pidof aurora.elf) suppress >/dev/null 2>&1", MEMPATCH);
		if (system(cmd) == 0)
			fprintf(stderr, "otv: input uart (aurora keys blocked)\n");
	} else {
		fprintf(stderr, "otv: input uart (no mempatch; tap not applied)\n");
	}
	/* any exit path restores aurora's input (SIGKILL is the only gap) */
	signal(SIGTERM, on_term);
	signal(SIGINT, on_term);
	signal(SIGHUP, on_term);
	atexit(cleanup_all);
	return 0;
}

void gw_input_close(void)
{
	if (sfd >= 0) {
		close(sfd);
		unlink(SOCK_PATH);
		sfd = -1;
	}
	if (access(MEMPATCH, X_OK) == 0) {
		char cmd[160];
		snprintf(cmd, sizeof(cmd),
			 "%s $(pidof aurora.elf) restore >/dev/null 2>&1", MEMPATCH);
		system(cmd);
	}
}

int gw_input_poll(gw_input_event *ev, int max, int timeout_ms)
{
	if (sfd < 0 || max <= 0)
		return 0;
	struct pollfd pfd = { .fd = sfd, .events = POLLIN };
	int pr = poll(&pfd, 1, timeout_ms);
	if (pr <= 0)
		return 0;
	int n = 0;
	while (n < max) {
		unsigned char buf[64];
		ssize_t r = recvfrom(sfd, buf, sizeof(buf), MSG_DONTWAIT, NULL, NULL);
		if (r < 0)
			break;
		if (r == 5 && buf[0] == 0x4b) { /* 'K' + 4-byte LE key */
			unsigned long k = (unsigned char)buf[1] |
				((unsigned long)(unsigned char)buf[2] << 8) |
				((unsigned long)(unsigned char)buf[3] << 16) |
				((unsigned long)(unsigned char)buf[4] << 24);
			int code, down;
			if (k >= 2000) {
				down = 0;
				code = (int)(k - 2000);
			} else {
				down = 1;
				code = (int)k;
			}
			/* Exit (528): short press = Back; hold >= EXIT_HOLD_MS =
			 * quit the launcher and hand back to stock aurora. */
			if (code == 528) {
				if (down) {
					g_exit_press_ms = now_ms();
				} else if (g_exit_press_ms &&
					   now_ms() - g_exit_press_ms >= EXIT_HOLD_MS) {
					fprintf(stderr,
						"otv: exit hold — restoring aurora input\n");
					cleanup_all();
					exit(0);
				}
			}
			gw_key key = ice_to_gwk(code);
			if (key != GWK_NONE) {
				ev[n].key = key;
				ev[n].down = down;
				ev[n].pointer = false;
				ev[n].right = false;
				ev[n].x = 0;
				ev[n].y = 0;
				n++;
			}
		}
	}
	return n;
}

void gw_input_screen_size(int *w, int *h)
{
	*w = OTV_W;
	*h = OTV_H;
}
