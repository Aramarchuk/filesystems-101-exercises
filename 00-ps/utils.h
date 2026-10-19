#pragma once

#include <stddef.h>

typedef char** arr_of_str;


int is_numeric(const char *s);

int parse_nul_separated_strings(char* buf, size_t len, arr_of_str* result);

int read_and_parse(const char* filename, const int dir_fd, char** buf, arr_of_str* result);

int read_all(int fd, char **out_buf, size_t *out_len);