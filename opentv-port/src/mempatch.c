/* mempatch: apply/restore the aurora IR-forward patch to the running process via ptrace.
 * Usage: mempatch <pid> tap      (forward keys to otv_port, aurora still handles them)
 *        mempatch <pid> suppress (forward keys; swallow non-passthrough keys so aurora ignores them)
 *        mempatch <pid> apply    (alias for tap)
 *        mempatch <pid> restore  (restores original bytes, saved to /tmp/irpatch.orig)
 * Passthrough allowlist in suppress mode (forward + let aurora handle): KeyPower/suspend 543 (0x21F), KeySleep 574 (0x23E).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

#define HOOK_VA 0x761cf8
#define CAVE_VA 0x690100
#define HOOK_LEN 4
#define CAVE_LEN 121
#define CAVE_SUPPRESS_LEN 148
#define BACKUP_LEN 152  /* max(HOOK_LEN+CAVE_LEN, HOOK_LEN+CAVE_SUPPRESS_LEN) */

static const unsigned char hook_patch[4] = {0x2e,0xf7,0x02,0xba};
/* tap: forward to /tmp/otv_port.sock, then resume aurora handler (b.w 0x761cfc). */
static const unsigned char cave_tap[121] = {
0xf8,0xb5,0x86,0xb0,0x04,0x90,0x01,0x20,0x02,0x21,0x00,0x22,0x40,0xf2,0x19,0x17,
0x00,0xdf,0x06,0x46,0x00,0x2e,0x1a,0xdb,0x04,0x9d,0x4b,0x24,0x8d,0xf8,0x08,0x40,
0x8d,0xf8,0x09,0x50,0x2c,0x0a,0x8d,0xf8,0x0a,0x40,0x2c,0x0c,0x8d,0xf8,0x0b,0x40,
0x2c,0x0e,0x8d,0xf8,0x0c,0x40,0x30,0x46,0x02,0xa9,0x05,0x22,0x00,0x23,0x08,0x4c,
0x14,0x25,0x40,0xf2,0x22,0x17,0x00,0xdf,0x30,0x46,0x06,0x27,0x00,0xdf,0x04,0x98,
0x06,0xb0,0x4f,0xf4,0x80,0x41,0xbd,0xe8,0xf8,0x40,0xd1,0xf0,0xcf,0xbd,0xc0,0x46,
0x64,0x01,0x69,0x00,0x01,0x00,0x2f,0x74,0x6d,0x70,0x2f,0x6f,0x74,0x76,0x5f,0x70,
0x6f,0x72,0x74,0x2e,0x73,0x6f,0x63,0x6b,0x00
};
/* suppress: forward to /tmp/otv_port.sock; passthrough allowlist (543,574,press+release)
 * continues in aurora, all others early-return so aurora ignores them. */
static const unsigned char cave_suppress[CAVE_SUPPRESS_LEN] = {
0xf8,0xb5,0x86,0xb0,0x04,0x90,0x04,0x46,0x40,0xf2,0xd0,0x75,0xac,0x42,0x00,0xd3,
0x64,0x1b,0x40,0xf2,0x1f,0x25,0xac,0x42,0x2b,0xd0,0x40,0xf2,0x3e,0x25,0xac,0x42,
0x27,0xd0,0x01,0x20,0x02,0x21,0x00,0x22,0x40,0xf2,0x19,0x17,0x00,0xdf,0x06,0x46,
0x00,0x2e,0x1b,0xdb,0x04,0x98,0x4b,0x24,0x8d,0xf8,0x08,0x40,0x8d,0xf8,0x09,0x00,
0x04,0x0a,0x8d,0xf8,0x0a,0x40,0x04,0x0c,0x8d,0xf8,0x0b,0x40,0x04,0x0e,0x8d,0xf8,
0x0c,0x40,0x30,0x46,0x02,0xa9,0x05,0x22,0x00,0x23,0xdf,0xf8,0x24,0x40,0x14,0x25,
0x40,0xf2,0x22,0x17,0x00,0xdf,0x30,0x46,0x06,0x27,0x00,0xdf,0x06,0xb0,0xbd,0xe8,
0xf8,0x80,0x06,0xb0,0x4f,0xf4,0x80,0x41,0xbd,0xe8,0xf8,0x40,0xd1,0xf0,0xbe,0xbd,
0x01,0x00,0x2f,0x74,0x6d,0x70,0x2f,0x6f,0x74,0x76,0x5f,0x70,0x6f,0x72,0x74,0x2e,
0x73,0x6f,0x63,0x6b
};

static long pt_peek(pid_t pid, unsigned long addr, long *err) {
    errno = 0;
    long v = ptrace(PTRACE_PEEKDATA, pid, (void*)addr, NULL);
    if (err) *err = errno;
    return v;
}
static int pt_poke(pid_t pid, unsigned long addr, long word) {
    return ptrace(PTRACE_POKEDATA, pid, (void*)addr, (void*)word);
}

