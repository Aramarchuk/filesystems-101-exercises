#include <solution.h>
#include <fs_malloc.h>
#include <fs_ext2.h>

#include <stdint.h>
#include <utils.h>

struct ext2_fs
{
	struct ext2_super_block sb;
	struct ext2_block_group_header bgh;
	uint32_t block_size;

	uint32_t ext2_blocks_in_single_indirect;
	uint32_t ext2_blocks_in_double_indirect;
	uint32_t ext2_blocks_in_triple_indirect;

	int fd;
};

struct ext2_blkiter
{
	uint32_t inode_nr;
	uint32_t blocks_num;
	uint32_t i_block[EXT2_N_BLOCKS];
	
	uint32_t read;
	struct ext2_fs *fs;
	
	__le32 *indirect_block_buffer;
	__le32 *double_indirect_block_buffer;
	__le32 *triple_indirect_block_buffer;

	uint32_t direct_blocks;
	uint32_t single_indirect_blocks;
	uint32_t double_indirect_blocks;
	uint32_t triple_indirect_blocks;
};

int ext2_fs_init(struct ext2_fs **fs, int fd)
{
	struct ext2_fs *ext2 = fs_xzalloc(sizeof(struct ext2_fs));
	ext2->fd = fd;
	int err;
	
	if ((err = read_exact(fd, &ext2->sb, sizeof(struct ext2_super_block), EXT2_SUPERBLOCK_OFFSET)) < 0)
		return err;
	ext2->block_size = 1024U << ext2->sb.s_log_block_size;

	ext2->ext2_blocks_in_single_indirect = ext2->block_size / sizeof(__le32);
	ext2->ext2_blocks_in_double_indirect = ext2->ext2_blocks_in_single_indirect * ext2->ext2_blocks_in_single_indirect;
	ext2->ext2_blocks_in_triple_indirect = ext2->ext2_blocks_in_double_indirect * ext2->ext2_blocks_in_single_indirect;

	*fs = ext2;
	return 0;
}

void ext2_fs_free(struct ext2_fs *fs)
{
	if (fs == NULL)
		return;
	close(fs->fd);
	free(fs);
}

int ext2_blkiter_init(struct ext2_blkiter **i, struct ext2_fs *fs, int ino)
{
	struct ext2_blkiter *iter = fs_xzalloc(sizeof(struct ext2_blkiter));
	struct ext2_inode inode;
	int err;

	iter->indirect_block_buffer = fs_xmalloc(fs->block_size);
	iter->double_indirect_block_buffer = fs_xmalloc(fs->block_size);
	iter->triple_indirect_block_buffer = fs_xmalloc(fs->block_size);

	uint32_t group_nr = (ino - 1) / fs->sb.s_inodes_per_group;
	uint32_t inode_in_group = (ino - 1) % fs->sb.s_inodes_per_group;

	uint32_t gdt_block = (fs->block_size == 1024) ? 2 : 1;
	off_t gdt_offset = (off_t)gdt_block * fs->block_size;
	off_t descriptor_offset = gdt_offset + (off_t)group_nr * sizeof(struct ext2_block_group_header);

	if ((err = read_exact(fs->fd, &fs->bgh, sizeof(struct ext2_block_group_header),descriptor_offset)) < 0)
		goto err;

	
	off_t inode_offset = (off_t)fs->bgh.bg_inode_table * fs->block_size + (off_t)inode_in_group * fs->sb.s_inode_size;

	if ((err = read_exact(fs->fd, &inode, sizeof(struct ext2_inode),inode_offset)) < 0)
		goto err;

	uint32_t data_blocks = (inode.i_size + fs->block_size - 1) / fs->block_size;
	iter->blocks_num = data_blocks;

	uint32_t direct_blocks = data_blocks;
	if (direct_blocks > EXT2_DIRECT_BLOCKS)
		direct_blocks = EXT2_DIRECT_BLOCKS;

	iter->single_indirect_blocks = data_blocks - direct_blocks;
	if (iter->single_indirect_blocks > iter->fs->ext2_blocks_in_single_indirect)
		iter->single_indirect_blocks = iter->fs->ext2_blocks_in_single_indirect;

	iter->double_indirect_blocks = data_blocks - direct_blocks - iter->single_indirect_blocks;
	if (iter->double_indirect_blocks > iter->fs->ext2_blocks_in_double_indirect)
		iter->double_indirect_blocks = iter->fs->ext2_blocks_in_double_indirect;

	iter->triple_indirect_blocks = data_blocks - direct_blocks - iter->single_indirect_blocks - iter->double_indirect_blocks;
	if (iter->triple_indirect_blocks > iter->fs->ext2_blocks_in_triple_indirect)
		iter->triple_indirect_blocks = iter->fs->ext2_blocks_in_triple_indirect;

	iter->fs = fs;

	*i = iter;
	return 0;

err:
	ext2_blkiter_free(iter);
	return err;
}

