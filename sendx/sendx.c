#include <stdio.h>
#include <stdlib.h>
#include <error.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>

#define NPAGES 8

int main(int argn, char* argv[], char * envp[])
{
    int fd;
    int ret;
    unsigned int *kadr;
    int len = NPAGES * getpagesize();

    fd = open("/dev/morse0", O_RDWR, 0);
    if(fd == -1)
        error(1, errno, "open()");

    // ret = write(fd, "x", 1);
    // if(ret == -1)
    //     error(1, errno, "write()");

    kadr = mmap(0, len, PROT_READ | PROT_WRITE , MAP_SHARED, fd, 0);
    if(kadr == MAP_FAILED)
    {
        printf("Error!\n");
        return -2;
    }

    printf("send: %s\n", kadr);

    munmap(kadr, len);
    close(fd);

    return 0;
}