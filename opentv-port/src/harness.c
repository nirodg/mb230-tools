/* harness: replicate the aurora forwarder's exact logic (registers/stack) standalone,
 * to verify socket+sendto+close sequence is safe before live-patching. */
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdlib.h>

/* mimic forwarder as a normal C function, preserving r0..r4 semantics (approx) */
__attribute__((noinline)) static int fwd(unsigned int key) {
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket"); return -1; }
    unsigned char msg[5] = {'K', key&0xff, (key>>8)&0xff, (key>>16)&0xff, (key>>24)&0xff};
    struct sockaddr_un sa; memset(&sa,0,sizeof(sa)); sa.sun_family = AF_UNIX;
    snprintf(sa.sun_path, sizeof(sa.sun_path), "/tmp/otv_port.sock");
    if (sendto(fd, msg, 5, 0, (struct sockaddr*)&sa, sizeof(sa)) < 0) { perror("sendto"); close(fd); return -1; }
    close(fd);
    return 0;
}

int main(int argc, char **argv) {
    unsigned int key = (argc > 1) ? (unsigned int)strtoul(argv[1], 0, 0) : 0x21f;
    printf("harness: forwarding key 0x%x\n", key);
    int r = fwd(key);
    printf("harness: fwd rc=%d\n", r);
    return r;
}
