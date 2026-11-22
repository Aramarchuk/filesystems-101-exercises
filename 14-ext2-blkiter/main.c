#include <solution.h>

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <err.h>

int main(int argc, char **argv)
{
	if (argc != 3) {
		fprintf(stderr, "use: ./a.out <img-file-name> <inode-nr>\n");
		return 1;
	}

	int img = open(argv[1], O_RDONLY);
	if (img < 0)
		errx(1, "open(img) failed");
	int ino = atoi(argv[2]);

	int r = dump_blocks(img, ino);
	if (r < 0)
		errx(1, "dump_blocks() failed");
	return 0;
}
