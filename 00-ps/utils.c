#include <errno.h>
#include <stdint.h>
#include <ctype.h>
#include <fcntl.h>
#include <stddef.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <utils.h>

typedef  char** arr_of_str;

int is_numeric(const char *s)
{
    if (*s == '\0') {
        return 0;
    }

    for (; *s != '\0'; ++s) {
        if (!isdigit((unsigned char)*s)) {
            return 0;
        }
    }

    return 1;
}

int parse_nul_separated_strings(char* buf, size_t len, arr_of_str* result)
{
    size_t str_count = 0;

    for (size_t i = 0; i < len; ) {
        size_t n = strnlen(buf + i, len - i);

        str_count++;
        i += n + 1;
    }

    arr_of_str arr = calloc(str_count + 1, sizeof(*arr));
    if (arr == NULL) {
        return -1;
    }

    size_t j = 0;
    for (size_t i = 0; i < len; ) {
        size_t n = strnlen(buf + i, len - i);
        arr[j++] = (char *)(buf + i);
        i += n + 1;
    }

    arr[str_count] = NULL;
    *result = arr;
    return 0;
}

int read_and_parse(const char* filename, const int dir_fd, char** buf, arr_of_str* result) {
	int fd;
	size_t readed;
    int is_err = 0;

	fd = openat(dir_fd, filename, O_RDONLY);
	if (fd == -1) {
		return -1;
	}

	is_err = read_all(fd, buf, &readed);
	if (is_err == -1) {
        int saved_errno = errno;
		close(fd);
        errno = saved_errno;
		return -1;
	}
	
	is_err = parse_nul_separated_strings(*buf, readed, result);
	if (is_err != 0) {
		int saved_errno = errno;
		close(fd);
        errno = saved_errno;
		return -1;
	}
	close(fd);
	return 0;
}

int read_all(int fd, char **out_buf, size_t *out_len)
{
    size_t cap = 4096;
    size_t len = 0;
    char *buf = malloc(cap);

    if (buf == NULL) {
        return -1;
    }

    for (;;) {
        if (len == cap) {
            if (cap > SIZE_MAX / 2) {
                free(buf);
                errno = EOVERFLOW;
                return -1;
            }

            size_t new_cap = cap * 2;
            char *tmp = realloc(buf, new_cap);

            if (tmp == NULL) {
                free(buf);
                return -1;
            }

            buf = tmp;
            cap = new_cap;
        }

        ssize_t n = read(fd, buf + len, cap - len);

        if (n > 0) {
            len += (size_t)n;
        } else if (n == 0) {
            break;
        } else {
            int saved_errno = errno;
            free(buf);
            errno = saved_errno;
            return -1;
        }
    }

    *out_buf = buf;
    *out_len = len;
    return 0;
}