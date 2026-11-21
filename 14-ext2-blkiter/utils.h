#pragma once

#include <unistd.h>


int read_exact(int fd, void *buf, size_t size, off_t offset);

int write_exact(int fd, void *buf, size_t size);
