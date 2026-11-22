#include <errno.h>
#include <fs_ext2.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#include <solution.h>


static int read_exact(int fd, void *buf, size_t size, off_t offset)
{
    size_t done = 0;

    while (done < size) {
        ssize_t n = pread(fd, (char *)buf + done,
                          size - done, offset + done);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -errno;
        }

        if (n == 0)
            return -EIO;

        done += n;
    }

    return 0;
}

static int write_exact(int fd, void *buf, size_t size)
{
    size_t done = 0;

    while (done < size) {
        ssize_t n = write(fd, (char *)buf + done,
                          size - done);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -errno;
        }

        if (n == 0)
            return -EIO;

        done += n;
    }

    return 0;
}

int dump_dir(int img, int inode_nr)
{
	(void) img;
	(void) inode_nr;



	/* implement me */
	return 0;
}
