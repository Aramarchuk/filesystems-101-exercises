#include <errno.h>
#include <endian.h>
#include <linux/limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <solution.h>
#include <ext2_blkiter.h>
#include <fs_malloc.h>


struct dir_entry_fields {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t name_len;
    uint8_t file_type;
};

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

    int fs_fd = dup(img);
    if (fs_fd < 0)
        return -errno;

    err = ext2_fs_init(&fs, fs_fd);
    if (err < 0)
        return err;

    err = ext2_blkiter_init(&iter, fs, inode_nr);
    if (err < 0)
        goto cleanup;

    uint32_t block_size = ext2_fs_block_size(fs);
    buf = fs_xmalloc(block_size);

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

        err = ext2_fs_read_block(fs, blkno, buf);
        if (err < 0)
            goto cleanup;

        err = process_dir_block(buf, block_size);
        if (err < 0)
            goto cleanup;
    }

cleanup:
    fs_xfree(buf);
    ext2_blkiter_free(iter);
    ext2_fs_free(fs);
    return err;
}
