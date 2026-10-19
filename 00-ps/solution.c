#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <string.h>

#include <solution.h>
#include <utils.h>

#define MS 8192


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
			if (readed == (ssize_t)sizeof(buf_exe)) {
				err = ENAMETOOLONG;
				snprintf(path, sizeof(path), "/proc/%s/exe", entry->d_name);
				report_error(path, err);
				close(pid_fd);
				continue;
}
			if (readed == -1) {
				err = errno;
				snprintf(path, sizeof(path), "/proc/%s/exe", entry->d_name);
				report_error(path, err);
				close(pid_fd);
				continue;

			}
			
			buf_exe[readed] = '\0';

			char* buf_envs = NULL;
			char* buf_cmd = NULL;
			arr_of_str envs_arr = NULL;
			arr_of_str cmd_arr = NULL;
			err = read_and_parse("./environ", pid_fd, &buf_envs, &envs_arr);
			if (err != 0) {
				snprintf(path, sizeof(path), "/proc/%s/environ", entry->d_name);
				report_error(path, errno);
				free(envs_arr);
				free(cmd_arr);
				free(buf_envs);
				free(buf_cmd);
				close(pid_fd);
				continue;
			}
			err = read_and_parse("./cmdline", pid_fd, &buf_cmd, &cmd_arr);
			if (err != 0) {
				snprintf(path, sizeof(path), "/proc/%s/cmdline", entry->d_name);
				report_error(path, errno);
				free(cmd_arr);
				free(envs_arr);
				free(buf_envs);
				free(buf_cmd);
				close(pid_fd);
				continue;
			}

			report_process((pid_t)(atoi(entry->d_name)), buf_exe, cmd_arr, envs_arr);

			free(envs_arr);
			free(cmd_arr);
			free(buf_envs);
			free(buf_cmd);
			close(pid_fd);
		}
	}
	close(proc_fd);
	closedir(dir);
}
