.PHONY: build test

SRC_SOLUTION := $(wildcard *.c)
HDR_SOLUTION := $(wildcard *.h)
SRC_EXT2_BLKITER := ../14-ext2-blkiter/solution.c
HDR_EXT2_BLKITER := ../14-ext2-blkiter/ext2_blkiter.h ../14-ext2-blkiter/fs_ext2.h

SRC_STDLIB := $(wildcard ../stdlib/*.c)
HDR_STDLIB := $(wildcard ../stdlib/*.h)

test: build
	./a.out

build: a.out

a.out: $(SRC_SOLUTION) $(HDR_SOLUTION) $(SRC_EXT2_BLKITER) $(HDR_EXT2_BLKITER) $(SRC_STDLIB) $(HDR_STDLIB)
	gcc \
		-std=gnu11 -Wall -Wextra -Werror \
		-I. -I../stdlib -I../14-ext2-blkiter \
		-D_GNU_SOURCE \
		-g -Og \
		$(SRC_SOLUTION) $(SRC_EXT2_BLKITER) $(SRC_STDLIB)
