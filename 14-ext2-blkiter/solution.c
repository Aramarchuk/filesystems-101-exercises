#include <ext2_blkiter.h>
#include <stdio.h>

#include <solution.h>

int dump_blocks(int img, int ino)
{
	struct ext2_fs *fs = NULL;
	struct ext2_blkiter *iter = NULL;
	int err = ext2_fs_init(&fs, img);
	if (err < 0)
		return err;

	err = ext2_blkiter_init(&iter, fs, ino);
	if (err < 0)
		goto cleanup;

	for (;;) {
		uint32_t block;
		int next = ext2_blkiter_next(iter, &block);
		if (next < 0) {
			err = next;
			goto cleanup;
		}
		if (next == 0) {
			err = 0;
			break;
		}
		printf("%u\n", block);
	}

cleanup:
	ext2_blkiter_free(iter);
	ext2_fs_free(fs);
	return err;
}
