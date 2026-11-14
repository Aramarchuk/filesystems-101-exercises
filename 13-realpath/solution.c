#include <asm-generic/errno-base.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <linux/limits.h>
#include <fs_malloc.h>
#include <fcntl.h>

#include <solution.h>

#define MAX_SYMLINKS 40
#define COMP_LEN NAME_MAX + 1


typedef struct {
    char resolved[PATH_MAX][COMP_LEN];
    char pending[PATH_MAX][COMP_LEN];
    int resolved_len, pending_len;
    size_t symlink_count;
    int fd;
} context;

static void write_components(context *ctx, const char *comps, ssize_t comps_len)
{
    const char *p = comps + comps_len;
    const char *start = comps;

    while (p > start) {
        while (p > start && p[-1] == '/')
            p--;

        if (p == start)
            break;

        const char *component_end = p;

        while (p > start && p[-1] != '/')
            p--;

        size_t len = component_end - p;

        // if (ctx->pending_len >= PATH_MAX || len >= COMP_LEN) {
        //     errno = ENAMETOOLONG;
        //     goto error;
        // }

        char *dst = ctx->pending[ctx->pending_len++];
        memcpy(dst, p, len);
        dst[len] = '\0';
    }

    return;
}


static void next_component(char *dest, const char **path)
{
    while (**path == '/')
        (*path)++;

    while (**path != '/' && **path != '\0') {
       *dest = **path;
        (*path)++;
        dest++;
    }
    *dest = '\0';
}


static void init_pending(context *ctx, const char *path) {
    while (*path != '\0') {
        next_component(ctx->pending[ctx->pending_len++], &path);
    }
}

static ssize_t read_symlink(context *ctx, const char *path,
                            char **buf, size_t *capacity)
{
    ssize_t nread;

    while (1) {
        if (*capacity < 1024)
            *capacity = 1024;

        *capacity *= 2;
        *buf = fs_xrealloc(*buf, *capacity);

        nread = readlinkat(ctx->fd, path, *buf, *capacity - 1);

        if (nread < 0)
            return -1;

        if ((size_t)nread < *capacity - 1)
            break;
    }

    (*buf)[nread] = '\0';
    return nread;
}

void construct_path(context *ctx, char *res, int is_dir) {
    char *cur = res;
    for (int i = 0; i < ctx->resolved_len; i++) {
        *(cur++) = '/';
        cur = stpcpy(cur, ctx->resolved[i]);

    }
    if (is_dir) {
        *(cur++) = '/';
        *cur = '\0';
    }
    return;
}


static void resolve_path(context *ctx)
{
    int err, fd;
    char *symlink = NULL;
    char res[PATH_MAX];
    char *cursor = "/";
    int is_dir = 1;
    size_t symlink_capacity = 0;
    ssize_t nread;
    fd = open("/", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (fd < 0) 
        goto error;
    ctx->fd = fd;
    while (ctx->pending_len)
    {
        cursor = ctx->pending[ctx->pending_len - 1];
        ctx->pending_len--;
        fd = openat(ctx->fd, cursor, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        if (fd == -1) {
            if (errno == ENOTDIR)
            {
                // trying to process symlink
                if ((nread = read_symlink(ctx, cursor, &symlink, &symlink_capacity)) < 0)
                    goto error;
                if (symlink[0] == '/') {
                    ctx->resolved_len = 0;
                    close(ctx->fd);
                    if ((fd = openat(ctx->fd, cursor, O_RDONLY | O_DIRECTORY | O_NOFOLLOW)) < 0)
                        goto error;
                    ctx->fd = fd;
                }
                write_components(ctx, symlink, nread);
                continue;
            }
            else
            {
                is_dir = 0;
                if ((fd = openat(ctx->fd, cursor, O_RDONLY | O_NOFOLLOW)) < 0)
                    goto error;
            }
        }
        if (strcmp(cursor, ".") == 0)
            continue;
        if (strcmp(cursor, "..") == 0) 
        {
            ctx->resolved_len--;
            continue;
        }
        is_dir = 1;
        memcpy(ctx->resolved[ctx->resolved_len++], cursor, COMP_LEN);
        close(ctx->fd);
        ctx->fd = fd;
    }
    construct_path(ctx, res, is_dir);
    report_path(res);
    goto cleanup;
    
error:
    err = errno;
    construct_path(ctx, res, is_dir);
    report_error(res, cursor, err);
cleanup:
    if (ctx->fd )close(ctx->fd);
    fs_xfree(symlink);
    return;
}

void abspath(const char *path)
{
    context *ctx = fs_xzalloc(sizeof(context));
    init_pending(ctx, path);
    resolve_path(ctx);

    if (ctx->fd) close(ctx->fd);
    free(ctx);
}