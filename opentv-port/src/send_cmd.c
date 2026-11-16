#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int main(int argc, char **argv) {
    const char *path = "/tmp/otv_port.sock";
    const char *msg = "PING";
    if (argc > 1) msg = argv[1];
    if (argc > 2) path = argv[2];
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket"); return 1; }
    struct sockaddr_un sa; memset(&sa,0,sizeof(sa));
    sa.sun_family = AF_UNIX;
    snprintf(sa.sun_path, sizeof(sa.sun_path), "%s", path);
    if (sendto(fd, msg, strlen(msg), 0, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
        perror("sendto"); close(fd); return 1;
    }
    printf("sent: %s -> %s\n", msg, path);
    close(fd);
    return 0;
}
