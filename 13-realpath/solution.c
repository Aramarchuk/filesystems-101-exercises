#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#include <solution.h>

#define MAX_SYMLINKS 40


typedef struct {
    const char *addr;
    size_t len;
} string_view;

typedef struct {
    char *resolved_path;
    char *pending_path;
    size_t symlink_count;
} context;

static void report_component_error(
    const char *parent,
    string_view child,
    int errno_code
) {
    char *name = strndup(child.addr, child.len);

    if (!name)
        return;

    report_error(parent, name, errno_code);

    free(name);
}

static string_view next_component(const char **path)
{
    while (**path == '/')
        (*path)++;

    string_view sv = {
        .addr = *path,
        .len = 0
    };

    while (**path != '/' && **path != '\0') {
        sv.len++;
        (*path)++;
    }

    return sv;
}

static int append_component(char **path, string_view component)
{
    size_t len = strlen(*path);
    size_t sep = len > 1 ? 1 : 0;

	if (len > SIZE_MAX - sep - component.len - 1) {
		errno = ENOMEM;
		return -1;
	}

    char *new_path = realloc(
        *path,
        len + sep + component.len + 1
    );

    if (!new_path)
        return -1;

    *path = new_path;

    if (sep)
        (*path)[len++] = '/';

    memcpy(*path + len, component.addr, component.len);
    (*path)[len + component.len] = '\0';

    return 0;
}

static void pop_component(char *path)
{
    size_t len = strlen(path);

    if (len <= 1)
        return;

    while (len > 1 && path[len - 1] != '/')
        len--;

    if (len > 1)
        len--;

    path[len] = '\0';
}

static int read_symlink(const char *path, char **target)
{
    size_t size = 128;

    while (1) {
        char *buf = malloc(size);

        if (!buf)
            return -1;

        ssize_t n = readlink(path, buf, size);

        if (n < 0) {
            int err = errno;
            free(buf);
            errno = err;
            return -1;
        }

        if ((size_t)n < size) {
            buf[n] = '\0';
            *target = buf;
            return 0;
        }

        free(buf);

        if (size > SIZE_MAX / 2) {
            errno = ENAMETOOLONG;
            return -1;
        }

        size *= 2;
    }
}

static char *join_paths(
    const char *target,
    const char *rest,
    int preserve_trailing_slash
) {
    size_t a = strlen(target);
    size_t b = strlen(rest);
    size_t sep = (b > 0 || preserve_trailing_slash) ? 1 : 0;

    if (a > SIZE_MAX - b - sep - 1) {
        errno = ENOMEM;
        return NULL;
    }

    char *result = malloc(a + sep + b + 1);

    if (!result)
        return NULL;

    memcpy(result, target, a);

    if (sep)
        result[a] = '/';

    memcpy(result + a + sep, rest, b);
    result[a + sep + b] = '\0';

    return result;
}


static int resolve_path(context *ctx)
{
restart:
	;
    const char *cursor = ctx->pending_path;

    while (1) {
        string_view component = next_component(&cursor);

        if (component.len == 0)
            return 0;

        const char *rest = cursor;

        while (*rest == '/')
            rest++;

        int has_more = *rest != '\0';
        int trailing_slash = !has_more && rest != cursor;
        int must_be_dir = has_more || trailing_slash;

        // "." — остаёмся в текущей директории.
        if (component.len == 1 &&
            component.addr[0] == '.') {
            continue;
        }

        // ".." — переходим к родительской директории.
        if (component.len == 2 &&
            component.addr[0] == '.' &&
            component.addr[1] == '.') {
            pop_component(ctx->resolved_path);
            continue;
        }

        // Формируем абсолютный путь до компонента.
        char *candidate = strdup(ctx->resolved_path);

        if (!candidate)
            return -1;

        if (append_component(&candidate, component) < 0) {
            free(candidate);
            return -1;
        }

        struct stat st;

        // Проверяем существование компонента.
        if (lstat(candidate, &st) < 0) {
            int err = errno;

            report_component_error(
                ctx->resolved_path,
                component,
                err
            );

            free(candidate);
            errno = err;
            return -1;
        }

        // Обработка symbolic link.
        if (S_ISLNK(st.st_mode)) {
            if (++ctx->symlink_count > MAX_SYMLINKS) {
                report_component_error(
                    ctx->resolved_path,
                    component,
                    ELOOP
                );

                free(candidate);
                errno = ELOOP;
                return -1;
            }

            char *target;

            if (read_symlink(candidate, &target) < 0) {
                int err = errno;

                if (err != ENOMEM) {
                    report_component_error(
                        ctx->resolved_path,
                        component,
                        err
                    );
                }

                free(candidate);
                errno = err;
                return -1;
            }

            free(candidate);

            // Подставляем target вместо symlink.
            char *new_pending = join_paths(
                target,
                rest,
                trailing_slash
            );

            if (!new_pending) {
                free(target);
                return -1;
            }

            // Абсолютный target меняет базовую директорию.
            if (target[0] == '/') {
                ctx->resolved_path[0] = '/';
                ctx->resolved_path[1] = '\0';
            }

            free(target);
            free(ctx->pending_path);

            ctx->pending_path = new_pending;

            // Начинаем обработку нового пути сначала.
            goto restart;
        }

        // Промежуточный компонент обязан быть директорией.
        if (must_be_dir && !S_ISDIR(st.st_mode)) {
            report_component_error(
                ctx->resolved_path,
                component,
                ENOTDIR
            );

            free(candidate);
            errno = ENOTDIR;
            return -1;
        }

        free(candidate);

        if (append_component(
                &ctx->resolved_path, component) < 0) {
            return -1;
        }
    }
}

void abspath(const char *path)
{
    context ctx = {
        .resolved_path = strdup("/"),
        .pending_path = strdup(path),
        .symlink_count = 0,
    };

    if (!ctx.resolved_path || !ctx.pending_path) {
        free(ctx.resolved_path);
        free(ctx.pending_path);
        return;
    }

    if (resolve_path(&ctx) != 0) {
        free(ctx.resolved_path);
        free(ctx.pending_path);
        return;
    }

    struct stat st;
    if (stat(ctx.resolved_path, &st) == -1) {
        free(ctx.resolved_path);
        free(ctx.pending_path);
        return;
    }

    if (S_ISDIR(st.st_mode)) {
        size_t len = strlen(ctx.resolved_path);

        if (ctx.resolved_path[len - 1] != '/') {
            char *tmp = realloc(ctx.resolved_path, len + 2);
            if (!tmp) {
                free(ctx.resolved_path);
                free(ctx.pending_path);
                return;
            }

            ctx.resolved_path = tmp;
            ctx.resolved_path[len] = '/';
            ctx.resolved_path[len + 1] = '\0';
        }
    }

    report_path(ctx.resolved_path);

    free(ctx.resolved_path);
    free(ctx.pending_path);
}