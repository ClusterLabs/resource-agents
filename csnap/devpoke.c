#define _GNU_SOURCE /* Berserk glibc headers: O_DIRECT not defined unless _GNU_SOURCE defined */
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h> 

ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset);

#define trace_on(args) args
#define trace_off(args)
#define BREAK asm("int3")
#define warn(string, args...) do { fprintf(stderr, "%s: " string "\n", __FILE__, ##args); } while (0)
#define error(string, args...) do { warn(string, ##args); BREAK; } while (0)

#define trace trace_on

void *malloc_aligned(size_t size, unsigned binalign)
{
	unsigned long p = (unsigned long)malloc(size + binalign - 1);
	return (void *)(p + (-p & (binalign - 1)));
}

int main(int argc, char *argv[])
{
	int err, dev, iterations = 1, blockshift = 12, blocksize = 1 << blockshift;
	char *buffer = malloc_aligned(blocksize, blocksize);

	if (!(dev = open(argv[1], O_RDWR | O_DIRECT)))
		error("Could not open %s", argv[1]);
	if (argc < 3 || argc > 4)
		error("usage: %s device read/write [iterations]", argv[0]);
	if (argc > 3)
		iterations = atoi(argv[3]);

	int rw = !strcmp(argv[2], "write");
	typeof(pread) *fn = rw? ((typeof(pread) *)pwrite): pread;
	char *what = rw? "write": "read";
	unsigned range = (lseek(dev, 0, SEEK_END) >> blockshift), total = 0;

	printf("range = %u, iterations = %u\n", range, iterations);

	while (iterations--) {
		unsigned block = 1? (rand() % range): total;
		trace(warn("%s block %x", what, block);)
		if ((err = fn(dev, buffer, blocksize, block << blockshift)) < 0)
			error("poke error %i", err);
		trace(warn("...block %x done", block);)
		total++;
	}

	return err;
}
