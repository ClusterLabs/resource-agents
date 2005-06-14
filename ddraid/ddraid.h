#define u8 unsigned char
#define u16 unsigned short
#define s16 short
#define s32 int
#define u32 unsigned
#define u64 unsigned long long

#define le_u32 u32
#define le_u16 u16
#define le_u64 u64
#define u64 unsigned long long
#define EFULL ENOMEM
#define PACKED __attribute__ ((packed))

static inline int readpipe(int fd, void *buffer, size_t count)
{
	// printf("read %u bytes\n", count);
	int n;
	while (count) {
		if ((n = read(fd, buffer, count)) < 1)
			return n? n: -EPIPE;
		buffer += n;
		count -= n;
	}
	return 0;
}

#define writepipe write

#define outbead(SOCK, CODE, STRUCT, VALUES...) ({ \
	struct { struct head head; STRUCT body; } PACKED message = \
		{ { CODE, sizeof(STRUCT) }, { VALUES } }; \
	writepipe(SOCK, &message, sizeof(message)); })

typedef unsigned long long chunk_t;

#define MAX_ADDRESS 16

struct server { u16 port; u8 type; u8 address_len; char address[MAX_ADDRESS]; } PACKED;

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 256
#endif

