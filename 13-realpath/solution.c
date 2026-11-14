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
    int err;
    char *symlink = NULL;
    char res[PATH_MAX];
    char *cursor;
    int is_dir = 1;
    int fd;
    size_t symlink_capacity = 0;
    ssize_t nread;
    struct stat st;
    while (ctx->pending_len)
    {
        cursor = ctx->pending[ctx->pending_len - 1];
        ctx->pending_len--;
        fd = openat(ctx->fd, cursor, O_PATH | O_NOFOLLOW);
        if (fd < 0)
            goto error;
        if (fstat(fd, &st) < 0)
            goto error;
        if (S_ISLNK(st.st_mode)) {
            if ((nread = read_symlink(fd, &symlink, &symlink_capacity)) < 0)
            {
                goto error;
            }
            if (symlink[0] == '/') {
                int root_fd;
                ctx->resolved_len = 0;
                is_dir = 1;
                if ((root_fd = open("/", O_RDONLY | O_DIRECTORY | O_NOFOLLOW)) < 0)
                    goto error;
                close(ctx->fd);
                fprintf(stdout, "CLOSING ctx->fd in ISLINK = %d\n", ctx->fd);
                ctx->fd = root_fd;
            }
            close(fd);
            fprintf(stdout, "CLOSING fd in ISLINK = %d\n", fd);
            write_components(ctx, symlink, nread);
            continue;
        }
        if (strcmp(cursor, ".") == 0)
        {
            fprintf(stdout, "CLOSING fd in '.' = %d\n", fd);
            close(fd);
            continue;
        }
        if (strcmp(cursor, "..") == 0) 
        {
            if (ctx->resolved_len > 0)
                ctx->resolved_len--;
            close(ctx->fd);
            fprintf(stdout, "CLOSING ctx->fd in '..' = %d\n", ctx->fd);
            ctx->fd = fd;
            fd = -1;
            continue;
        }
        if (S_ISDIR(st.st_mode))
            is_dir = 1;
        else if (S_ISREG(st.st_mode))
            is_dir = 0;
        else {
            errno = ENOTDIR;
            goto error;
        }
        strcpy(ctx->resolved[ctx->resolved_len++], cursor);
        close(ctx->fd);
        fprintf(stdout, "CLOSING ctx->fd in REG|DIR = %d\n", ctx->fd);
        ctx->fd = fd;
        fd = -1;
    }
    construct_path(ctx, res, is_dir);
    report_path(res);
    goto cleanup;
    
error:
    err = errno;
    if (fd >= 0)
    {
        close(fd);
        fprintf(stdout, "CLOSING fd in ERROR= %d\n", fd);
    }
    construct_path(ctx, res, is_dir);
    report_error(res, cursor, err);
cleanup:
    fs_xfree(symlink);
    close(ctx->fd);
    fprintf(stdout, "CLOSING ctx->fd = %d\n", ctx->fd);
    return;
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
    init_pending(ctx, path);
    resolve_path(ctx);
    fprintf(stdout, "RETURNED FROM resolve_path\n");
cleanup:
    free(ctx);
}