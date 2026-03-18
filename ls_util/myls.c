//#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "wrapper.h"
#include "print.h"

#define BUF_SIZE 4096

typedef struct dirent {
    unsigned long d_ino;
    off_t d_off;
    unsigned short d_reclen;
    char d_name[];
} dirent;

int compare (const void * a, const void * b) {
    const info_file *ia = a;
    const info_file *ib = b;
    return strcmp(ib->name, ia->name);
}

void free_arr(int len, info_file* arr) {
    while (len > 0) {
        len--;
        free(arr[len].name);
    }
    free(arr);
}

void list_directory(const char *path, bool flag_a, bool flag_i) {
    struct stat s;
    if (wrap_stat(path, &s) != 0)
    {
//        printf("%s\n", path);
//        printf("%d\n", errno);
        perror(path);
        return;
    }
    if( s.st_mode & S_IFREG ) {
        if (flag_i) {
            printf("%ld ", s.st_ino);
        }
        printf("%s\n", path);
        return;
    }
    char buffer[BUF_SIZE];
    int len = 0;
    int size = 2;
    long nread;
    int fd = wrap_open(path);
    //printf("%d\n", fd);
    if (fd < 0) {
        printf("Cannot open directory\n");
        return;
    }
    info_file* files = calloc(size, sizeof(info_file));
    while ((nread = wrap_getdents(fd, buffer, BUF_SIZE)) > 0) {
        dirent* d;
        int bpos = 0;
	    while (bpos < nread) {
            d = (dirent* )(buffer + bpos);
            if (len == size) {
                size *= 2;
                files = realloc(files, size * sizeof(info_file));
            }
            files[len].name = strdup(d->d_name);
            files[len].innode = d->d_ino;
            bpos += d->d_reclen;
	        len++;
	    }
    }
    wrap_close(fd);
    qsort(files, len, sizeof(info_file), compare);
    print(len, files, flag_a, flag_i);
    free_arr(len, files);
}

int main(int argc, char *argv[]) {
    const char *path = ".";
    bool flag_a = false;
    bool flag_i = false;
    int opt;
    while ((opt = getopt(argc, argv, "ai")) != -1) {
        switch (opt) {
            case 'a':
                flag_a = true;
                break;
            case 'i':
                flag_i = true;
                break;
            case '?':
                printf("Unknown option: -%c\n", optopt);
                return 1;
        }
    }
    if (optind >= argc) {
        list_directory(path, flag_a, flag_i);
        return 0;
    }
    int count = argc - optind;
    for(; optind < argc; optind++){
        path = argv[optind];
        if (count > 1) {printf("%s:\n", path);}
        list_directory(path, flag_a, flag_i);
        count++;
    }
    return 0;
}
