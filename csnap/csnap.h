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

/* should be in csnap.c... */

#define MAX_SNAPSHOTS 64

typedef unsigned long long sector_t;
typedef unsigned long long chunk_t;

/* Directory at the base of the leaf block */

struct enode
{
	u32 count;
	u32 unused;
	struct index_entry
	{
		u64 key; // note: entries[0].key never accessed
		sector_t sector; // node sector address goes here
	} entries[];
};

struct eleaf
{
	le_u16 magic;
	le_u16 version;
	le_u32 count;
	le_u64 base_chunk;
	le_u64 using_mask;
	struct etree_map
	{
		le_u32 offset;
		le_u32 rchunk;
	}
	map[];
};

/* List of exceptions at top of leaf block */

struct exception
{
	le_u64 share;
	le_u64 chunk;
};

static inline struct exception *emap(struct eleaf *leaf, unsigned i)
{
	return	(struct exception *)((char *) leaf + leaf->map[i].offset);
}

struct superblock
{
	/* Persistent, saved to disk */
	struct
	{
		sector_t etree_root;
		sector_t bitmap_base;
		sector_t chunks, freechunks;
		sector_t orgchunks;
		chunk_t last_alloc;
		u64 flags;
		u32 blocksize_bits, chunksize_bits;
		u64 deleting;
		struct snapshot
		{
			u8 tag;
			u8 bit;
			u32 create_time;
			u16 reserved;
		} snaplist[MAX_SNAPSHOTS];
		u32 snapshots;
		u32 etree_levels;
		u32 bitmap_blocks;
	} image;

	/* Derived, not saved to disk */
	u64 snapmask;
	u32 blocksize, chunksize, keys_per_node;
	u32 sectors_per_block_bits, sectors_per_block;
	u32 sectors_per_chunk_bits, sectors_per_chunk;
	unsigned flags;
	unsigned snapdev, orgdev;
	unsigned snaplock_hash_bits;
	struct snaplock **snaplocks;
	unsigned copybuf_size;
	char *copybuf;
	chunk_t source_chunk;
	chunk_t dest_exception;
	unsigned copy_chunks;
};

/* Persistent superblock flags */

#define SB_BUSY 1

/* Runtime superblock flags */

#define SB_DIRTY 1

struct client
{
	unsigned id, sock;
	int snapnum;
};