int ext2_blkiter_next(struct ext2_blkiter *i, int *blkno)
{
	int err;
	uint32_t block_size = i->fs->block_size;
	int fd = i->fs->fd;
	if (i->read < i->direct_blocks)
	{
		*blkno = i->i_block[i->read++];
		goto ret;
	}

	// Single Indirect blocks
	uint32_t single_indirect_index = i->read - i->direct_blocks;
	if (single_indirect_index < i->single_indirect_blocks)
	{
		if (single_indirect_index == 0)
		{
			off_t single_indirect_offset = (off_t)i->i_block[EXT2_SINGLE_INDRECT_IBLOCK] * block_size;
			if ((err = read_exact(fd, i->indirect_block_buffer, block_size, single_indirect_offset)) < 0)
				goto err;
		}
		uint32_t *blocks = i->indirect_block_buffer;
		*blkno = blocks[single_indirect_index];
		i->read++;
		goto ret;
	}
	
	// Double Indirect blocks
	uint32_t double_indirect_index = i->read - i->single_indirect_blocks - i->direct_blocks;
	if (double_indirect_index < i->double_indirect_blocks)
	{
		if (double_indirect_index == 0)
		{
			off_t double_indirect_offset = (off_t)i->i_block[EXT2_DOUBLE_INDRECT_IBLOCK] * block_size;
			if ((err = read_exact(fd, i->double_indirect_block_buffer, block_size, double_indirect_offset)) < 0)
				goto err;
		}

		if (double_indirect_index % i->fs->ext2_blocks_in_single_indirect == 0)
		{
			off_t indirect_block_offset = (off_t)i->double_indirect_block_buffer[double_indirect_index] * block_size;
			if ((err = read_exact(fd, i->indirect_block_buffer, block_size, indirect_block_offset)) < 0)
				goto err;
		}

		uint32_t *blocks = i->indirect_block_buffer;
		*blkno = blocks[double_indirect_index];
		i->read++;
		goto ret;
	}

	uint32_t triple_indirect_index = i->read - i->double_indirect_blocks - i->single_indirect_blocks - i->direct_blocks;
	if (triple_indirect_index < i->triple_indirect_blocks)
	{
		if (triple_indirect_index == 0)
		{
			off_t triple_indirect_offset = (off_t)i->i_block[EXT2_TRIPLE_INDRECT_IBLOCK] * block_size;
			if ((err = read_exact(fd, i->triple_indirect_block_buffer, block_size, triple_indirect_offset)) < 0)
				goto err;
		}
	}
ret:
	if (i->read == i->blocks_num)
		return 0;
	return 1;
}

void ext2_blkiter_free(struct ext2_blkiter *i)
{
	if (i == NULL)
		return;
	free(i->double_indirect_block_buffer);
	free(i->triple_indirect_block_buffer);
	free(i->indirect_block_buffer);
}
