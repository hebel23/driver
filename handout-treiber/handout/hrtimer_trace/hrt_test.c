// hrt_test.c

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


int main(int argn, char* argv[])
{
	int fd;
	int ret;
	int cmd;
	unsigned long arg;

	if (argn >= 2)
	{
		cmd = atoi (argv[1]);
		if (argn >= 3)
			arg = atoi (argv[2]);
		else
			arg = 0;
	}
	else
	{
		printf ("usage: hrt_test <cmd>\n");
		return (-4);
	}

	fd = open("/dev/hrt", O_RDWR);
	if (fd == -1)
	{
		perror("open");
		return(-1);
	}

	ret = ioctl (fd, cmd, arg);
	if (ret == -1)
	{
		perror("ioctl");
		return(-2);
	}

	close(fd);

	return(0);
}


