#define _GNU_SOURCE /* O_DIRECT */
#define _XOPEN_SOURCE 500 /* pwrite */
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#define error(string, args...) do { printf(string "\n", ##args); exit(1); } while (0)

void *malloc_aligned(size_t size, unsigned binalign)
{
	unsigned long p = (unsigned long)malloc(size + binalign - 1);
	return (void *)(p + (-p & (binalign - 1)));
}

void spamdata(char *buf, unsigned len, unsigned tag, unsigned block)
{
	int i, j, k, n = len / 16;
	char *p = buf;

	for(i = 0; i < n; i++) {
		int spams[3] = { tag, block, i };
		memcpy(p, "SPAM", 4);
		p += 4;
		for(j = 0; j < 3; j++) {
			int v = spams[j];
			for (k = 0; k < 4; k++,v <<= 4) {
				int d = (v >> 12) & 0xf;
				*p++ = d < 10? d + '0': d - 10 + 'a'; 
			}
		}
	}
	/* assert(p == buf + len); */
}

int main(int argc, char *argv[])
{
	#define ncommands 4
	char *commands[ncommands] = { "read", "write", "randread", "randwrite" };
	int err, dev, command, blockshift = 12, blocksize = 1 << blockshift;
	char *buffer = malloc_aligned(blocksize, blocksize);
	char *buffer2 = malloc(blocksize);

	if (argc != 5)
usage:		error("usage: %s device read/write/randread/randwrite tag iterations", argv[0]);

	if ((dev = open(argv[1], O_RDWR /*| O_DIRECT*/)) == -1)
		error("Can't open %s, %s", argv[1], strerror(errno));

	for (command = 0; command < ncommands; command++)
		if (!strcmp(argv[2], commands[command]))
			break;

	if (command == ncommands)
		goto usage;

	int code = atoi(argv[4]), iterations = atoi(argv[3]);
	int is_write = command & 1, is_rand = command >> 1;
	unsigned range = (lseek(dev, 0, SEEK_END) >> blockshift), total = 0;
	typeof(pread) *fn = is_write? ((typeof(pread) *)pwrite): pread;

	printf("spam code = %u, iterations = %u, range = %u\n", code, iterations, range);

	while (iterations--) {
		unsigned block = is_rand? (rand() % range): total;

		if (is_write)
			spamdata(buffer, blocksize, code, block);

		if ((err = fn(dev, buffer, blocksize, block << blockshift)) < 0)
			error("spam %s error %i", commands[command], err);

		if (!is_write) {
			spamdata(buffer2, blocksize, code, block);
			if (memcmp(buffer, buffer2, blocksize))
				printf("block %u doesn't match\n", block);
		}
		total++;
	}
	return err;
}
