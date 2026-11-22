#include <stdint.h>
#include <errno.h>
#include <unistd.h>

#include <ext2_blkiter.h>
#include <fs_malloc.h>
#include <solution.h>
#include <utils.h>

int dump_file(int img, int inode_nr, int out)
{
	struct ext2_fs *fs = NULL;
	struct ext2_blkiter *iter = NULL;
	void *buffer = NULL;
	int fs_fd = dup(img);
	if (fs_fd < 0)
		return -errno;

	int err = ext2_fs_init(&fs, fs_fd);
	if (err < 0)
		return err;

	err = ext2_blkiter_init(&iter, fs, inode_nr);
	if (err < 0)
		goto cleanup;

	uint32_t block_size = ext2_fs_block_size(fs);
	uint64_t remaining = ext2_blkiter_file_size(iter);
	buffer = fs_xmalloc(block_size);

	while (1) {
		int block;
		int next = ext2_blkiter_next(iter, &block);
		if (next < 0) {
			err = next;
			goto cleanup;
		}
		if (next == 0) {
			err = 0;
			break;
		}

		err = ext2_fs_read_block(fs, block, buffer);
		if (err < 0)
			goto cleanup;

		size_t to_write = remaining < block_size ? remaining : block_size;
		err = write_exact(out, buffer, to_write);
		if (err < 0)
			goto cleanup;
		remaining -= to_write;
	}

cleanup:
	fs_xfree(buffer);
	ext2_blkiter_free(iter);
	ext2_fs_free(fs);
	return err;
}
