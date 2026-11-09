#include "io_uring.h"
#include <asm-generic/errno-base.h>
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
	off_t is_busy;
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

static int queue_read(struct io_uring *ringp, off_t buf_size, struct io_data *data, off_t offset, int in)
{
	struct io_uring_sqe *sqe;

	sqe = io_uring_get_sqe(ringp);
	if (!sqe) {
		return 1;
	}

	data->op = OP_READ;
	data->offset = offset;

	data->buf = data->buf_base;
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

static int process_cqe(struct io_uring *ringp, struct io_uring_cqe *cqe, int in, int out, unsigned *pending) {
	struct io_data *data;
	int fd;

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
	} else {
		data->is_busy = 0;
		(*pending)--;
	}
	io_uring_cqe_seen(ringp, cqe);
	return 0;
}

static void cleanup(struct io_uring *ringp, struct io_data *datap, char *bufsp) {
	io_uring_queue_exit(ringp);
	free(datap);
	free(bufsp);
}

static int copy_file(struct io_uring *ringp, size_t insize, int in, int out) {
	unsigned pending = 0;
	unsigned num_blocks = insize / BS;
	unsigned read_offset = 0;
	unsigned had_reads_pending = 0;
	int ret;
	struct io_uring_cqe *cqe;
	struct io_data *datap = fs_xzalloc(sizeof(struct io_data) * QD);
	char *buffers = fs_xmalloc(BS * QD);

	for (int i = 0; i < QD; i++) {
		datap[i].buf_base = buffers + i * BS;
	}

	while (
		read_offset < insize ||
		pending
	) {
		had_reads_pending = pending;
		while (read_offset < insize) {
			
			off_t this_size = BS;

			if (pending >= QD)
				break;
			if (read_offset == num_blocks * BS)
				this_size = insize % BS;
			else if (!this_size)
				break;
			int i = 0;
			while (datap[i].is_busy) i++;
			datap[i].is_busy = 1;
			if (queue_read(ringp, this_size, &datap[i], read_offset,  in))
				break;

			read_offset += this_size;
			pending++;
		}
		if (had_reads_pending != pending) {
			ret = io_uring_submit(ringp);
			if (ret < 0) {
				fprintf(stderr, "io_uring_submit: %s\n", strerror(-ret));
				break;
			}
		}
		ret = io_uring_wait_cqe(ringp, &cqe);
		if (ret < 0) {
			fprintf(stderr, "io_uring_wait_cqe: %s\n", strerror(-ret));
			cleanup(ringp, datap, buffers);
			return 1;
		}

		while (1) {
			ret = process_cqe(ringp, cqe, in, out, &pending);
			if (ret) {
				cleanup(ringp, datap, buffers);
				return 1;
			}
			ret = io_uring_peek_cqe(ringp, &cqe);

			if (ret == -EAGAIN)
				break;

			if (ret < 0) {
				fprintf(stderr, "io_uring_peek_cqe: %s\n", strerror(-ret));
				cleanup(ringp, datap, buffers);
				return 1;
			}
		}

		do {
			ret = io_uring_submit(ringp);
		} while (ret == -EINTR);
		if (ret < 0) {
			fprintf(stderr, "io_uring_submit: %s\n", strerror(-ret));
			cleanup(ringp, datap, buffers);
			return 1;
		}
	}
	cleanup(ringp, datap, buffers);

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

    return ret;

}
