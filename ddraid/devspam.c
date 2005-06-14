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

void spam(char *buf, unsigned len, unsigned tag, unsigned block)
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
	char *command[] = {
		"read", "write", "randread", "randwrite",
		"readspam", "writespam", "randreadspam", "randwritespam" };
	int commands = sizeof(command) / sizeof(command[0]);
	int err = 0, dev, cmd;

	if (argc != 6)
usage:		error("usage: %s device read/write/randread/randwrite iterations blockshift tag", argv[0]);

	if ((dev = open(argv[1], O_RDWR | (1? O_DIRECT: 0))) == -1)
		error("Can't open %s, %s", argv[1], strerror(errno));

	for (cmd = 0; cmd < commands; cmd++)
		if (!strcmp(argv[2], command[cmd]))
			break;

	if (cmd == commands)
		goto usage;

	int blockshift = atoi(argv[4]), blocksize = 1 << blockshift;
	int code = atoi(argv[5]), iterations = atoi(argv[3]);
	int is_write = cmd & 1, is_rand = (cmd >> 1) & 1, spam_it = (cmd >> 2) & 1;
	unsigned range = (lseek(dev, 0, SEEK_END) >> blockshift), total = 0;
	char *buffer = malloc_aligned(blocksize, blocksize);
	char *buffer2 = malloc(blocksize);
	typeof(pread) *fn = is_write? ((typeof(pread) *)pwrite): pread;

	printf("%s tag = %u, iterations = %u, blocksize = %u, range = %u\n",
		command[cmd], code, iterations, blocksize, range);

	while (iterations--) {
		unsigned block = is_rand? (rand() % range): total;

		if (spam_it && is_write)
			spam(buffer, blocksize, code, block);

		if (fn(dev, buffer, blocksize, block << blockshift) == -1)
			error("%s error %i: %s", command[cmd], errno, strerror(errno));

		if (spam_it && !is_write) {
			spam(buffer2, blocksize, code, block);
			if (memcmp(buffer, buffer2, blocksize))
				printf("block %u doesn't match\n", block);
		}
		total++;
	}
	return err;
}
