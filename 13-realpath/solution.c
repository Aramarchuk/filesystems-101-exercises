#include <stdint.h>
#include <stdio.h>
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
#define INITIAL_CONTEXT_LENGTH 64


typedef struct {
    char *resolved;
    char *pending;
    int resolved_len, pending_len;
    ssize_t resolved_size, pending_size;
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

        if (ctx->pending_len >= ctx->pending_size)
        {
            ctx->pending_size *= 2;
            ctx->pending = fs_xrealloc(ctx->pending, ctx->pending_size * COMP_LEN);
        }

        char *dst = &ctx->pending[(ctx->pending_len++) * COMP_LEN];
        memcpy(dst, p, len);
        dst[len] = '\0';
    }

    return;
}

static void write_resolved_component(context *ctx, const char *comp)
{
    const char *cur = comp;
    size_t len = 0;
    while ((*cur++) != '\0') {}
    len = cur - comp;
    if (ctx->resolved_len >= ctx->resolved_size)
    {
        ctx->resolved_size *= 2;
        ctx->resolved = fs_xrealloc(ctx->resolved, ctx->resolved_size * COMP_LEN);
    }
    char *dst = ctx->resolved + (ctx->resolved_len++) * COMP_LEN;
    memcpy(dst, comp, len);
}


static void init_pending(context *ctx, const char *path)
{
    write_components(ctx, path, strlen(path));
}


static ssize_t read_symlink(int fd,
                            char **buf, size_t *capacity)
{
    ssize_t nread;

    while (1) {
        if (*capacity < 1024)
            *capacity = 1024;

        *capacity *= 2;
        *buf = fs_xrealloc(*buf, *capacity);

        nread = readlinkat(fd, "", *buf, *capacity - 1);

        if (nread < 0)
            return -errno;

        if ((size_t)nread < *capacity - 1)
            break;
    }

    (*buf)[nread] = '\0';
    return nread;
}

void construct_path(context *ctx, char *res, int is_dir)
{
    char *cur = res;
    for (int i = 0; i < ctx->resolved_len; i++) {
        *(cur++) = '/';
        char *src = &ctx->resolved[i * COMP_LEN];
        char *len_check = src;
        while ((*len_check++) != '\0') {}
        int len = len_check - src - 1;
        memcpy(cur, src, len);
        cur += len;
    }
    if (is_dir) {
        *(cur++) = '/';
    }
    *cur = '\0';
    return;
}

char* get_pending_component(context *ctx)
{
    char *dst = ctx->pending + (--ctx->pending_len) * COMP_LEN;
    return dst;
}


static void resolve_path(context *ctx)
{
    int err;
    char *symlink = NULL;
    char res[PATH_MAX];
    char *cursor;
    int is_dir = 0;
    int fd;
    size_t symlink_capacity = 0;
    ssize_t nread;
    while (ctx->pending_len)
    {
        is_dir = 0;
        cursor = get_pending_component(ctx);
        fd = openat(ctx->fd, cursor, O_PATH | O_NOFOLLOW | O_DIRECTORY);
        if (fd < 0)
        {
            if (errno == ENOTDIR)
            {
                if ((fd = openat(ctx->fd, cursor, O_PATH | O_NOFOLLOW)) < 0)
                    goto error;
                if ((nread = read_symlink(fd, &symlink, &symlink_capacity)) > 0)
                {
                    if (++ctx->symlink_count > MAX_SYMLINKS)
                    {
                        errno = ELOOP;
                        is_dir = 0;
                        goto error;
                    }
                    if (symlink[0] == '/')
                    {
                        int root_fd;
                        ctx->resolved_len = 0;
                        is_dir = 1;
                        if ((root_fd = open("/", O_RDONLY | O_DIRECTORY | O_NOFOLLOW)) < 0)
                            goto error;
                        close(ctx->fd);
                        ctx->fd = root_fd;
                    }
                    close(fd);
                    write_components(ctx, symlink, nread);
                    continue;
                }
                else
                {
                    is_dir = 0;
                    if (nread != -ENOENT)
                    {
                        goto error;
                    }
                }
            }
            else
            {
                goto error;
            }
        }
        if (strcmp(cursor, ".") == 0)
        {
            close(fd);
            continue;
        }
        if (strcmp(cursor, "..") == 0) 
        {
            if (ctx->resolved_len > 0)
                ctx->resolved_len--;
            close(ctx->fd);
            ctx->fd = fd;
            continue;
        }
        write_resolved_component(ctx, cursor);
        close(ctx->fd);
        ctx->fd = fd;
    }
    construct_path(ctx, res, is_dir);
    report_path(res);
    goto cleanup;

error:
    err = errno;
    if (fd >= 0)
    {
        close(fd);
    }
    construct_path(ctx, res, is_dir);
    report_error(res, cursor, err);
cleanup:
    fs_xfree(symlink);
    return;
}

void init_context(context *ctx, const char *path)
{
    ctx->resolved_size = INITIAL_CONTEXT_LENGTH;
    ctx->pending_size = INITIAL_CONTEXT_LENGTH;
    ctx->resolved = fs_xmalloc(ctx->resolved_size * COMP_LEN);
    ctx->pending = fs_xmalloc(ctx->pending_size * COMP_LEN);
    init_pending(ctx, path);

}

void abspath(const char *path)
{
    int fd, err;
    context *ctx = fs_xzalloc(sizeof(context));
    fd = open("/", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (fd < 0)
    {
        err = errno;
        report_error("", "/", err);
        goto cleanup;
    }
    ctx->fd = fd;
    init_context(ctx, path);
    resolve_path(ctx);
cleanup:
    close(ctx->fd);
    free(ctx->pending);
    free(ctx->resolved);
    free(ctx);
}