#include "ext2_blkiter.h"

#include <endian.h>
#include <errno.h>
#include <stdint.h>
#include <unistd.h>

#include "fs_ext2.h"
#include "fs_malloc.h"
#include "utils.h"

struct ext2_fs
{
	struct ext2_super_block sb;
	uint32_t block_size;
	uint32_t inode_size;
	uint64_t ext2_blocks_in_single_indirect;
	uint64_t ext2_blocks_in_double_indirect;
	uint64_t ext2_blocks_in_triple_indirect;
	int fd;
};

struct ext2_blkiter
{
	struct ext2_fs *fs;
	uint32_t i_block[EXT2_N_BLOCKS];
	uint64_t blocks_num;
	uint64_t file_size;
	uint64_t read;

	uint64_t direct_blocks;
	uint64_t single_indirect_blocks;
	uint64_t double_indirect_blocks;
	uint64_t triple_indirect_blocks;

	__le32 *indirect_block_buffer;
	__le32 *double_indirect_block_buffer;
	__le32 *triple_indirect_block_buffer;
};

int ext2_fs_init(struct ext2_fs **fs, int fd)
{
	if (fs == NULL)
		return -EINVAL;
	*fs = NULL;

	struct ext2_fs *ext2 = fs_xzalloc(sizeof(*ext2));
	ext2->fd = fd;

	int err = read_exact(fd, &ext2->sb, sizeof(ext2->sb),
			     EXT2_SUPERBLOCK_OFFSET);
	if (err < 0)
		goto fail;

	ext2->block_size = 1024U << le32toh(ext2->sb.s_log_block_size);
	ext2->inode_size = le32toh(ext2->sb.s_rev_level) == 0 ?
			   sizeof(struct ext2_inode) :
			   le16toh(ext2->sb.s_inode_size);
	ext2->ext2_blocks_in_single_indirect =
		ext2->block_size / sizeof(__le32);
	ext2->ext2_blocks_in_double_indirect =
		ext2->ext2_blocks_in_single_indirect *
		ext2->ext2_blocks_in_single_indirect;
	ext2->ext2_blocks_in_triple_indirect =
		ext2->ext2_blocks_in_double_indirect *
		ext2->ext2_blocks_in_single_indirect;

	*fs = ext2;
	return 0;

fail:
	close(fd);
	fs_xfree(ext2);
	return err;
}

void ext2_fs_free(struct ext2_fs *fs)
{
	if (fs == NULL)
		return;
	close(fs->fd);
	fs_xfree(fs);
}

uint32_t ext2_fs_block_size(const struct ext2_fs *fs)
{
	return fs->block_size;
}

int ext2_fs_read_block(struct ext2_fs *fs, uint32_t block, void *buf)
{
	off_t offset = (off_t)block * fs->block_size;
	return read_exact(fs->fd, buf, fs->block_size, offset);
}

int ext2_blkiter_init(struct ext2_blkiter **i, struct ext2_fs *fs, int ino)
{
	if (i == NULL)
		return -EINVAL;
	*i = NULL;
	if (fs == NULL || ino <= 0 ||
	    (uint32_t)ino > le32toh(fs->sb.s_inodes_count))
		return -EINVAL;

	struct ext2_blkiter *iter = fs_xzalloc(sizeof(*iter));
	struct ext2_inode inode;
	iter->fs = fs;
	iter->indirect_block_buffer = fs_xmalloc(fs->block_size);
	iter->double_indirect_block_buffer = fs_xmalloc(fs->block_size);
	iter->triple_indirect_block_buffer = fs_xmalloc(fs->block_size);

	uint32_t inodes_per_group = le32toh(fs->sb.s_inodes_per_group);
	uint32_t group_nr = ((uint32_t)ino - 1) / inodes_per_group;
	uint32_t inode_in_group = ((uint32_t)ino - 1) % inodes_per_group;

	uint32_t gdt_block = fs->block_size == 1024 ? 2 : 1;
	off_t gdt_offset = (off_t)gdt_block * fs->block_size;
	off_t descriptor_offset =
		gdt_offset + (off_t)group_nr * sizeof(struct ext2_block_group_header);

	struct ext2_block_group_header bgh;
	int err = read_exact(fs->fd, &bgh, sizeof(bgh), descriptor_offset);
	if (err < 0)
		goto fail;

	off_t inode_offset =
		(off_t)le32toh(bgh.bg_inode_table) * fs->block_size +
		(off_t)inode_in_group * fs->inode_size;
	err = read_exact(fs->fd, &inode, sizeof(inode), inode_offset);
	if (err < 0)
		goto fail;
	if (le16toh(inode.i_mode) == 0 || le16toh(inode.i_links_count) == 0) {
		err = -ENOENT;
		goto fail;
	}

	for (uint32_t n = 0; n < EXT2_N_BLOCKS; n++)
		iter->i_block[n] = le32toh(inode.i_block[n]);

	iter->blocks_num =
		((uint64_t)le32toh(inode.i_size) + fs->block_size - 1) /
		fs->block_size;
	iter->file_size = le32toh(inode.i_size);
	iter->direct_blocks = iter->blocks_num < EXT2_DIRECT_BLOCKS ?
				      iter->blocks_num : EXT2_DIRECT_BLOCKS;

	uint64_t remaining = iter->blocks_num - iter->direct_blocks;
	iter->single_indirect_blocks =
		remaining < fs->ext2_blocks_in_single_indirect ?
		remaining : fs->ext2_blocks_in_single_indirect;
	remaining -= iter->single_indirect_blocks;
	iter->double_indirect_blocks =
		remaining < fs->ext2_blocks_in_double_indirect ?
		remaining : fs->ext2_blocks_in_double_indirect;
	remaining -= iter->double_indirect_blocks;
	iter->triple_indirect_blocks =
		remaining < fs->ext2_blocks_in_triple_indirect ?
		remaining : fs->ext2_blocks_in_triple_indirect;

	*i = iter;
	return 0;

fail:
	ext2_blkiter_free(iter);
	return err;
}

