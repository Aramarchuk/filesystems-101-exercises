#include <fcntl.h>
#include <errno.h>
#include <solution.h>

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static int myopen(const char *filename, struct fuse_file_info *fi) {
	(void)fi;
	if (strcmp(filename, "/hello") != 0) {
		return -ENOENT;
	}
	return 0;
}


int mygetattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
	(void)fi;
	memset(stbuf, 0, sizeof(struct stat));
	if (strcmp(path, "/hello") == 0) {
		stbuf->st_mode = S_IFREG | S_IRUSR | S_IRGRP | S_IROTH;
		stbuf->st_nlink = 1;
		stbuf->st_size = 64;
	} else if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | S_IXUSR | S_IXGRP | S_IXOTH | S_IREAD | S_IRGRP | S_IROTH;
		stbuf->st_nlink = 2;
	} else {
		return -ENOENT;
	}
	return 0;
}

static int myreaddir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi,
			 enum fuse_readdir_flags flags) {
	(void)offset;
	(void)flags;
	(void)fi;

	if (strcmp(path, "/") != 0) return -ENOENT;

	filler(buf, ".", NULL, 0, 0);
	filler(buf, "..", NULL, 0, 0);
	filler(buf, "hello", NULL, 0, 0);

	return 0;
}

static int myread(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi) {
	size_t len;
	(void) fi;
	if(strcmp(path, "/hello") != 0)
		return -ENOENT;

	pid_t pid = fuse_get_context()->pid;
	char contents[64];
	snprintf(contents, sizeof(contents), "hello, %d\n", pid);
	len = strlen(contents);
	if ((size_t)offset < len) {
		if (offset + size > len)
			size = len - offset;
		memcpy(buf, contents + offset, size);
	} else
		size = 0;

	return size;
}

static int mywrite(const char *path, const char *buf, size_t size,
                   off_t offset, struct fuse_file_info *fi) {
    (void)path;
    (void)buf;
    (void)size;
    (void)offset;
    (void)fi;
    return -EROFS;
}

static int mymkdir(const char *path, mode_t mode) {
    (void)path;
    (void)mode;
    return -EROFS;
}

static int myunlink(const char *path) {
    (void)path;
    return -EROFS;
}

static int myrmdir(const char *path) {
    (void)path;
    return -EROFS;
}

static int myrename(const char *from, const char *to, unsigned int flags) {
    (void)from;
    (void)to;
    (void)flags;
    return -EROFS;
}

static int mycreate(const char *path, mode_t mode,
                    struct fuse_file_info *fi) {
    (void)path;
    (void)mode;
    (void)fi;
    return -EROFS;
}

static int mytruncate(const char *path, off_t size,
                      struct fuse_file_info *fi) {
    (void)path;
    (void)size;
    (void)fi;
    return -EROFS;
}

static int mysymlink(const char *target, const char *linkpath) {
    (void)target;
    (void)linkpath;
    return -EROFS;
}

static int mylink(const char *from, const char *to) {
    (void)from;
    (void)to;
    return -EROFS;
}

static int mychmod(const char *path, mode_t mode,
                   struct fuse_file_info *fi) {
    (void)path;
    (void)mode;
    (void)fi;
    return -EROFS;
}

static int mychown(const char *path, uid_t uid, gid_t gid,
                   struct fuse_file_info *fi) {
    (void)path;
    (void)uid;
    (void)gid;
    (void)fi;
    return -EROFS;
}

static int myutimens(const char *path, const struct timespec tv[2],
                     struct fuse_file_info *fi) {
    (void)path;
    (void)tv;
    (void)fi;
    return -EROFS;
}

static const struct fuse_operations hellofs_ops = {
    .open     = myopen,
    .getattr  = mygetattr,
    .readdir  = myreaddir,
    .read     = myread,

    .write    = mywrite,
    .mkdir    = mymkdir,
    .unlink   = myunlink,
    .rmdir    = myrmdir,
    .rename   = myrename,
    .create   = mycreate,
    .truncate = mytruncate,
    .symlink  = mysymlink,
    .link     = mylink,
    .chmod    = mychmod,
    .chown    = mychown,
    .utimens  = myutimens
};

int helloworld(const char *mntp)
{
	char *argv[] = {"exercise", "-f", (char *)mntp, NULL};
	return fuse_main(3, argv, &hellofs_ops, NULL);
}
