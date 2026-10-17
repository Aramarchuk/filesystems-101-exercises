#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>

#include <string.h>


#include <solution.h>

#define MS 8192

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

char** parse_nul_separated_strings(char* buf, size_t len)
{
    size_t argc = 0;

    for (size_t i = 0; i < len; ) {
        size_t n = strnlen(buf + i, len - i);
        if (n == len - i) {
            return NULL;
        }

        argc++;
        i += n + 1;
    }

    char **argv = calloc(argc + 1, sizeof(*argv));
    if (argv == NULL) {
        return NULL;
    }

    size_t j = 0;
    for (size_t i = 0; i < len; ) {
        size_t n = strnlen(buf + i, len - i);
        argv[j++] = (char *)(buf + i);
        i += n + 1;
    }

    argv[argc] = NULL;
    return argv;
}

char** read_and_parse(const char* filename, const int dir_fd, char* buf, int* err) {
	int fd;
	ssize_t readed;
	char** arr;

	fd = openat(dir_fd, filename, O_RDONLY);
	if (fd == -1) {
		*err = errno;
		return NULL;
	}

	readed = read(fd, buf, MS);

	if (readed == -1) {
		*err = errno;
		close(fd);
		return NULL;
	}

	if (readed == MS) {
		errno = ENOMEM;
		*err = errno;
		close(fd);
		return NULL;
	}
	
	arr = parse_nul_separated_strings(buf, (size_t)(readed));
	if (arr == NULL) {
		errno = ENOMEM;
		*err = errno;
		close(fd);
		return NULL;
	}
	close(fd);
	return arr;
}

void ps(void)
{
	int proc_fd = open("/proc", O_RDONLY | O_DIRECTORY);
	char path[PATH_MAX];
	int err = 0;

	if (proc_fd == -1) {
		err = errno;
		snprintf(path, sizeof(path), "/proc");
    	report_error(path, err);
		exit(EXIT_FAILURE);
	}

	int iter_fd = dup(proc_fd);
	if (iter_fd == -1) {
		err = errno;
		snprintf(path, sizeof(path), "/proc");
    	report_error(path, err);
		close(proc_fd);
		exit(EXIT_FAILURE);
	}

	DIR *dir = fdopendir(iter_fd);
	if (dir == NULL) {
		err = errno;
		snprintf(path, sizeof(path), "/proc");
    	report_error(path, err);
		close(iter_fd);
		close(proc_fd);
		exit(EXIT_FAILURE);
	}

	struct dirent *entry;
	for (;;) {
		errno = 0;
		err = 0;
		entry = readdir(dir);

		if (entry == NULL) {
			if (errno != 0) {
				snprintf(path, sizeof(path), "/proc");
				report_error(path, errno);
			}
			break;
		}

		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
		if (is_numeric(entry->d_name)) {
			int pid_fd = openat(proc_fd, entry->d_name, O_RDONLY | O_DIRECTORY);
			if (pid_fd == -1) {
				err = errno;
				snprintf(path, sizeof(path), "/proc/%s", entry->d_name);
				report_error(path, err);
				continue;
			}
			char buf_exe[MS];
			ssize_t readed;
			readed = readlinkat(pid_fd, "./exe", buf_exe, MS);
			if (readed >= MS - 1) {
				// err = errno;
				// snprintf(path, sizeof(path), "/proc/%s/exe", entry->d_name);
				// report_error(path, err);
				close(proc_fd);
				close(iter_fd);
				close(pid_fd);
				exit(EXIT_FAILURE);
			}
			if (readed == -1) {
				err = errno;
				snprintf(path, sizeof(path), "/proc/%s/exe", entry->d_name);
				report_error(path, err);
				close(pid_fd);
				continue;

			}
			
			buf_exe[readed] = '\0';

			char buf_envs[MS], buf_cmd[MS];
			char** envs_arr = read_and_parse("./environ", pid_fd, buf_envs, &err);
			if (err != 0) {
				snprintf(path, sizeof(path), "/proc/%s/environ", entry->d_name);
				report_error(path, err);
				free(envs_arr);
				close(pid_fd);
				continue;
			}
			char** cmd_arr = read_and_parse("./cmdline", pid_fd, buf_cmd, &err);
			if (err != 0) {
				snprintf(path, sizeof(path), "/proc/%s/cmdline", entry->d_name);
				report_error(path, err);
				free(cmd_arr);
				free(envs_arr);
				close(pid_fd);
				continue;
			}

			report_process((pid_t)(atoi(entry->d_name)), buf_exe, cmd_arr, envs_arr);

			free(envs_arr);
			free(cmd_arr);
			close(pid_fd);
		}
	}
	close(iter_fd);
	closedir(dir);
}
