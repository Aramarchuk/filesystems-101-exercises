#include <errno.h>
#include <fs_ext2.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#include <fs_malloc.h>

#include <solution.h>

#define EXT2_SUPERBLOCK_OFFSET 1024

#define EXT2_DIRECT_BLOCKS 12
#define EXT2_SINGLE_INDRECT_IBLOCK 12
#define EXT2_DOUBLE_INDRECT_IBLOCK 13

int read_exact(int fd, void *buf, size_t size, off_t offset)
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

int write_exact(int fd, void *buf, size_t size)
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

int translate_block(int img, int out, uint32_t *buf, uint32_t block_size, uint32_t *remaining, uint32_t block_number)
{
	int err;
	size_t to_write = *remaining < block_size ? *remaining : block_size;
	if ((err = read_exact(img, buf, to_write, (off_t)block_number * block_size)) < 0)
		return err;
	if ((err = write_exact(out, buf, to_write)))
		return err;
	*remaining -= to_write;
	return 0;
}

int dump_file(int img, int inode_nr, int out)
{
	int err = 0;

	struct ext2_super_block sb;
	struct ext2_block_group_header bg_header;
	struct ext2_inode inode;

	__le32 *block_buffer = NULL;
	__le32 *indirect_block_buffer = NULL;
	__le32 *double_indirect_block_buffer = NULL;
	
	if ((err = read_exact(img, &sb, sizeof(struct ext2_super_block), EXT2_SUPERBLOCK_OFFSET)) < 0)
		goto cleanup;
	uint32_t block_size = 1024U << sb.s_log_block_size;
	uint32_t group_nr = (inode_nr - 1) / sb.s_inodes_per_group;
	uint32_t inode_in_group = (inode_nr - 1) % sb.s_inodes_per_group;

	uint32_t ext2_blocks_in_single_indirect = block_size / sizeof(__le32);
	uint32_t ext2_blocks_in_double_indirect = ext2_blocks_in_single_indirect * ext2_blocks_in_single_indirect;

	
	uint32_t gdt_block = (block_size == 1024) ? 2 : 1;
	off_t gdt_offset = (off_t)gdt_block * block_size;
	off_t descriptor_offset = gdt_offset + (off_t)group_nr * sizeof(struct ext2_block_group_header);

	block_buffer = fs_xmalloc(block_size);
	indirect_block_buffer = fs_xmalloc(block_size);
	double_indirect_block_buffer = fs_xmalloc(block_size);

	if ((err = read_exact(img, &bg_header, sizeof(struct ext2_block_group_header),descriptor_offset)) < 0)
		goto cleanup;

	off_t inode_offset =
    (off_t)bg_header.bg_inode_table * block_size +
    (off_t)inode_in_group * sb.s_inode_size;

	if ((err = read_exact(img, &inode, sizeof(struct ext2_inode),inode_offset)) < 0)
		goto cleanup;

	uint32_t remaining = inode.i_size;
	uint32_t data_blocks = (inode.i_size + block_size - 1) / block_size;

	uint32_t direct_blocks = data_blocks;
	if (direct_blocks > EXT2_DIRECT_BLOCKS)
		direct_blocks = EXT2_DIRECT_BLOCKS;

	uint32_t single_inderct_blocks = data_blocks - direct_blocks;
	if (single_inderct_blocks > ext2_blocks_in_single_indirect)
		single_inderct_blocks = ext2_blocks_in_single_indirect;

	uint32_t double_inderct_blocks = data_blocks - direct_blocks - single_inderct_blocks;
	if (double_inderct_blocks > ext2_blocks_in_double_indirect)
		double_inderct_blocks = ext2_blocks_in_double_indirect;

	// Direct blocks
	for (uint32_t i = 0; i < direct_blocks; i++)
	{
		if ((err = translate_block(img, out, block_buffer, block_size, &remaining, inode.i_block[i])) < 0)
			goto cleanup;
	}

	// Single Indirect blocks
	if (single_inderct_blocks > 0)
	{
		off_t single_indirect_offset = (off_t)inode.i_block[EXT2_SINGLE_INDRECT_IBLOCK] * block_size;
		if ((err = read_exact(img, indirect_block_buffer, block_size, single_indirect_offset)) < 0)
			goto cleanup;

		for (uint32_t i = 0; i < single_inderct_blocks; i++)
		{

			if ((err = translate_block(img, out, block_buffer, block_size, &remaining, indirect_block_buffer[i])) < 0)
				goto cleanup;
		}
	}
	// Double Indirect blocks
	if (double_inderct_blocks > 0)
	{
		off_t double_indirect_offset = (off_t)inode.i_block[EXT2_DOUBLE_INDRECT_IBLOCK] * block_size;

		if ((err = read_exact(img, double_indirect_block_buffer, block_size, double_indirect_offset)) < 0)
			goto cleanup;
		uint32_t remaining_blocks = double_inderct_blocks;

		for (uint32_t i = 0; remaining_blocks > 0; i++)
		{
			off_t indirect_block_offset = (off_t)double_indirect_block_buffer[i] * block_size;

			if ((err = read_exact(img, indirect_block_buffer, block_size, indirect_block_offset)) < 0)
				goto cleanup;

			uint32_t count = remaining_blocks < ext2_blocks_in_single_indirect
				? remaining_blocks
				: ext2_blocks_in_single_indirect;

			for (uint32_t j = 0; j < count; j++)
			{
				if ((err = translate_block(img, out, block_buffer, block_size,
										&remaining,
										indirect_block_buffer[j])) < 0)
					goto cleanup;
			}
			remaining_blocks -= count;
		}
	}

cleanup:
	free(block_buffer);
	free(indirect_block_buffer);
	free(double_indirect_block_buffer);
	return err;
}
