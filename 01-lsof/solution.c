#include <errno.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <utils.h>
#include <solution.h>

void process_pid(const char* pid) {
	int err = 0;
	char path[PATH_MAX];
	snprintf(path, sizeof(path), "/proc/%s/fd", pid);
	DIR *fd_dir = opendir(path);
	if (fd_dir == NULL) {
		err = errno;
		report_error(path, err);
		return;
	}
	struct dirent *fd_entry;
	for (;;) {
		errno = 0;
		fd_entry = readdir(fd_dir);
		if (fd_entry == NULL) {
			if (errno != 0) {
				err = errno;
				snprintf(path, sizeof(path), "/proc/%s/fd", pid);
				report_error(path, err);
			}
			break;
		}
		if (strcmp(fd_entry->d_name, ".") == 0 || strcmp(fd_entry->d_name, "..") == 0) continue;
		char fd_path[PATH_MAX];
		char fd_buf[PATH_MAX];
		snprintf(fd_path, sizeof(fd_path), "/proc/%s/fd/%s", pid, fd_entry->d_name);
		ssize_t readed = readlinkat(dirfd(fd_dir), fd_entry->d_name, fd_buf, PATH_MAX);
		if (readed == -1) {
			report_error(fd_path, errno);
			continue;
		}
		if (readed == PATH_MAX) {
			errno = ENAMETOOLONG;
			report_error(fd_path, errno);
			continue;
		}
		fd_buf[readed] = '\0';
		report_file(fd_buf);
	}
	closedir(fd_dir);
	return;
}

void lsof(void)
{
	char path[PATH_MAX];
	int err = 0;
	DIR *dir = opendir("/proc");
	if (dir == NULL) {
		err = errno;
		snprintf(path, sizeof(path), "/proc");
    	report_error(path, err);
		return;
	}

	struct dirent *entry;
	for (;;) {
		errno = 0;
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
			process_pid(entry->d_name);
		}
	}
	closedir(dir);
}