int ext2_blkiter_next(struct ext2_blkiter *i, uint32_t *blkno)
{
	if (i == NULL || blkno == NULL)
		return -EINVAL;
	if (i->read >= i->blocks_num)
		return 0;

	uint64_t index = i->read;
	uint32_t block;
	int err;
	uint64_t pointers_per_block = i->fs->block_size / sizeof(__le32);

	if (index < i->direct_blocks) {
		block = i->i_block[index];
	} else if (index < i->direct_blocks + i->single_indirect_blocks) {
		uint64_t single_index = index - i->direct_blocks;
		if (single_index == 0) {
			off_t offset = (off_t)i->i_block[EXT2_SINGLE_INDRECT_IBLOCK] *
				       i->fs->block_size;
			err = read_exact(i->fs->fd, i->indirect_block_buffer,
					i->fs->block_size, offset);
			if (err < 0)
				return err;
		}
		block = le32toh(i->indirect_block_buffer[single_index]);
	} else if (index < i->direct_blocks + i->single_indirect_blocks +
		   i->double_indirect_blocks) {
		uint64_t double_index = index - i->direct_blocks -
					i->single_indirect_blocks;
		uint64_t root_index = double_index / pointers_per_block;
		uint64_t leaf_index = double_index % pointers_per_block;

		if (double_index == 0) {
			off_t offset = (off_t)i->i_block[EXT2_DOUBLE_INDRECT_IBLOCK] *
				       i->fs->block_size;
			err = read_exact(i->fs->fd, i->double_indirect_block_buffer,
					i->fs->block_size, offset);
			if (err < 0)
				return err;
		}
		if (leaf_index == 0) {
			off_t offset =
				(off_t)le32toh(i->double_indirect_block_buffer[root_index]) *
				i->fs->block_size;
			err = read_exact(i->fs->fd, i->indirect_block_buffer,
					i->fs->block_size, offset);
			if (err < 0)
				return err;
		}
		block = le32toh(i->indirect_block_buffer[leaf_index]);
	} else {
		uint64_t triple_index = index - i->direct_blocks -
					i->single_indirect_blocks -
					i->double_indirect_blocks;
		uint64_t pointers_squared = pointers_per_block * pointers_per_block;
		uint64_t root_index = triple_index / pointers_squared;
		uint64_t middle_index =
			(triple_index / pointers_per_block) % pointers_per_block;
		uint64_t leaf_index = triple_index % pointers_per_block;

		if (triple_index == 0) {
			off_t offset = (off_t)i->i_block[EXT2_TRIPLE_INDRECT_IBLOCK] *
				       i->fs->block_size;
			err = read_exact(i->fs->fd, i->triple_indirect_block_buffer,
					i->fs->block_size, offset);
			if (err < 0)
				return err;
		}
		if (triple_index % pointers_squared == 0) {
			off_t offset =
				(off_t)le32toh(i->triple_indirect_block_buffer[root_index]) *
				i->fs->block_size;
			err = read_exact(i->fs->fd, i->double_indirect_block_buffer,
					i->fs->block_size, offset);
			if (err < 0)
				return err;
		}
		if (leaf_index == 0) {
			off_t offset =
				(off_t)le32toh(i->double_indirect_block_buffer[middle_index]) *
				i->fs->block_size;
			err = read_exact(i->fs->fd, i->indirect_block_buffer,
					i->fs->block_size, offset);
			if (err < 0)
				return err;
		}
		block = le32toh(i->indirect_block_buffer[leaf_index]);
	}

	*blkno = block;
	i->read++;
	return 1;
}

uint64_t ext2_blkiter_file_size(const struct ext2_blkiter *i)
{
	return i->file_size;
}

void ext2_blkiter_free(struct ext2_blkiter *i)
{
	if (i == NULL)
		return;
	fs_xfree(i->double_indirect_block_buffer);
	fs_xfree(i->triple_indirect_block_buffer);
	fs_xfree(i->indirect_block_buffer);
	fs_xfree(i);
}
