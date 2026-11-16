/* send_key: send binary IR key ('K' + 4-byte LE key) to otv_port control socket */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
int main(int argc, char **argv) {
    const char *path = "/tmp/otv_port.sock";
    unsigned long key = 0x21f;
    if (argc > 1) key = strtoul(argv[1], NULL, 0);
    if (argc > 2) path = argv[2];
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket"); return 1; }
    struct sockaddr_un sa; memset(&sa,0,sizeof(sa)); sa.sun_family = AF_UNIX;
    snprintf(sa.sun_path, sizeof(sa.sun_path), "%s", path);
    unsigned char buf[5] = {'K', key&0xff, (key>>8)&0xff, (key>>16)&0xff, (key>>24)&0xff};
    if (sendto(fd, buf, 5, 0, (struct sockaddr*)&sa, sizeof(sa)) < 0) { perror("sendto"); close(fd); return 1; }
    printf("sent KEY 0x%lx -> %s\n", key, path);
    close(fd); return 0;
}
