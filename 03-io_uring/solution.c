#include <assert.h>
#include <solution.h>
#include <liburing.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fs_malloc.h>

#define QD 4
#define BS (256 * 1024)

enum op_type {
    OP_READ,
    OP_WRITE
};

struct io_data {
	enum op_type op;
	off_t offset;
	size_t len;
	size_t len_base;
	char *buf;
	char *buf_base;
	off_t done;
};

static int get_file_size(int fd, off_t *size)
{
	struct stat st;

	if (fstat(fd, &st) == 0) {
		*size = st.st_size;
		return 0;
	}

	return -1;
}

static int setup_context(unsigned entries, struct io_uring *ring)
{
	int ret;

	ret = io_uring_queue_init(entries, ring, 0);
	if (ret < 0) {
		fprintf(stderr, "queue_init: %s\n", strerror(-ret));
		return -1;
	}

	return 0;
}

static void queue_prepped(struct io_uring *ring, struct io_data *data, int fd)
{
	struct io_uring_sqe *sqe;

	sqe = io_uring_get_sqe(ring);
	assert(sqe);

	if (data->op == OP_READ)
		io_uring_prep_read(sqe, fd, data->buf, data->len, data->offset);
	else
		io_uring_prep_write(sqe, fd, data->buf, data->len, data->offset);

	io_uring_sqe_set_data(sqe, data);
}

static int queue_read(struct io_uring *ringp, off_t buf_size, off_t offset, int in)
{
	struct io_uring_sqe *sqe;
	struct io_data *data;

	char *buf = fs_xmalloc(buf_size);
	data = fs_xmalloc(sizeof(*data));

	sqe = io_uring_get_sqe(ringp);
	if (!sqe) {
		free(data);
		free(buf);
		return 1;
	}

	data->op = OP_READ;
	data->offset = offset;

	data->buf = data->buf_base = buf;
	data->len = data->len_base = buf_size;

	io_uring_prep_read(sqe, in, data->buf, data->len, data->offset);
	io_uring_sqe_set_data(sqe, data);
	return 0;
}

static void queue_write(struct io_uring *ringp, struct io_data *data, int out)
{
	data->op = OP_WRITE;
	data->len = data->len_base;
	data->buf = data->buf_base;
	data->offset -= data->len_base - data->len;
	queue_prepped(ringp, data, out);
}

static int copy_file(struct io_uring *ringp, size_t insize, int in, int out) {
	unsigned reads_in_progress = 0, writes_in_progress = 0;
	unsigned num_blocks = insize / BS;
	unsigned read_offset = 0;
	unsigned had_reads = 0, got_completed = 0;
	int ret, fd;
	struct io_uring_cqe *cqe;
	struct io_data *data;

	while (
		read_offset < insize ||
		reads_in_progress ||
		writes_in_progress
	) {
		had_reads = reads_in_progress;
		while (read_offset < insize) {
			
			off_t this_size = BS;

			if (reads_in_progress + writes_in_progress >= QD)
				break;
			if (read_offset == num_blocks * BS)
				this_size = insize % BS;
			else if (!this_size)
				break;

			if (queue_read(ringp, this_size, read_offset, in))
				break;

			read_offset += this_size;
			reads_in_progress++;
		}
		if (had_reads != reads_in_progress) {
			ret = io_uring_submit(ringp);
			if (ret < 0) {
				fprintf(stderr, "io_uring_submit: %s\n", strerror(-ret));
				break;
			}
		}

		got_completed = 0;
		while (reads_in_progress || writes_in_progress) {
			if (!got_completed) {
				ret = io_uring_wait_cqe(ringp, &cqe);
				got_completed = 1;
			} else {
				ret = io_uring_peek_cqe(ringp, &cqe);
				if (ret == -EAGAIN) {
					cqe = NULL;
					ret = 0;
				}
			}
			if (ret < 0) {
				fprintf(stderr, "io_uring_peek_cqe: %s\n",
							strerror(-ret));
				return 1;
			}
			if (!cqe)
    			break;

			data = io_uring_cqe_get_data(cqe);


			if (data->op == OP_READ) fd = in; else fd = out;
			if (cqe->res < 0) {
				if (cqe->res == -EAGAIN) {
					queue_prepped(ringp, data, fd);
				} else {
					fprintf(stderr, "cqe failed: %s\n",
							strerror(-cqe->res));
					return 1;
				}
			} else if ((size_t)cqe->res != data->len) {
				/* Short read/write, adjust and requeue */
				data->buf += cqe->res;
				data->len -= cqe->res;
				data->offset += cqe->res;
				queue_prepped(ringp, data, fd);
			} else if (data->op == OP_READ) {
				queue_write(ringp, data, out);
				reads_in_progress--;
				writes_in_progress++;
			} else {
				free(data->buf_base);
				free(data);
				writes_in_progress--;
			}
			io_uring_cqe_seen(ringp, cqe);
			do {
    			ret = io_uring_submit(ringp);
			} while (ret == -EINTR || ret == -EAGAIN);
		}
	}
	
	// Cleanup in case of submit error
	while (reads_in_progress || writes_in_progress) {
		ret = io_uring_wait_cqe(ringp, &cqe);
		if (ret) {
			fprintf(stderr, "wait_cqe=%d\n", ret);
			return 1;
		}
		if (cqe->res < 0) {
			fprintf(stderr, "res=%d\n", cqe->res);
			return 1;
		}
		data = io_uring_cqe_get_data(cqe);
		if (data->op == OP_READ) reads_in_progress--; else writes_in_progress--;
		free(data->buf_base);
		free(data);
		io_uring_cqe_seen(ringp, cqe);
	}

	return 0;
}

int copy(int in, int out)
{
	struct io_uring ring;
    off_t insize;
    int ret;

	if (setup_context(QD, &ring))
        return 1;

    if (get_file_size(in, &insize))
        return 1;

    ret = copy_file(&ring, insize, in, out);

	io_uring_queue_exit(&ring);
    return ret;

}
