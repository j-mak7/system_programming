#include "print.h"


void print(int len, info_file* arr, bool flag_a, bool flag_i) {
    while (len > 0) {
        len--;
        if (flag_i) {
            printf("%ld ", arr[len].innode);
        }
        if (flag_a) {
            printf("%s\n", arr[len].name);
        } else {
            if (arr[len].name[0] != '.') {
                printf("%s\n", arr[len].name);
            }
        }
    }
}