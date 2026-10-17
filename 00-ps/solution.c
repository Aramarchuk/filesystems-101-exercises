#include <ctype.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <dirent.h>
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

char** parse_nul_separated_strings(char* buf, size_t len, ssize_t* count)
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
    *count = argc;
    return argv;
}

void ps(void)
{
	int proc_fd = open("/proc", O_RDONLY | O_DIRECTORY);

	if (proc_fd == -1) {
    	perror("open /proc");
		exit(EXIT_FAILURE);
	}

	int iter_fd = dup(proc_fd);
	if (iter_fd == -1) {
		perror("dup");
		close(proc_fd);
		exit(EXIT_FAILURE);
	}

	DIR *dir = fdopendir(iter_fd);
	if (dir == NULL) {
		perror("fdopendir");
		close(iter_fd);
		close(proc_fd);
		exit(EXIT_FAILURE);
	}

	struct dirent *entry;

	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
		
		if (is_numeric(entry->d_name)) {
			int pid_fd = openat(proc_fd, entry->d_name, O_RDONLY | O_DIRECTORY);
			if (pid_fd == -1) {
				perror("pid_fd");
				continue;
			}
			char buf[MS];
			ssize_t written;
			written = readlinkat(pid_fd, "./exe", buf, MS);
			if (written >= MS - 1) {
				perror("buf overload");
				close(proc_fd);
				close(iter_fd);
				close(pid_fd);
				exit(EXIT_FAILURE);
			}
			buf[written] = '\0';

			char envs_buf[MS];
			int envs_fd;
			envs_fd = openat(pid_fd, "./environ", O_RDONLY);
			if (envs_fd == -1) {
				perror("environ_fd");
				continue;
			}

			ssize_t readed_env;
			ssize_t envs_out;
			char** envs_arr;
			readed_env = read(envs_fd, envs_buf, MS);
			if (readed_env == -1) {
				perror("read env error");
				continue;
			}
			if (readed_env == MS) {
				perror("bufer overload ");
				continue;
			}

			if (readed_env == 0) {
				perror("empty env");
				continue;
			}

			envs_arr = parse_nul_separated_strings(envs_buf, (size_t)(readed_env), &envs_out);

			
			char cmd_buf[MS];
			int cmdline_fd;
			cmdline_fd = openat(pid_fd, "./cmdline", O_RDONLY);
			if (cmdline_fd == -1) {
				perror("cmdline_fd");
				cmd_arr = NULL;
			}
			ssize_t readed_cmd;
			ssize_t argc_out;
			char** cmd_arr;
			readed_cmd = read(cmdline_fd, cmd_buf, MS);
			
			cmd_arr = parse_nul_separated_strings(cmd_buf, readed_cmd, &argc_out);
			if (cmd_arr == NULL) {
				perror("cmd_arr");
				continue;
			}

			report_process((pid_t)(atoi(entry->d_name)), buf, cmd_arr, envs_arr);

			free(envs_arr);
			free(cmd_arr);

		}
	}


}
