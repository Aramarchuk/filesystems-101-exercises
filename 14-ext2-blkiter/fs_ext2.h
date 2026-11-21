#pragma once

#include <assert.h>
#include <linux/types.h>

#define EXT2_N_BLOCKS 15
#define EXT2_DIRECT_BLOCKS 12
#define EXT2_SINGLE_INDRECT_IBLOCK 12
#define EXT2_DOUBLE_INDRECT_IBLOCK 13
#define EXT2_TRIPLE_INDRECT_IBLOCK 14

#define EXT2_SUPERBLOCK_OFFSET 1024


struct ext2_super_block {
__le32 s_inodes_count; /* Inodes count */
__le32 s_blocks_count; /* Blocks count */
__le32 s_r_blocks_count; /* Reserved blocks count */
__le32 s_free_blocks_count; /* Free blocks count */
__le32 s_free_inodes_count; /* Free inodes count */
__le32 s_first_data_block; /* First Data Block */
__le32 s_log_block_size; /* Block size */
__le32 s_log_frag_size; /* Fragment size */
__le32 s_blocks_per_group; /* # Blocks per group */
__le32 s_frags_per_group; /* # Fragments per group */
__le32 s_inodes_per_group; /* # Inodes per group */
__le32 s_mtime; /* Mount time */
__le32 s_wtime; /* Write time */
__le16 s_mnt_count; /* Mount count */
__le16 s_max_mnt_count; /* Maximal mount count */
__le16 s_magic; /* Magic signature */
__le16 s_state; /* File system state */

__le16 s_errors; /* Behaviour when detecting errors */
__le16 s_minor_rev_level; /* minor revision level */
__le32 s_lastcheck; /* time of last check */
__le32 s_checkinterval; /* max. time between checks */
__le32 s_creator_os; /* OS */
__le32 s_rev_level; /* Revision level */
__le16 s_def_resuid; /* Default uid for reserved blocks */
__le16 s_def_resgid; /* Default gid for reserved blocks */
__le32 s_first_ino; /* First non-reserved inode */
__le16 s_inode_size; /* size of inode structure */
__le16 s_block_group_nr; /* block group # of this sb */
__le32 s_feature_compat; /* compatible features */
__le32 s_feature_incompat; /* incompatible features */
__le32 s_feature_ro_compat; /* readonly-compatible features */
__u8 s_uuid[16]; /* 128-bit uuid for volume */
char s_volume_name[16]; /* volume name */
char s_last_mounted[64]; /* directory where last mounted */
__le32 s_algorithm_usage_bitmap; /* For compression */

__le32 s_pad[205];
};


struct ext2_block_group_header {
    __le32 bg_block_bitmap;
    __le32 bg_indode_bitmap;
    __le32 bg_inode_table;
    __le16 bg_free_blocks_count;
    __le16 bg_free_inode_count;;
    __le16 bg_used_dirs_count;
    __le16 bg_pad;
    __le32 bg_reserved[3];
};

struct ext2_inode {

__le16 i_mode; /* File mode */
__le16 i_uid; /* Low 16 bits of Owner Uid */
__le32 i_size; /* Size in bytes */
__le32 i_atime; /* Access time */
__le32 i_ctime; /* Creation time */
__le32 i_mtime; /* Modification time */
__le32 i_dtime; /* Deletion Time */
__le16 i_gid; /* Low 16 bits of Group Id */
__le16 i_links_count; /* Links count */
__le32 i_blocks; /* Blocks count */
__le32 i_flags; /* File flags */
__le32 i_osd1;
__le32 i_block[EXT2_N_BLOCKS];/* Pointers to blocks */
__le32 i_generation; /* File version (for NFS) */
__le32 i_file_acl; /* File ACL */
__le32 i_dir_acl; /* Directory ACL */
__le32 i_faddr; /* Fragment address */
__u8 i_osd2[12];

};

_Static_assert(sizeof(struct ext2_super_block) == 1024, "unexpected ext2_super_block size");
_Static_assert(sizeof(struct ext2_block_group_header) == 32, "unexpected ext_2_block_group_header size");
_Static_assert(sizeof(struct ext2_inode) == 128, "unexpected inode size");