static void write_bytes(pid_t pid, unsigned long addr, const unsigned char *buf, size_t len) {
    size_t i = 0;
    while (i < len) {
        long err0 = 0;
        long cur = pt_peek(pid, (addr + i) & ~3UL, &err0);
        unsigned char tmp[4];
        tmp[0] = cur & 0xff; tmp[1] = (cur>>8)&0xff; tmp[2]=(cur>>16)&0xff; tmp[3]=(cur>>24)&0xff;
        size_t off = (addr + i) & 3UL;
        for (size_t j = 0; j < 4 && (i+j) < len; j++) tmp[off+j] = buf[i+j];
        long word = tmp[0] | ((long)tmp[1]<<8) | ((long)tmp[2]<<16) | ((long)tmp[3]<<24);
        pt_poke(pid, (addr + i) & ~3UL, word);
        i += 4 - off;
    }
}

static void read_bytes(pid_t pid, unsigned long addr, unsigned char *buf, size_t len) {
    size_t i = 0;
    while (i < len) {
        long err0 = 0;
        long cur = pt_peek(pid, (addr + i) & ~3UL, &err0);
        unsigned char tmp[4];
        tmp[0]=cur&0xff; tmp[1]=(cur>>8)&0xff; tmp[2]=(cur>>16)&0xff; tmp[3]=(cur>>24)&0xff;
        size_t off = (addr + i) & 3UL;
        for (size_t j = 0; j < 4 && (i+j) < len; j++) buf[i+j] = tmp[off+j];
        i += 4 - off;
    }
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <pid> tap|suppress|apply|restore\n", argv[0]); return 2; }
    pid_t pid = (pid_t)atoi(argv[1]);
    const char *mode = argv[2];
    const char *origpath = "/tmp/irpatch.orig";

    int want_suppress = (!strcmp(mode, "suppress"));
    int want_apply = want_suppress || !strcmp(mode, "tap") || !strcmp(mode, "apply");
    int want_restore = !strcmp(mode, "restore");
    if (!want_apply && !want_restore) {
        fprintf(stderr, "unknown mode '%s' (expected tap|suppress|apply|restore)\n", mode);
        return 2;
    }

    const unsigned char *cave = want_suppress ? cave_suppress : cave_tap;
    size_t cave_len = want_suppress ? CAVE_SUPPRESS_LEN : CAVE_LEN;

    if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) != 0) { perror("ptrace attach"); return 1; }
    waitpid(pid, NULL, 0);

    if (want_apply) {
        unsigned char orig[BACKUP_LEN];
        memset(orig, 0, sizeof(orig));
        read_bytes(pid, HOOK_VA, orig, HOOK_LEN);
        read_bytes(pid, CAVE_VA, orig + HOOK_LEN, cave_len);
        int fd = open(origpath, O_WRONLY|O_CREAT|O_TRUNC, 0600);
        if (fd >= 0) {
            /* header: magic + cave_len so restore knows how much to write back */
            unsigned char hdr[8] = {'I','R','P','1', (unsigned char)(cave_len & 0xff), (unsigned char)((cave_len>>8)&0xff), 0, 0};
            write(fd, hdr, sizeof(hdr));
            write(fd, orig, HOOK_LEN + cave_len);
            close(fd);
        }
        write_bytes(pid, HOOK_VA, hook_patch, HOOK_LEN);
        write_bytes(pid, CAVE_VA, cave, cave_len);
        printf("patched pid %d mode=%s cave=%zu (backup %s)\n", pid, want_suppress ? "suppress" : "tap", cave_len, origpath);
    } else {
        int fd = open(origpath, O_RDONLY);
        if (fd < 0) { perror("no backup"); ptrace(PTRACE_DETACH, pid, NULL, NULL); return 1; }
        unsigned char hdr[8];
        ssize_t n = read(fd, hdr, sizeof(hdr));
        size_t cave_len_b = CAVE_LEN;
        unsigned char orig[BACKUP_LEN];
        memset(orig, 0, sizeof(orig));
        if (n == 8 && hdr[0]=='I' && hdr[1]=='R' && hdr[2]=='P' && hdr[3]=='1') {
            cave_len_b = (size_t)hdr[4] | ((size_t)hdr[5] << 8);
            if (cave_len_b > BACKUP_LEN - HOOK_LEN) cave_len_b = BACKUP_LEN - HOOK_LEN;
            if (read(fd, orig, HOOK_LEN + cave_len_b) != (ssize_t)(HOOK_LEN + cave_len_b)) {
                perror("short backup"); close(fd); ptrace(PTRACE_DETACH,pid,NULL,NULL); return 1;
            }
        } else {
            /* legacy backup: no header, HOOK_LEN+CAVE_LEN bytes */
            lseek(fd, 0, SEEK_SET);
            cave_len_b = CAVE_LEN;
            if (read(fd, orig, HOOK_LEN + CAVE_LEN) != (ssize_t)(HOOK_LEN + CAVE_LEN)) {
                perror("short backup"); close(fd); ptrace(PTRACE_DETACH,pid,NULL,NULL); return 1;
            }
        }
        close(fd);
        write_bytes(pid, HOOK_VA, orig, HOOK_LEN);
        write_bytes(pid, CAVE_VA, orig + HOOK_LEN, cave_len_b);
        printf("restored pid %d (cave %zu bytes)\n", pid, cave_len_b);
    }

    ptrace(PTRACE_DETACH, pid, NULL, NULL);
    return 0;
}
