#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <features.h>
#include <gnu/libc-version.h>
#include <linux/limits.h>   /* kernel-заголовки (linux-libc-dev): PATH_MAX */

/* Компайл-тайм проверка: заголовки именно >= 2.28, а не откатились на 2.24. */
#if !defined(__GLIBC_PREREQ) || !__GLIBC_PREREQ(2, 28)
# error "glibc headers older than 2.28 (заголовки перезатёрты старой версией?)"
#endif

int main(void) {
    printf("runtime glibc      : %s\n", gnu_get_libc_version());
    printf("header  glibc      : %d.%d\n", __GLIBC__, __GLIBC_MINOR__);
    printf("kernel PATH_MAX    : %d\n", PATH_MAX);
    /* copy_file_range объявлена в <unistd.h> с glibc 2.27 (нужен _GNU_SOURCE) и
       есть символом в libc — берём её адрес: при 2.24-заголовках/libc не соберётся. */
    printf("copy_file_range @  : %p\n", (void *)copy_file_range);
    return 0;
}
