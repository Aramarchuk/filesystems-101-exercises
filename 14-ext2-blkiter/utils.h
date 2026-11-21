#pragma once

#include <unistd.h>


static int read_exact(int fd, void *buf, size_t size, off_t offset);

static int write_exact(int fd, void *buf, size_t size);