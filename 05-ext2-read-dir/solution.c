#include <errno.h>
#include <fs_ext2.h>
#include <linux/limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <solution.h>
#include <blkiter.h>
#include <utils.h>
#include <fs_malloc.h>


struct dir_entry_fields {
    __le32 inode;
    __le16 rec_len;
    uint8_t name_len;
    uint8_t file_type;
};

#define DIR_ENTRY_FIELDS_SIZE 8


static off_t get_offset(struct ext2_fs *fs, int block_nr)
{
	return (off_t)fs->block_size * block_nr;
}

static int process_dir_block(const char *buffer, size_t block_size)
{
    size_t offset = 0;
    struct dir_entry_fields de;
    uint32_t inode, rec_len;
    char name[NAME_MAX + 1];

    while (offset < block_size) {
        memcpy(&de, buffer + offset, sizeof(de));

        inode = le32toh(de.inode);
        rec_len = le16toh(de.rec_len);
        if (rec_len < sizeof(de) ||
            rec_len % 4 != 0 ||
            rec_len > block_size - offset ||
            de.name_len > rec_len - sizeof(de))
            return -EPROTO;

        if (inode != 0) {
            memcpy(name, buffer + offset + sizeof(de), de.name_len);
            name[de.name_len] = '\0';

            char type = de.file_type == 2 ? 'd' : 'f';
            report_file(inode, type, name);
        }

        offset += rec_len;
    }

    return 0;
}

int dump_dir(int img, int inode_nr)
{
    struct ext2_fs *fs = NULL;
    struct ext2_blkiter *iter = NULL;
    char *buf = NULL;
    int blkno;
    int err;

    int fs_img = dup(img);
    if (fs_img < 0)
        return -errno;

    err = ext2_fs_init(&fs, fs_img);
    if (err < 0)
        return err;

    err = ext2_blkiter_init(&iter, fs, inode_nr);
    if (err < 0)
        goto cleanup;

    buf = fs_xmalloc(fs->block_size);

    for (;;) {
        int next = ext2_blkiter_next(iter, &blkno);
        if (next < 0) {
            err = next;
            goto cleanup;
        }
        if (next == 0) {
            err = 0;
            break;
        }

        err = read_exact(fs->fd, buf, fs->block_size, get_offset(fs, blkno));
        if (err < 0)
            goto cleanup;

        err = process_dir_block(buf, fs->block_size);
        if (err < 0)
            goto cleanup;
    }

cleanup:
    fs_xfree(buf);
    ext2_blkiter_free(iter);
    ext2_fs_free(fs);
    return err;
}
