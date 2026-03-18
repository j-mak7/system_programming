#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/unistd.h>
#include <sys/syscall.h>
#include <errno.h>
#include "wrapper.h"

int sys_stat(const char *pathname, struct stat* s) {
    int ret;
    asm volatile (
            "movl $4, %%eax\n"
            "movq %1, %%rdi\n"
            "movq %2, %%rsi\n"
            "syscall\n"
            "movl %%eax, %0\n"
            : "=r" (ret)
            : "r" (pathname), "r" (s)
            : "rax", "rdi", "rsi"
            );
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

int sys_open(const char *pathname, int flags, int mode) {
    int ret;
    asm volatile (
            "movl $2, %%eax\n"
            "movq %1, %%rdi\n"
            "movl %2, %%esi\n"
            "movl %3, %%edx\n"
            "syscall\n"
            "movl %%eax, %0\n"
            : "=r" (ret)
            : "r" (pathname), "r" (flags), "r" (mode)
            : "rax", "rdi", "rsi", "rdx"
            );
    return ret;
}

long sys_getdents(int fd, char *buf, int count) {
    long ret;
    asm volatile (
            "movl $78, %%eax\n"
            "movl %1, %%edi\n"
            "movq %2, %%rsi\n"
            "movl %3, %%edx\n"
            "syscall\n"
            "movq %%rax, %0\n"
            : "=r" (ret)
            : "r" (fd), "r" (buf), "r" (count)
            : "rax", "rdi", "rsi", "rdx"
            );
    return ret;
}

int sys_close(int fd) {
    int ret;
    asm volatile (
            "movl $3, %%eax\n"
            "movl %1, %%edi\n"
            "syscall\n"
            "movl %%eax, %0\n"
            : "=r" (ret)
            : "r" (fd)
            : "rax", "rdi"
            );
    return ret;
}

int wrap_stat(const char *pathname, struct stat* s) {
#if FL == 0
    return syscall(SYS_stat, pathname, s);
#else
    return sys_stat(pathname, s);
#endif
}

int wrap_open(const char* path) {
#if FL == 0
    return open(path, O_DIRECTORY | O_RDONLY, 0);
#else
    return sys_open(path, O_DIRECTORY | O_RDONLY, 0);
#endif
}

long wrap_getdents(int fd, char* buffer, int BUF_SIZE) {
#if FL == 0
    return syscall(SYS_getdents, fd, buffer, BUF_SIZE);
#else
    return sys_getdents(fd, buffer, BUF_SIZE);
#endif
}

void wrap_close(int fd){
#if FL == 0
    close(fd);
#else
    sys_close(fd);
#endif
}

