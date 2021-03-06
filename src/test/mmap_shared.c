/* -*- Mode: C; tab-width: 8; c-basic-offset: 8; indent-tabs-mode: t; -*- */

#include "rrutil.h"

static int create_segment(size_t num_bytes) {
	char filename[] = "/dev/shm/rr-test-XXXXXX";
	int fd = mkstemp(filename);
	unlink(filename);
	test_assert(fd >= 0);
	ftruncate(fd, num_bytes);
	return fd;
}

struct mmap_arg_struct {
	unsigned long addr;
	unsigned long len;
	unsigned long prot;
	unsigned long flags;
        unsigned long fd;
	unsigned long offset;
};

int main(int argc, char *argv[]) {
	size_t num_bytes = sysconf(_SC_PAGESIZE);
	int fd = create_segment(num_bytes);
	int* wpage = mmap(NULL, num_bytes, PROT_WRITE, MAP_SHARED, fd, 0);
	int i;
	int* rpage;

	close(128);
	munmap(NULL, 0);

	struct mmap_arg_struct args;
	args.addr = 0;
	args.len = num_bytes;
	args.prot = PROT_READ;
	args.flags = MAP_SHARED;
	args.fd = fd;
	args.offset = 0;
	rpage = (int*)syscall(SYS_mmap, &args);

	test_assert(wpage != (void*)-1 && rpage != (void*)-1
		    && rpage != wpage);

	close(128);

	for (i = 0; i < num_bytes / sizeof(int); ++i) {
		wpage[i] = i;
		test_assert(rpage[i] == i);
		atomic_printf("%d,", rpage[i]);
	}

	atomic_puts(" done");

	return 0;
}
