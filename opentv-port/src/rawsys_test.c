/* validate raw-syscall socket/sendto/close path standalone (same as forwarder fwd7) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>

int main(int argc, char **argv) {
    unsigned int key = (argc > 1) ? (unsigned int)strtoul(argv[1], 0, 0) : 0x21f;
    /* sockaddr_un: AF_UNIX=1, path */
    struct { unsigned short family; char path[108]; } sa;
    memset(&sa, 0, sizeof(sa));
    sa.family = 1;
    strcpy(sa.path, "/tmp/otv_port.sock");
    int fd = syscall(SYS_socket, 1, 2, 0);
    printf("fd=%d\n", fd);
    if (fd < 0) { perror("socket"); return 1; }
    unsigned char msg[5] = {'K', key&0xff, (key>>8)&0xff, (key>>16)&0xff, (key>>24)&0xff};
    long r = syscall(SYS_sendto, fd, msg, 5, 0, &sa, 20);
    printf("sendto=%ld\n", r);
    syscall(SYS_close, fd);
    return r < 0 ? 1 : 0;
}
