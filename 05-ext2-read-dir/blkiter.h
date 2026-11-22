#pragma once

#include <stdint.h>
#include <fs_ext2.h>


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
	uint64_t read;

	uint64_t direct_blocks;
	uint64_t single_indirect_blocks;
	uint64_t double_indirect_blocks;
	uint64_t triple_indirect_blocks;

	__le32 *indirect_block_buffer;
	__le32 *double_indirect_block_buffer;
	__le32 *triple_indirect_block_buffer;
};


int ext2_fs_init(struct ext2_fs **fs, int fd);

void ext2_fs_free(struct ext2_fs *fs);

int ext2_blkiter_init(struct ext2_blkiter **i, struct ext2_fs *fs, int ino);

int ext2_blkiter_next(struct ext2_blkiter *i, int *blkno);

void ext2_blkiter_free(struct ext2_blkiter *i);