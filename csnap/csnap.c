/*
 * Clustered Snapshot Metadata Server
 *
 * Daniel Phillips, Nov 2003 to May 2004
 * (c) 2003 Sistina Software Inc.
 * (c) 2004 Red Hat Software Inc.
 *
 */

#undef BUSHY
#define _GNU_SOURCE /* Berserk glibc headers: O_DIRECT not defined unless _GNU_SOURCE defined */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h> 
#include <time.h>
#include <signal.h>
#include <sys/poll.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netdb.h> // gethostbyname2_r
#include <linux/fs.h> // BLKGETSIZE
#include "list.h"
#include "buffer.h"
#include "csnap.h"
#include "../dm-csnap.h"
#include "trace.h"

#define trace trace_off
#define jtrace trace_off

/*
Todo:

BTree
  * coalesce leafs/nodes for delete
  - B*Tree splitting

Allocation bitmaps
  - allocation statistics
  - Per-snapshot free space as full-tree pass
  - option to track specific snapshot(s) on the fly
  - return stats to client (on demand? always?)
  * Bitmap block radix tree - resizing
  - allocation policy

Journal
  \ allocation
  \ write commit block
  \ write target blocks
  \ recovery
  - stats and misc data in commit block?

File backing
  \ double linked list ops
  - buffer lru 
  - buffer writeout policy
  - buffer eviction policy
  - verify no busy buffers between operations

Snapshot vs origin locking
  - anti-starvation measures

Message handling
  - send reply on async write completion
  - build up immediate reply in separate buffer

Snapshot handling path
  - background copyout thread
  - try AIO
  - coalesce leaves/nodes on delete
     - should wait for current queries on snap to complete
  - background deletion optimization
     - record current deletion list in superblock

Multithreading
  - separate thread for copyouts
  - separate thread for buffer flushing
  - separate thread for new connections (?)

Utilities
  - don't include anything not needed for create
  - snapshot store integrity check (snapcheck)

Error recovery
  \ Mark superblock active/inactive
  + upload client locks on server restart
  + release snapshot read locks for dead client
  - Examine entire tree to initialize statistics after unsaved halt

General
  \ Prevent multiple server starts on same snapshot store
  + More configurable tracing
  - Add more internal consistency checks
  - Magic number + version for superblock
  - Flesh out and audit error paths
  - Make it endian-neutral
  - Verify wordsize neutral
  - Add an on-the-fly verify path
  + strip out the unit testing gunk
  + More documentation
  - Audits and more audits
  - Memory inversion prevention

Cluster integration
  + Restart/Error recovery/reporting
*/

/*
 * Miscellaneous Primitives
 */

typedef int fd_t;

/*
 * Ripped from libiddev.  It's not quite ugly enough to convince me to
 * add a new dependency on a library that nobody has yet, but it's close.
 */
static int fd_size(int fd, u64 *bytes)
{
	struct stat stat;
	int error;

	if ((error = fstat(fd, &stat)))
		return error;

	if (S_ISREG(stat.st_mode)) {
		*bytes = stat.st_size;
		return 0;
	}
	if ((error = ioctl(fd, BLKGETSIZE64, bytes))) {
		unsigned sectors;

		if ((error = ioctl(fd, BLKGETSIZE, &sectors)))
			return error;
		*bytes = ((u64)sectors) << 9;
	}
	return 0;
}

void hexdump(void *data, unsigned length)
{
	while (length ) {
		int row = length < 16? length: 16;
		printf("%p: ", data);
		length -= row;
		while (row--)
			printf("%02hhx ", *(unsigned char *)data++);
		printf("\n");
	}
}

/* BTree Operations */

/* Directory at the base of the leaf block */

#define MAX_SNAPSHOTS 64

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

static inline struct enode *buffer2node(struct buffer *buffer)
{
	return (struct enode *)buffer->data;
}

static inline struct eleaf *buffer2leaf(struct buffer *buffer)
{
	return (struct eleaf *)buffer->data;
}

/* On-disk Format */

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
	struct disksuper
	{
		char magic[8];
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
		s32 journal_base, journal_next, journal_size;
		u32 sequence;
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
	unsigned max_commit_blocks;
};

#define SB_BUSY 1
#define SB_MAGIC "snapshot"

/* Journal handling */

#define JMAGIC "MAGICNUM"

struct commit_block
{
	char magic[8];
	u32 checksum;
	s32 sequence;
	u32 entries;
	u64 sector[];
} PACKED;

static sector_t journal_sector(struct superblock *sb, unsigned i)
{
	return sb->image.journal_base + (i << sb->sectors_per_block_bits);
}

static inline struct commit_block *buf2block(struct buffer *buf)
{
	return (void *)buf->data;
}

unsigned next_journal_block(struct superblock *sb)
{
	unsigned next = sb->image.journal_next;

	if (++sb->image.journal_next == sb->image.journal_size)
		sb->image.journal_next = 0;

	return next;
}

static int is_commit_block(struct commit_block *block)
{
	return !memcmp(&block->magic, JMAGIC, sizeof(block->magic));
}

static u32 checksum_block(struct superblock *sb, u32 *data)
{
	int i, sum = 0;
	for (i = 0; i < sb->image.blocksize_bits >> 2; i++)
		sum += data[i];
	return sum;
}

static struct buffer *jgetblk(struct superblock *sb, unsigned i)
{
	return getblk(sb->snapdev, journal_sector(sb, i), sb->blocksize);
}

static struct buffer *jread(struct superblock *sb, unsigned i)
{
	return bread(sb->snapdev, journal_sector(sb, i), sb->blocksize);
}

/*
 * For now there is only ever one open transaction in the journal, the newest
 * one, so we don't have to check for journal wrap, but just ensure that each
 * transaction stays small enough to fit in the journal.
 *
 * Since we don't have any asynchronous IO at the moment, journal commit is
 * straightforward: walk through the dirty blocks once, writing them to the
 * journal, then again, adding sector locations to the commit block.  We know
 * the dirty list didn't change between the two passes.  When ansynchronous
 * IO arrives here, this all has to be handled a lot more carefully.
 */
static void commit_transaction(struct superblock *sb)
{
// flush_buffers();
// return;
	if (list_empty(&dirty_buffers))
		return;

	struct list_head *list;

	list_for_each(list, &dirty_buffers) {
		struct buffer *buffer = list_entry(list, struct buffer, list);
		unsigned pos = next_journal_block(sb);
		jtrace(warn("journal data sector = %Lx [%u]", buffer->sector, pos);)
		assert(buffer_dirty(buffer));
		write_buffer_to(buffer, journal_sector(sb, pos));
	}

	unsigned pos = next_journal_block(sb);
	struct buffer *commit_buffer = jgetblk(sb, pos);
	struct commit_block *commit = buf2block(commit_buffer);
	*commit = (struct commit_block){ .magic = JMAGIC, .sequence = sb->image.sequence++ }; 

	while (!list_empty(&dirty_buffers)) {
		struct list_head *entry = dirty_buffers.next;
		struct buffer *buffer = list_entry(entry, struct buffer, list);
		jtrace(warn("write data sector = %Lx", buffer->sector);)
		assert(buffer_dirty(buffer));
		assert(commit->entries < sb->max_commit_blocks);
		commit->sector[commit->entries++] = buffer->sector;
		write_buffer(buffer); // deletes it from dirty (fixme: fragile)
		// we hope the order we just listed these is the same as committed above
	}

	jtrace(warn("commit journal block [%u]", pos);)
	commit->checksum = 0;
	commit->checksum = -checksum_block(sb, (void *)commit);
	write_buffer_to(commit_buffer, journal_sector(sb, pos));
	brelse(commit_buffer);
}

int recover_journal(struct superblock *sb)
{
	struct buffer *buffer;
	typeof(((struct commit_block *)NULL)->sequence) sequence;
	int scribbled = -1, last_block = -1, newest_block = -1;
	int data_from_start = 0, data_from_last = 0;
	int size = sb->image.journal_size;
	char *why = "";
	unsigned i;

	/* Scan full journal, find newest commit */

	for (i = 0; i < size; brelse(buffer), i++) {
		buffer = jread(sb, i);
		struct commit_block *block = buf2block(buffer);

		if (!is_commit_block(block)) {
			jtrace(warn("[%i] <data>", i);)
			if (sequence == -1)
				data_from_start++;
			else
				data_from_last++;
			continue;
		}

		if (checksum_block(sb, (void *)block)) {
			warn("block %i failed checksum", i);
			hexdump(block, 40);
			if (scribbled != -1) {
				why = "Too many scribbled blocks in journal";
				goto failed;
			}

			if (newest_block != -1 && newest_block != last_block) {
				why = "Bad block not last written";
				goto failed;
			}

			scribbled = i;
			if (last_block != -1)
				newest_block = last_block;
			sequence++;
			continue;
		}

		jtrace(warn("[%i] seq=%i", i, block->sequence);)

		if (last_block != -1 && block->sequence != sequence + 1) {
			int delta = sequence - block->sequence;

			if  (delta <= 0 || delta > size) {
				why = "Bad sequence";
				goto failed;
			}
	
			if (newest_block != -1) {
				why = "Multiple sequence wraps";
				goto failed;
			}
	
			if (!(scribbled == -1 || scribbled == i - 1)) {
				why = "Bad block not last written";
				goto failed;
			}
			newest_block = last_block;
		}
		data_from_last = 0;
		last_block = i;
		sequence = block->sequence;
	}

	if (last_block == -1) {
		why = "No commit blocks found";
		goto failed;
	}
	
	if (newest_block == -1) {
		/* test for all the legal scribble positions here */
		newest_block = last_block;
	}

	jtrace(warn("found newest commit [%u]", newest_block);)
	buffer = jread(sb, newest_block);
	struct commit_block *commit = buf2block(buffer);
	unsigned entries = commit->entries;

	for (i = 0; i < entries; i++) {
		unsigned pos = (newest_block - entries + i + size) % size;
		struct buffer *databuf = jread(sb, pos);
		struct commit_block *block = buf2block(databuf);

		if (is_commit_block(block)) {
			error("data block [%u] marked as commit block", pos);
			continue;
		}

		jtrace(warn("write journal [%u] data to %Lx", pos, commit->sector[i]);)
		write_buffer_to(databuf, commit->sector[i]);
		brelse(databuf);
	}
	sb->image.journal_next = (newest_block + 1 + size) % size;
	sb->image.sequence = commit->sequence + 1;
	brelse(buffer);
	return 0;

failed:
	errno = EIO; /* return a misleading error (be part of the problem) */
	error("Journal recovery failed, %s", why);
	return -1;
}

static void _show_journal(struct superblock *sb)
{
	int i, j;
	for (i = 0; i < sb->image.journal_size; i++) {
		struct buffer *buf = jread(sb, i);
		struct commit_block *block = buf2block(buf);

		if (!is_commit_block(block)) {
			printf("[%i] <data>\n", i);
			continue;
		}

		printf("[%i] seq=%i (%i)", i, block->sequence, block->entries);
		for (j = 0; j < block->entries; j++)
			printf(" %Lx", (long long)block->sector[j]);
		printf("\n");
		brelse(buf);
	}
	printf("\n");
}

#define show_journal(sb) do { warn("Journal..."); _show_journal(sb); } while (0)

/* BTree leaf operations */

/*
 * We operate directly on the BTree leaf blocks to insert exceptions and
 * to enquire the sharing status of given chunks.  This means all the data
 * items in the block need to be properly aligned for architecture
 * independence.  To save space and to permit binary search a directory
 * map at the beginning of the block points at the exceptions stored
 * at the top of the block.  The difference between two successive directory
 * pointers gives the number of distinct exceptions for a given chunk.
 * Each exception is paired with a bitmap that specifies which snapshots
 * the exception belongs to.
 *
 * The chunk addresses in the leaf block directory are relative to a base
 * chunk to save space.  These are currently 32 bit values but may become
 * 16 bits values.  Since each is paired with a pointer into the list of
 * exceptions, 16 bit emap entries would limit the blocksize to 64K.
 *
 * A mask in the leaf block header specifies which snapshots are actually
 * encoded in the chunk.  This allows lazy deletion (almost, needs fixing)
 *
 * The leaf operations need to know the size of the block somehow.
 * Currently that is accomplished by inserting the block size as a sentinel
 * in the block directory map; this may change.
 *
 * When an exception is created by a write to the origin it is initially
 * shared by all snapshots that don't already have exceptions.  Snapshot
 * writes may later unshare some of these exceptions.
 */

/*
 * To do:
 *   - Check leaf, index structure
 *   - Mechanism for identifying which snapshots are in each leaf
 *   - binsearch for leaf, index lookup
 *   - enforce 32 bit address range within leaf
 */

struct buffer *snapread(struct superblock *sb, sector_t sector)
{
	return bread(sb->snapdev, sector, sb->blocksize);
}

void show_leaf(struct eleaf *leaf)
{
	struct exception *p;
	int i;
	
	printf("%i chunks: ", leaf->count);
	for (i = 0; i < leaf->count; i++) {
		printf("%i=", leaf->map[i].rchunk);
		// printf("@%i ", leaf->map[i].offset);
		for (p = emap(leaf, i); p < emap(leaf, i+1); p++)
			printf("%Lx/%08llx%s", p->chunk, p->share, p+1 < emap(leaf, i+1)? ",": " ");
	}
	// printf("top@%i", leaf->map[i].offset);
	printf("\n");
}

/*
 * origin_chunk_unique: an origin logical chunk is shared unless all snapshots
 * have exceptions.
 */

int origin_chunk_unique(struct eleaf *leaf, u64 chunk, u64 snapmask)
{
	u64 using = 0;
	unsigned i, target = chunk - leaf->base_chunk;
	struct exception *p;

	for (i = 0; i < leaf->count; i++)
		if (leaf->map[i].rchunk == target)
			goto found;
	return !snapmask;
found:
	for (p = emap(leaf, i); p < emap(leaf, i+1); p++)
		using |= p->share;

	return !(~using & snapmask);
}

/*
 * snapshot_chunk_unique: a snapshot logical chunk is shared if it has no
 * exception or has the same exception as another snapshot.  In any case
 * if the chunk has an exception we need to know the exception address.
 */

int snapshot_chunk_unique(struct eleaf *leaf, u64 chunk, int snapshot, u64 *exception)
{
	u64 mask = 1LL << snapshot;
	unsigned i, target = chunk - leaf->base_chunk;
	struct exception *p;

	for (i = 0; i < leaf->count; i++)
		if (leaf->map[i].rchunk == target)
			goto found;
	return 0;
found:
	for (p = emap(leaf, i); p < emap(leaf, i+1); p++)
		/* shared if more than one bit set including this one */
		if ((p->share & mask)) {
			*exception = p->chunk;
// printf("unique %Lx %Lx\n", p->share, mask);
			return !(p->share & ~mask);
		}
	return 0;
}

/*
 * add_exception_to_leaf:
 *  - cycle through map to find existing logical chunk or insertion point
 *  - if not found need to add new chunk address
 *      - move tail of map up
 *      - store new chunk address in map
 *  - otherwise
 *      - for origin:
 *          - or together all sharemaps, invert -> new map
 *      - for snapshot:
 *          - clear out bit for existing exception
 *              - if sharemap zero warn and reuse this location
 *  - insert new exception
 *      - move head of exceptions down
 *      - store new exception/sharemap
 *      - adjust map head offsets
 *
 * If the new exception won't fit in the leaf, return an error so that
 * higher level code may split the leaf and try again.  This keeps the
 * leaf-editing code complexity down to a dull roar.
 */

int add_exception_to_leaf(struct eleaf *leaf, u64 chunk, u64 exception, int snapshot, u64 active)
{
	unsigned i, j, target = chunk - leaf->base_chunk;
	u64 mask = 1ULL << snapshot, sharemap;
	struct exception *ins, *exceptions = emap(leaf, 0);
	char *maptop = (char *)(&leaf->map[leaf->count + 1]); // include sentinel
	int free = (char *)exceptions - maptop;
	trace(warn("chunk %Lx exception %Lx, snapshot = %i", chunk, exception, snapshot);)

	for (i = 0; i < leaf->count; i++) // !!! binsearch goes here
		if (leaf->map[i].rchunk >= target)
			break;

	if (i == leaf->count || leaf->map[i].rchunk > target) {
		if (free < sizeof(struct exception) + sizeof(struct etree_map))
			return -EFULL;

		ins = emap(leaf, i);
		memmove(&leaf->map[i+1], &leaf->map[i], maptop - (char *)&leaf->map[i]);
		leaf->map[i].offset = (char *)ins - (char *)leaf;
		leaf->map[i].rchunk = target;
		leaf->count++;
		sharemap = snapshot == -1? active: mask;
		goto insert;
	}

	if (free < sizeof(struct exception))
		return -EFULL;

	if (snapshot == -1) {
		for (sharemap = 0, ins = emap(leaf, i); ins < emap(leaf, i+1); ins++)
			sharemap |= ins->share;
		sharemap = (~sharemap) & active;
	} else {
		for (ins = emap(leaf, i); ins < emap(leaf, i+1); ins++)
			if ((ins->share & mask)) {
				ins->share &= ~mask;
				break;
			}
		sharemap = mask;
	}
	ins = emap(leaf, i);
insert:
	memmove(exceptions - 1, exceptions, (char *)ins - (char *)exceptions);
	ins--;
	ins->share = sharemap;
	ins->chunk = exception;

	for (j = 0; j <= i; j++)
		leaf->map[j].offset -= sizeof(struct exception);

	return 0;
}

/*
 * split_leaf: Split one leaf into two approximately in the middle.  Copy
 * the upper half of entries to the new leaf and move the lower half of
 * entries to the top of the original block.
 */

u64 split_leaf(struct eleaf *leaf, struct eleaf *leaf2)
{
	unsigned i, nhead = (leaf->count + 1) / 2, ntail = leaf->count - nhead, tailsize;
	/* Should split at middle of data instead of median exception */
	u64 splitpoint = leaf->map[nhead].rchunk + leaf->base_chunk;
	char *phead, *ptail;

	phead = (char *)emap(leaf, 0);
	ptail = (char *)emap(leaf, nhead);
	tailsize = (char *)emap(leaf, leaf->count) - ptail;

	/* Copy upper half to new leaf */
	memcpy(leaf2, leaf, offsetof(struct eleaf, map)); // header
	memcpy(&leaf2->map[0], &leaf->map[nhead], (ntail + 1) * sizeof(leaf->map[0])); // map
	memcpy(ptail - (char *)leaf + (char *)leaf2, ptail, tailsize); // data
	leaf2->count = ntail;

	/* Move lower half to top of block */
	memmove(phead + tailsize, phead, ptail - phead);
	leaf->count = nhead;
	for (i = 0; i <= nhead; i++) // also adjust sentinel
		leaf->map[i].offset += tailsize;
	leaf->map[nhead].rchunk = 0; // tidy up

	return splitpoint;
}

void init_leaf(struct eleaf *leaf, int block_size)
{
	leaf->magic = 0x1eaf;
	leaf->version = 0;
	leaf->base_chunk = 0;
	leaf->count = 0;
	leaf->map[0].offset = block_size;
#ifdef BUSHY
	leaf->map[0].offset = 200;
#endif
}

/*
 * Chunk allocation via bitmaps
 */

#define SB_DIRTY 1
#define SB_LOC 8

void mark_sb_dirty(struct superblock *sb)
{
	sb->flags |= SB_DIRTY;
}

int get_bitmap_bit(unsigned char *bitmap, unsigned bit)
{
	return bitmap[bit >> 3] & (1 << (bit & 7));
}

void set_bitmap_bit(unsigned char *bitmap, unsigned bit)
{
	bitmap[bit >> 3] |= 1 << (bit & 7);
}

void clear_bitmap_bit(unsigned char *bitmap, unsigned bit)
{
	bitmap[bit >> 3] &= ~(1 << (bit & 7));
}

void init_allocation(struct superblock *sb)
{
	u64 chunks = sb->image.chunks;
	unsigned chunkshift = sb->image.chunksize_bits;
	unsigned bitmaps = (chunks + (1 << (chunkshift + 3)) - 1) >> (chunkshift + 3);
	unsigned bitmap_base_chunk = (SB_LOC + sb->sectors_per_block + sb->sectors_per_chunk  - 1) >> sb->sectors_per_chunk_bits;
	unsigned bitmap_chunks = sb->image.bitmap_blocks = bitmaps; // !!! chunksize same as blocksize
	unsigned reserved = bitmap_base_chunk + bitmap_chunks + sb->image.journal_size; // !!! chunksize same as blocksize
	unsigned sector = sb->image.bitmap_base = bitmap_base_chunk << sb->sectors_per_chunk_bits;

	warn("snapshot store size: %Lu chunks (%Lu sectors)", chunks, chunks << sb->sectors_per_chunk_bits);
	printf("Initializing %u bitmap blocks... ", bitmaps);

	unsigned i;
	for (i = 0; i < bitmaps; i++, sector += sb->sectors_per_block) {
		struct buffer *buffer = getblk(sb->snapdev, sector, sb->blocksize);
		printf("%Lx ", buffer->sector);
		memset(buffer->data, 0, sb->blocksize);
		/* Reserve bitmaps and superblock */
		if (i == 0) {
			unsigned i;
			for (i = 0; i < reserved; i++)
				set_bitmap_bit(buffer->data, i);
		}
		/* Suppress overrun allocation in partial last byte */
		if (i == bitmaps - 1 && (chunks & 7))
			buffer->data[(chunks >> 3) & (sb->blocksize - 1)] |= 0xff << (chunks & 7);
		trace_off(dump_buffer(buffer, 0, 16);)
		brelse_dirty(buffer);
	}
	printf("\n");
	sb->image.freechunks = chunks - reserved;
	sb->image.last_alloc = 0;
	sb->image.journal_base = (bitmap_base_chunk + bitmap_chunks) << sb->sectors_per_chunk_bits;
}

#if 0
char *memscan (char *addr, int c,  size_t size)
{
	while (size-- && *addr != c)
		addr++;
	return addr;
}
#endif

void free_chunk(struct superblock *sb, chunk_t chunk)
{
	unsigned bitmap_shift = sb->image.blocksize_bits + 3, bitmap_mask = (1 << bitmap_shift ) - 1;
	u64 bitmap_block = chunk >> bitmap_shift;

	trace(printf("free chunk %Lx\n", chunk);)
	struct buffer *buffer = snapread(sb, sb->image.bitmap_base + (bitmap_block << sb->sectors_per_block_bits));
	assert(get_bitmap_bit(buffer->data, chunk & bitmap_mask));
	clear_bitmap_bit(buffer->data, chunk & bitmap_mask);
	brelse_dirty(buffer);
	sb->image.freechunks++;
	mark_sb_dirty(sb); // !!! optimize this away
}

#if 0
void grab_chunk(struct superblock *sb, chunk_t chunk) // just for testing
{
	unsigned bitmap_shift = sb->image.blocksize_bits + 3, bitmap_mask = (1 << bitmap_shift ) - 1;
	u64 bitmap_block = chunk >> bitmap_shift;

	struct buffer *buffer = snapread(sb, sb->image.bitmap_base + (bitmap_block << sb->sectors_per_block_bits));
	assert(!get_bitmap_bit(buffer->data, chunk & bitmap_mask));
	set_bitmap_bit(buffer->data, chunk & bitmap_mask);
	brelse_dirty(buffer);
}
#endif

chunk_t alloc_chunk_range(struct superblock *sb, chunk_t chunk, chunk_t range)
{
	unsigned bitmap_shift = sb->image.blocksize_bits + 3, bitmap_mask = (1 << bitmap_shift ) - 1;
	u64 blocknum = chunk >> bitmap_shift;
	unsigned bit = chunk & 7, offset = (chunk & bitmap_mask) >> 3;
	u64 length = (range + bit + 7) >> 3;

	while (1) {
		struct buffer *buffer = snapread(sb, sb->image.bitmap_base + (blocknum << sb->sectors_per_block_bits));
		unsigned char c, *p = buffer->data + offset;
		unsigned tail = sb->blocksize  - offset, n = tail > length? length: tail;
	
		trace_off(printf("search %u bytes of bitmap %Lx from offset %u\n", n, blocknum, offset);)
		// dump_buffer(buffer, 4086, 10);
	
		for (length -= n; n--; p++)
			if ((c = *p) != 0xff) {
				int i, bit;
				trace_off(printf("found byte at offset %u of bitmap %Lx = %hhx\n", p - buffer->data, blocknum, c);)
				for (i = 0, bit = 1;; i++, bit <<= 1)
					if (!(c & bit)) {
						chunk = i + ((p - buffer->data) << 3) + (blocknum << bitmap_shift);
						assert(!get_bitmap_bit(buffer->data, chunk & bitmap_mask));
						set_bitmap_bit(buffer->data, chunk & bitmap_mask);
						brelse_dirty(buffer);
						sb->image.freechunks--;
						mark_sb_dirty(sb); // !!! optimize this away
						return chunk;
					}
			}
	
		brelse(buffer);
		if (!length)
			return 0;
		if (++blocknum == sb->image.bitmap_blocks)
			 blocknum = 0;
		offset = 0;
		trace_off(printf("go to bitmap %Lx\n", blocknum);)
	}
}

chunk_t alloc_chunk(struct superblock *sb)
{
	chunk_t  last = sb->image.last_alloc, total = sb->image.chunks, found;

	if ((found =  alloc_chunk_range(sb, last, total - last)))
		goto success;
	if (!(found =  alloc_chunk_range(sb, 0, last)))
		error("snapshot store full, do something");
success:
	sb->image.last_alloc = found;
	mark_sb_dirty(sb); // !!! optimize this away
	return (found);
}

/* Snapshot Store Allocation */

sector_t alloc_block(struct superblock *sb)
{
	return alloc_chunk(sb) << sb->sectors_per_chunk_bits; // !!! assume blocksize = chunksize
}

u64 alloc_exception(struct superblock *sb)
{
	return alloc_chunk(sb);
}

struct buffer *new_block(struct superblock *sb)
{
	return getblk(sb->snapdev, alloc_block(sb), sb->blocksize);
}

struct buffer *new_leaf(struct superblock *sb)
{
	trace(printf("New leaf\n");)
	struct buffer *buffer = new_block(sb);
	init_leaf(buffer2leaf(buffer), sb->blocksize);
	set_buffer_dirty(buffer);
	return buffer;
}

struct buffer *new_node(struct superblock *sb)
{
	trace(printf("New node\n");)
	struct buffer *buffer = new_block(sb);
	struct enode *node = buffer2node(buffer);
	node->count = 0;
	set_buffer_dirty(buffer);
	return buffer;
}

/* BTree debug dump */

void show_subtree(struct superblock *sb, struct enode *node, int levels, int indent)
{
	int i;
	printf("%*s", indent, "");
	printf("%i nodes:\n", node->count);
	for (i = 0; i < node->count; i++) {
		struct buffer *buffer = snapread(sb, node->entries[i].sector);
		if (levels)
			show_subtree(sb, buffer2node(buffer), levels - 1, indent + 3);
		else {
			printf("%*s", indent + 3, "");
			show_leaf(buffer2leaf(buffer));
		}
		brelse(buffer);
	}
}

void show_tree(struct superblock *sb)
{
	struct buffer *buffer = snapread(sb, sb->image.etree_root);
	show_subtree(sb, buffer2node(buffer), sb->image.etree_levels - 1, 0);
	brelse(buffer);
}

/* High Level BTree Editing */

/*
 * BTree insertion is a little hairy, as expected.  We keep track of the
 * access path in a vector of etree_path elements, each of which holds
 * a node buffer and a pointer into the buffer data giving the address at
 * which the next buffer in the path was found, which is also where a new
 * node will be inserted if necessary.  If a leaf is split we may need to
 * work all the way up from the bottom to the top of the path, splitting
 * index nodes as well.  If we split the top index node we need to add
 * a new tree level.  We have to keep track of which nodes were modified
 * and keep track of refcounts of all buffers involved, which can be quite
 * a few.
 *
 * Note that the first key of an index block is never accessed.  This is
 * because for a btree, there is always one more key than nodes in each
 * index node.  In other words, keys lie between node pointers.  We will
 * micro-optimize by placing the node count in the first key, which allows
 * a node to contain an esthetically pleasing binary number of pointers.
 * (Not done yet.)
 */

#define MAX_ETREE_DEPTH 6

struct etree_path { struct buffer *buffer; struct index_entry *pnext; };

struct buffer *probe(struct superblock *sb, u64 chunk, struct etree_path *path)
{
	unsigned i, levels = sb->image.etree_levels;
	struct buffer *nodebuf = snapread(sb, sb->image.etree_root);
	struct enode *node = buffer2node(nodebuf);

	for (i = 0; i < levels; i++) {
		struct index_entry *pnext = node->entries, *top = pnext + node->count;

		while (++pnext < top)
			if (pnext->key > chunk)
				break;

		path[i].buffer = nodebuf;
		path[i].pnext = pnext;
		nodebuf = snapread(sb, (pnext - 1)->sector);
		node = (struct enode *)nodebuf->data;
	}
	assert(((struct eleaf *)nodebuf->data)->magic == 0x1eaf);
	return nodebuf;
}

void brelse_path(struct etree_path *path, unsigned levels)
{
	unsigned i;
	for (i = 0; i < levels; i++)
		brelse(path[i].buffer);
}

void show_tree_range(struct superblock *sb, chunk_t start, unsigned leaves)
{
	int levels = sb->image.etree_levels, level = -1;
	struct etree_path path[levels];
	struct buffer *nodebuf;
	struct enode *node;
	struct buffer *leafbuf;

#if 1
	leafbuf = probe(sb, start, path);
	level = levels - 1;
	nodebuf = path[level].buffer;
	node = buffer2node(nodebuf);
	goto start;
#endif

	while (1) {
 		do {
			level++;
			nodebuf = snapread(sb, level? path[level - 1].pnext++->sector: sb->image.etree_root);
			node = buffer2node(nodebuf);
			path[level].buffer = nodebuf;
			path[level].pnext = node->entries;
			trace(printf("push to level %i, %i nodes\n", level, node->count);)
		} while (level < levels - 1);

		trace(printf("do %i leaf nodes level = %i\n", node->count, level);)
		while (path[level].pnext  < node->entries + node->count) {
			leafbuf = snapread(sb, path[level].pnext++->sector);
start:			show_leaf(buffer2leaf(leafbuf));
			brelse(leafbuf);
			if (!--leaves) {
				brelse_path(path, level + 1);
				return;
			}
		}

		do {
			brelse(nodebuf);
			if (!level)
				return;
			nodebuf = path[--level].buffer;
			node = buffer2node(nodebuf);
			trace(printf("pop to level %i, %i of %i nodes\n", level, path[level].pnext - node->entries, node->count);)
		} while (path[level].pnext == node->entries + node->count);
	};
}

void insert_child(struct enode *node, struct index_entry *p, sector_t child, u64 childkey)
{
	memmove(p + 1, p, (char *)(&node->entries[0] + node->count) - (char *)p);
	p->sector = child;
	p->key = childkey;
	node->count++;
}

void add_exception_to_tree(struct superblock *sb, struct buffer *leafbuf, u64 target, u64 exception, int snapnum, struct etree_path path[], unsigned levels)
{
	if (!add_exception_to_leaf(buffer2leaf(leafbuf), target, exception, snapnum, sb->snapmask)) {
		brelse_dirty(leafbuf);
		return;
	}

	trace(printf("add leaf\n");)
	struct buffer *childbuf = new_leaf(sb);
	u64 childkey = split_leaf(buffer2leaf(leafbuf), buffer2leaf(childbuf));
	sector_t childsector = childbuf->sector;

	if (add_exception_to_leaf(target < childkey? buffer2leaf(leafbuf): buffer2leaf(childbuf), target, exception, snapnum, sb->snapmask))
		error("can't happen");
	brelse_dirty(leafbuf);
	brelse_dirty(childbuf);

	while (levels--) {
		struct index_entry *pnext = path[levels].pnext;
		struct buffer *parentbuf = path[levels].buffer;
		struct enode *parent = buffer2node(parentbuf);

		if (parent->count < sb->keys_per_node) {
			insert_child(parent, pnext, childsector, childkey);
			set_buffer_dirty(parentbuf);
			return;
		}

		unsigned half = parent->count / 2;
		u64 newkey = parent->entries[half].key;
		struct buffer *newbuf = new_node(sb);
		struct enode *newnode = buffer2node(newbuf);

		newnode->count = parent->count - half;
		memcpy(&newnode->entries[0], &parent->entries[half], newnode->count * sizeof(struct index_entry));
		parent->count = half;

		if (pnext > &parent->entries[half]) {
			pnext = pnext - &parent->entries[half] + newnode->entries;
			set_buffer_dirty(parentbuf);
			parentbuf = newbuf;
			parent = newnode;
		} else set_buffer_dirty(newbuf);

		insert_child(parent, pnext, childsector, childkey);
		set_buffer_dirty(parentbuf);
		childkey = newkey;
		childsector = newbuf->sector;
		brelse(newbuf);
	}

	trace(printf("add tree level\n");)
	struct buffer *newrootbuf = new_node(sb); // !!! handle error
	struct enode *newroot = buffer2node(newrootbuf);

	newroot->count = 2;
	newroot->entries[0].sector = sb->image.etree_root;
	newroot->entries[1].key = childkey;
	newroot->entries[1].sector = childsector;
	sb->image.etree_root = newrootbuf->sector;
	sb->image.etree_levels++;
	mark_sb_dirty(sb);
	brelse_dirty(newrootbuf);
}
#define chunk_highbit ((sizeof(chunk_t) * 8) - 1)

int finish_copyout(struct superblock *sb)
{
	if (sb->copy_chunks) {
		int is_snap = sb->source_chunk >> chunk_highbit;
		chunk_t source = sb->source_chunk & ~(1ULL << chunk_highbit);
		unsigned size = sb->copy_chunks << sb->image.chunksize_bits;
		trace(warn("copy %u %schunks from %Lx to %Lx", sb->copy_chunks, is_snap? "snapshot ": "", source, sb->dest_exception);)
		assert(size <= sb->copybuf_size);
		pread(is_snap? sb->snapdev: sb->orgdev, sb->copybuf, size, source << sb->image.chunksize_bits);  // 64 bit!!!
		pwrite(sb->snapdev, sb->copybuf, size, sb->dest_exception << sb->image.chunksize_bits);  // 64 bit!!!
		sb->copy_chunks = 0;
	}
	return 0;
}

int copyout(struct superblock *sb, chunk_t chunk, chunk_t exception)
{
#if 1
	if (sb->source_chunk + sb->copy_chunks == chunk &&
		sb->dest_exception + sb->copy_chunks == exception &&
		sb->copy_chunks < sb->copybuf_size >> sb->image.chunksize_bits) {
		sb->copy_chunks++;
		return 0;
	}
	finish_copyout(sb);
	sb->copy_chunks = 1;
	sb->source_chunk = chunk;
	sb->dest_exception = exception;
#else
	int is_snap = sb->source_chunk >> chunk_highbit;
	chunk_t source = chunk & ~((1ULL << chunk_highbit) - 1);
	pread(is_snap? sb->snapdev: sb->orgdev, sb->copybuf, sb->chunksize, source << sb->image.chunksize_bits);  // 64 bit!!!
	pwrite(sb->snapdev, sb->copybuf, sb->chunksize, exception << sb->image.chunksize_bits);  // 64 bit!!!
#endif
	return 0;
}

/*
 * This is the bit that does all the work.  It's rather arbitrarily
 * factored into a probe and test part, then an exception add part,
 * called only if an exception for a given chunk isn't already present
 * in the Btree.  This factoring will change a few more times yet as
 * the code gets more asynchronous and multi-threaded.
 */
chunk_t make_unique(struct superblock *sb, chunk_t chunk, int snapnum)
{
	unsigned levels = sb->image.etree_levels;
	struct etree_path path[levels + 1];
	struct buffer *leafbuf = probe(sb, chunk, path);
	chunk_t exception = 0;
	trace(warn("chunk %Lx, snapnum %i", chunk, snapnum));

	if (snapnum == -1?
		origin_chunk_unique(buffer2leaf(leafbuf), chunk, sb->snapmask):
		snapshot_chunk_unique(buffer2leaf(leafbuf), chunk, snapnum, &exception))
	{
		trace(warn("chunk %Lx already unique in snapnum %i", chunk, snapnum);)
		brelse(leafbuf);
	} else {
		u64 newex = alloc_exception(sb);
// if (snapnum == -1)
		copyout(sb, exception? (exception | (1ULL << chunk_highbit)): chunk, newex);
		add_exception_to_tree(sb, leafbuf, chunk, newex, snapnum, path, levels);
		exception = newex;
	}
	brelse_path(path, levels);
	return exception;
}

int test_unique(struct superblock *sb, chunk_t chunk, int snapnum, chunk_t *exception)
{
	unsigned levels = sb->image.etree_levels;
	struct etree_path path[levels + 1];
	struct buffer *leafbuf = probe(sb, chunk, path);
	trace(warn("chunk %Lx, snapnum %i", chunk, snapnum));
	int result = snapnum == -1?
		origin_chunk_unique(buffer2leaf(leafbuf), chunk, sb->snapmask):
		snapshot_chunk_unique(buffer2leaf(leafbuf), chunk, snapnum, exception);
	brelse(leafbuf);
	brelse_path(path, levels);
	return result;
}

/* Snapshot Store Superblock handling */

u64 calc_snapmask(struct superblock *sb)
{
	u64 mask = 0;
	int i;

	for (i = 0; i < sb->image.snapshots; i++)
		mask |= 1ULL << sb->image.snaplist[i].bit;

	return mask;
}

int tag2snapnum(struct superblock *sb, unsigned tag)
{
	unsigned i, n = sb->image.snapshots;

	for (i = 0; i < n; i++)
		if (sb->image.snaplist[i].tag == tag)
			return sb->image.snaplist[i].bit;

	return -1;
}

int snapnum2tag(struct superblock *sb, unsigned bit)
{
	unsigned i, n = sb->image.snapshots;

	for (i = 0; i < n; i++)
		if (sb->image.snaplist[i].bit == bit)
			return sb->image.snaplist[i].tag;

	return -1;
}

int create_snapshot(struct superblock *sb, unsigned snaptag)
{
	unsigned i, snapshots = sb->image.snapshots;
	struct snapshot *snapshot;

	/* Check tag not already used */
	for (i = 0; i < snapshots; i++)
		if (sb->image.snaplist[i].tag == snaptag)
			return -1;

	/* Find available snapshot bit */
	for (i = 0; i < MAX_SNAPSHOTS; i++)
		if (!(sb->snapmask & (1ULL << i)))
			goto create;
	return -EFULL;

create:
	trace_on(printf("Create snapshot %i (internal %i)\n", snaptag, i);)
	snapshot = sb->image.snaplist + sb->image.snapshots++;
	*snapshot = (struct snapshot){ .tag = snaptag, .bit = i, .create_time = time(NULL) };
	sb->snapmask |= (1ULL << i);
	mark_sb_dirty(sb);
	return i;
};

/*
 * delete_snapshot: remove all exceptions from a given snapshot from a leaf
 * working from top to bottom of the exception list clearing snapshot bits
 * and packing the nonzero exceptions into the top of the block.  Then work
 * from bottom to top in the directory map packing nonempty entries into the
 * bottom of the map.
 */

void delete_snapshots_from_leaf(struct superblock *sb, struct eleaf *leaf, u64 snapmask)
{
	struct exception *p = emap(leaf, leaf->count), *dest = p;
	struct etree_map *pmap, *dmap;
	unsigned i;

	/* Scan top to bottom clearing snapshot bit and moving
	 * non-zero entries to top of block */
	for (i = leaf->count; i--;) {
		while (p != emap(leaf, i))
			if (((--p)->share &= ~snapmask))
				*--dest = *p;
			else
				free_chunk(sb, p->chunk);
		leaf->map[i].offset = (char *)dest - (char *)leaf;
	}
	/* Remove empties from map */
	dmap = pmap = &leaf->map[0];
	for (i = 0; i < leaf->count; i++, pmap++)
		if (pmap->offset != (pmap + 1)->offset)
			*dmap++ = *pmap;
	dmap->offset = pmap->offset;
	dmap->rchunk = 0; // tidy up
	leaf->count = dmap - &leaf->map[0];
}

void delete_snapshots_from_tree(struct superblock *sb, u64 snapmask)
{
	int levels = sb->image.etree_levels, level = -1;
	struct etree_path path[levels];
	struct buffer *nodebuf;
	struct enode *node;

	trace_on(printf("delete snapshot mask %Lx\n", snapmask);)
	while (1) {
 		do {
			level++;
			nodebuf = snapread(sb, level? path[level - 1].pnext++->sector: sb->image.etree_root);
			node = buffer2node(nodebuf);
			path[level].buffer = nodebuf;
			path[level].pnext = node->entries;
			trace(printf("push to level %i, %i nodes\n", level, node->count);)
		} while (level < levels - 1);

		trace(printf("do %i leaf nodes\n", node->count);)
		while (path[level].pnext  < node->entries + node->count) {
			struct buffer *leafbuf = snapread(sb, path[level].pnext++->sector);
			trace_off(printf("process leaf %Lx\n", leafbuf->sector);)
			delete_snapshots_from_leaf(sb, buffer2leaf(leafbuf), snapmask);
			brelse(leafbuf);
		}

		do {
			brelse(nodebuf);
			if (!level)
				return;
			nodebuf = path[--level].buffer;
			node = buffer2node(nodebuf);
			trace(printf("pop to level %i, %i of %i nodes\n", level, path[level].pnext - node->entries, node->count);)
		} while (path[level].pnext == node->entries + node->count);
	};
}

int delete_snapshot(struct superblock *sb, unsigned tag)
{
	struct snapshot *snapshot;
	unsigned i, bit;

	for (i = 0; i < sb->image.snapshots; i++)
		if (sb->image.snaplist[i].tag == tag)
			goto delete;
	return -1;

delete:
	snapshot = sb->image.snaplist + i;
	bit = snapshot->bit;
	trace_on(printf("Delete snapshot %i (internal %i)\n", tag, bit);)
	memmove(snapshot, snapshot + 1, (char *)(sb->image.snaplist + --sb->image.snapshots) - (char *)snapshot);
	sb->snapmask &= ~(1ULL << bit);
	delete_snapshots_from_tree(sb, 1ULL << bit);
	mark_sb_dirty(sb);
	return bit;
};

void show_snapshots(struct superblock *sb)
{
	unsigned snapnum, snapshots = sb->image.snapshots;

	printf("%u snapshots\n", snapshots);
	for (snapnum = 0; snapnum < snapshots; snapnum++) {
		struct snapshot *snapshot = sb->image.snaplist + snapnum;
		printf("snapshot %u tag %u created %x\n", 
			snapshot->bit, 
			snapshot->tag, 
			snapshot->create_time);
	}
};

/* Lock snapshot reads against origin writes */

static void reply(fd_t sock, struct messagebuf *message)
{
	trace(warn("%x/%u", message->head.code, message->head.length);)
	writepipe(sock, &message->head, message->head.length + sizeof(message->head));
}

struct client
{
	u64 id;
	fd_t sock;
	int snap;
};

struct pending
{
	unsigned holdcount;
	struct client *client;
	struct messagebuf message;
};

struct snaplock_wait
{
	struct pending *pending;
	struct snaplock_wait *next;
};

struct snaplock_hold
{
	struct client *client;
	struct snaplock_hold *next;
};

struct snaplock
{
	struct snaplock_wait *waitlist;
	struct snaplock_hold *holdlist;
	struct snaplock *next;
	chunk_t chunk;
};

struct snaplock *new_snaplock(struct superblock *sb)
{
	return malloc(sizeof(struct snaplock));
}

struct snaplock_wait *new_snaplock_wait(struct superblock *sb)
{
	return malloc(sizeof(struct snaplock_wait));
}

struct snaplock_hold *new_snaplock_hold(struct superblock *sb)
{
	return malloc(sizeof(struct snaplock_hold));
}

void free_snaplock(struct superblock *sb, struct snaplock *p)
{
	free(p);
}

void free_snaplock_hold(struct superblock *sb, struct snaplock_hold *p)
{
	free(p);
}

void free_snaplock_wait(struct superblock *sb, struct snaplock_wait *p)
{
	free(p);
}

unsigned snaplock_hash(struct superblock *sb, chunk_t chunk)
{
	return ((u32)(chunk * 3498734713U)) >> (32 - sb->snaplock_hash_bits);
}

struct snaplock *find_snaplock(struct snaplock *list, chunk_t chunk)
{
	for (; list; list = list->next)
		if (list->chunk == chunk)
			return list;
	return NULL;
}

void waitfor_chunk(struct superblock *sb, chunk_t chunk, struct pending **pending)
{
	struct snaplock *lock;
	if ((lock = find_snaplock(sb->snaplocks[snaplock_hash(sb, chunk)], chunk))) {
		if (!*pending) {
			// arguably we should know the client and fill it in here
			*pending = calloc(1, sizeof(struct pending));
			(*pending)->holdcount = 1;
		}
		struct snaplock_wait *wait = new_snaplock_wait(sb);
		wait->pending = *pending;
		wait->next = lock->waitlist;
		lock->waitlist = wait;
		(*pending)->holdcount++;
	}
}

void readlock_chunk(struct superblock *sb, chunk_t chunk, struct client *client)
{
	struct snaplock **bucket = &sb->snaplocks[snaplock_hash(sb, chunk)];
	struct snaplock *lock;

	if (!(lock = find_snaplock(*bucket, chunk))) {
		lock = new_snaplock(sb);
		*lock = (struct snaplock){ .chunk = chunk, .next = *bucket };
		*bucket = lock;
	}
	struct snaplock_hold *hold = new_snaplock_hold(sb);
	hold->client = client;
	hold->next = lock->holdlist;
	lock->holdlist = hold;
}

struct snaplock *release_lock(struct superblock *sb, struct snaplock *lock, struct client *client)
{
	struct snaplock *ret = lock;
	struct snaplock_hold **holdp = &lock->holdlist;
	while (*holdp && (*holdp)->client != client)
		holdp = &(*holdp)->next;

	if (!*holdp) {
		trace_on(printf("chunk %Lx holder %Lu not found\n", lock->chunk, client->id);)
		return NULL;
	}

	/* Delete and free holder record */
	struct snaplock_hold *next = (*holdp)->next;
	free_snaplock_hold(sb, *holdp);
	*holdp = next;

	if (lock->holdlist)
		return ret;

	/* Release and delete waiters, delete lock */
	struct snaplock_wait *list = lock->waitlist;
	while (list) {
		struct snaplock_wait *next = list->next;
		assert(list->pending->holdcount);
		if (!--(list->pending->holdcount)) {
			struct pending *pending = list->pending;
			reply(pending->client->sock, &pending->message);
			free(pending);
		}
		free_snaplock_wait(sb, list);
		list = next;
	}
	ret = lock->next;
	free_snaplock(sb, lock);
	return ret;
}

int release_chunk(struct superblock *sb, chunk_t chunk, struct client *client)
{
	trace(printf("release %Lx\n", chunk);)
	struct snaplock **lockp = &sb->snaplocks[snaplock_hash(sb, chunk)];

	/* Find pointer to lock record */
	while (*lockp && (*lockp)->chunk != chunk)
		lockp = &(*lockp)->next;
	struct snaplock *next, *lock = *lockp;

	if (!lock) {
		trace_on(printf("chunk %Lx not locked\n", chunk);)
		return -1;
	}

	next = release_lock(sb, lock, client);
	if (!next)
		return -2;
	*lockp = next;
	return 0;
}

void show_locks(struct superblock *sb)
{
	unsigned n = 0, i;
	for (i = 0; i < (1 << sb->snaplock_hash_bits); i++) {
		struct snaplock *lock = sb->snaplocks[i];
		if (!lock)
			continue;
		if (!n) printf("Locks:\n");
		printf("[%03u] ", i);
		do {
			printf("chunk %Lx ", lock->chunk);
			struct snaplock_hold *hold = lock->holdlist;
			for (; hold; hold = hold->next)
				printf("held by client %Lu ", hold->client->id);
			struct snaplock_wait *wait = lock->waitlist;
			for (; wait; wait = wait->next)
				printf("wait [%02hx/%u] ", snaplock_hash(sb, (u32)wait->pending), wait->pending->holdcount);
		} while ((lock = lock->next));
		printf("\n");
		n++;
	}
	if (!n) printf("-- no locks --\n");
}

/* Build up a response as a list of chunk ranges */

struct addto
{ 
	unsigned count;
	chunk_t firstchunk; 
	chunk_t nextchunk;
	struct rwmessage *reply;
	shortcount *countp;
	chunk_t *top;
	char *lim;
};

void check_response_full(struct addto *r, unsigned bytes)
{
	if ((char *)r->top < r->lim - bytes)
		return;
	error("Need realloc");
}

void addto_response(struct addto *r, chunk_t chunk)
{
	if (chunk != r->nextchunk) {
		if (r->top) {
			trace_off(warn("finish old range\n");)
			*(r->countp) = (r->nextchunk -  r->firstchunk);
		} else {
			trace_off(warn("alloc new reply");)
			r->reply = (void *) malloc(sizeof(struct messagebuf));
			r->top = (chunk_t *)(((char *)r->reply) + sizeof(struct head) + offsetof(struct rw_request, ranges));
			r->lim = ((char *)r->reply) + maxbody;
			r->count++;
		}
		trace_off(warn("start new range");)
		check_response_full(r, 2*sizeof(chunk_t));
		r->firstchunk = *(r->top)++ = chunk;
		r->countp = (shortcount *)r->top;
		r->top = (chunk_t *)(((shortcount *)r->top) + 1);
	}
	r->nextchunk = chunk + 1;
}

int finish_reply_(struct addto *r, unsigned code, unsigned id)
{
	if (!r->countp)
		return 0;

	*(r->countp) = (r->nextchunk -  r->firstchunk);
	r->reply->head.code = code;
	r->reply->head.length = (char *)r->top - (char *)r->reply - sizeof(struct head);
	r->reply->body.id = id;
	r->reply->body.count = r->count;
	return 1;
}

void finish_reply(int sock, struct addto *r, unsigned code, unsigned id)
{
	if (finish_reply_(r, code, id))
		reply(sock, (struct messagebuf *)r->reply);
	free(r->reply);
}

/* Initialization, State load/save */

void setup_sb(struct superblock *sb)
{
	unsigned blocksize_bits = sb->image.blocksize_bits;
	unsigned chunksize_bits = sb->image.blocksize_bits;
	sb->blocksize = 1 << blocksize_bits;
	sb->chunksize = 1 << chunksize_bits, 
	sb->sectors_per_block_bits = blocksize_bits - SECTOR_BITS;
	sb->sectors_per_chunk_bits = chunksize_bits - SECTOR_BITS;
	sb->keys_per_node = (sb->blocksize - offsetof(struct enode, entries)) / sizeof(struct index_entry);
#ifdef BUSHY
	sb->keys_per_node = 10;
#endif
	sb->copybuf = malloc_aligned(sb->copybuf_size = (32 * sb->chunksize), 4096); // !!! check failed
	sb->sectors_per_block = 1 << sb->sectors_per_block_bits;
	sb->sectors_per_chunk = 1 << sb->sectors_per_chunk_bits;
	sb->snapmask = 0;
	sb->flags = 0;

	sb->max_commit_blocks = (sb->blocksize - sizeof(struct commit_block)) / sizeof(sector_t);

	unsigned snaplock_hash_bits = 8;
	sb->snaplock_hash_bits = snaplock_hash_bits;
	sb->snaplocks = (struct snaplock **)calloc(1 << snaplock_hash_bits, sizeof(struct snaplock *));
}

void load_sb(struct superblock *sb)
{
	struct buffer *buffer = bread(sb->snapdev, SB_LOC, 4096);
	memcpy(&sb->image, buffer->data, sizeof(sb->image));
	assert(!memcmp(sb->image.magic, SB_MAGIC, sizeof(sb->image.magic)));
	brelse(buffer);
	setup_sb(sb);
	sb->snapmask = calc_snapmask(sb);
	trace_on(printf("Active snapshot mask: %016llx\n", sb->snapmask);)
}

void save_sb(struct superblock *sb)
{
	if (sb->flags & SB_DIRTY) {
		struct buffer *buffer = getblk(sb->snapdev, SB_LOC, 4096);
		memcpy(buffer->data, &sb->image, sizeof(sb->image));
		write_buffer(buffer);
		brelse(buffer);
		sb->flags &= ~SB_DIRTY;
	}
}

void save_state(struct superblock *sb)
{
	flush_buffers();
	save_sb(sb);
}

/*
 * This source compiles either the snapshot server or the snapshot store setup
 * utility, depending on whether the macro variable CREATE is defined.
 *
 * I'll leave all the testing hooks lying around in the main routine for now,
 * since the low level components still tend to break every now and then and
 * require further unit testing.
 */

int init_snapstore(struct superblock *sb)
{
	int i, error;

	unsigned sectors_per_block_bits = 3;
	sb->image = (struct disksuper){ .magic = SB_MAGIC };
	sb->image.etree_levels = 1,
	sb->image.blocksize_bits = SECTOR_BITS + sectors_per_block_bits;
	sb->image.chunksize_bits = sb->image.blocksize_bits; // !!! just for now
	setup_sb(sb);

	u64 size;
	if ((error = fd_size(sb->snapdev, &size)))
		error("Error %i: %s determining snapshot store size", error, strerror(error));
	sb->image.chunks = size >> sb->image.chunksize_bits;
	if ((error = fd_size(sb->orgdev, &size)))
		error("Error %i: %s determining origin volume size", errno, strerror(errno));
	sb->image.orgchunks = size >> sb->image.chunksize_bits;

	sb->image.journal_size = 100;
#ifdef TEST_JOURNAL
	sb->image.journal_size = 5;
#endif
	sb->image.journal_next = 0;
	sb->image.sequence = sb->image.journal_size;
	init_allocation(sb);
	mark_sb_dirty(sb);

	for (i = 0; i < sb->image.journal_size; i++) {
		struct buffer *buffer = jgetblk(sb, i);
		struct commit_block *commit = (struct commit_block *)buffer->data;
		*commit = (struct commit_block){ .magic = JMAGIC, .sequence = i };
#ifdef TEST_JOURNAL
		commit->sequence = (i + 3) % 5 ;
#endif
		commit->checksum = -checksum_block(sb, (void *)commit);
		brelse_dirty(buffer);
	}
#ifdef TEST_JOURNAL
show_journal(sb);
show_tree(sb);
flush_buffers();
recover_journal(sb);
show_buffers();
#endif

#if 0
	printf("chunk = %Lx\n", alloc_chunk_range(sb, sb->image.chunks - 1, 1));
//	struct buffer *buffer = snapread(sb, sb->image.bitmap_base + 3 * 8);
//	dump_buffer(buffer, 4090, 6);
return 0;
#endif

#if 0
	grab_chunk(sb, 32769);
	struct buffer *buffer = snapread(sb, sb->image.bitmap_base + 8);
	printf("sector %Lx\n", buffer->sector);
	free_chunk(sb, 32769);
return 0;
#endif

	struct buffer *leafbuf = new_leaf(sb);
	struct buffer *rootbuf = new_node(sb);
	buffer2node(rootbuf)->count = 1;
	buffer2node(rootbuf)->entries[0].sector = leafbuf->sector;
	sb->image.etree_root = rootbuf->sector;

#if 0
	printf("chunk = %Lx\n", alloc_chunk(sb));
	printf("chunk = %Lx\n", alloc_chunk(sb));
	printf("chunk = %Lx\n", alloc_chunk(sb));
	printf("chunk = %Lx\n", alloc_chunk(sb));
	printf("chunk = %Lx\n", alloc_chunk(sb));
	printf("chunk = %Lx\n", alloc_chunk(sb));
	printf("chunk = %Lx\n", alloc_chunk(sb));
//	free_chunk(sb, 23);
	printf("chunk = %Lx\n", alloc_chunk(sb));
	printf("chunk = %Lx\n", alloc_chunk(sb));
	printf("chunk = %Lx\n", alloc_chunk(sb));
	printf("chunk = %Lx\n", alloc_chunk(sb));
return 0;
#endif

	brelse_dirty(rootbuf);
	brelse_dirty(leafbuf);
#ifdef TEST_JOURNAL
	show_buffers();
	show_dirty_buffers();
	commit_transaction(sb);
	evict_buffers();

	show_journal(sb);
	show_tree(sb);
	recover_journal(sb);
	evict_buffers();
	show_tree(sb);
#endif
	save_state(sb);
	return 0;
}

int client_locks(struct superblock *sb, struct client *client, int check)
{
	int i;

	for (i = 0; i < (1 << sb->snaplock_hash_bits); i++) {
		struct snaplock **lockp = &sb->snaplocks[i];

		while (*lockp) {
			struct snaplock_hold *hold;

			for (hold = (*lockp)->holdlist; hold; hold = hold->next)
				if (hold->client == client) {
					if (check)
						return 1;
					*lockp = release_lock(sb, *lockp, client);
					goto next;
				}
			lockp = &(*lockp)->next;
next:
			continue;
		}
	}
	return 0;
}

#define check_client_locks(x, y) client_locks(x, y, 1)
#define free_client_locks(x, y) client_locks(x, y, 0)

/*
 * Responses to IO requests take two quite different paths through the
 * machinery:
 *
 *   - Origin write requests are just sent back with their message
 *     code changed, unless they have to wait for a snapshot read
 *     lock in which case the incoming buffer is copied and the
 *     response takes a kafkaesque journey through the read locking
 *     beaurocracy.
 *
 *   - Responses to snapshot read or write requests have to be built
 *     up painstakingly in allocated buffers, keeping a lot of state
 *     around so that they end up with a minimum number of contiguous
 *     chunk ranges.  Once complete they can always be sent
 *     immediately.
 *
 * To mess things up further, snapshot read requests can return both
 * a list of origin ranges and a list of snapshot store ranges.  In
 * the latter case the specific snapshot store chunks in each logical
 * range are also returned, because they can (normally will) be
 * discontiguous.  This goes back to the client in two separate
 * messages, on the theory that the client will find it nice to be
 * able to process the origin read ranges and snapshot read chunks
 * separately.  We'll see how good an idea that is.
 *
 * The implementation ends up looking nice and tidy, but appearances
 * can be deceiving.
 */
int incoming(struct superblock *sb, struct client *client)
{
	struct messagebuf message;
	unsigned sock = client->sock;
	int i, j, err;

	if ((err = readpipe(sock, &message.head, sizeof(message.head))))
		goto pipe_error;
	trace(warn("%x/%u", message.head.code, message.head.length);)
	if (message.head.length > maxbody)
		goto message_too_long;
	if ((err = readpipe(sock, &message.body, message.head.length)))
		goto pipe_error;

	switch (message.head.code) {
		case QUERY_WRITE:
		if (client->snap == -1) {
			struct pending *pending = NULL;
			struct rw_request *body = (struct rw_request *)message.body;
			struct chunk_range *p = body->ranges;
			chunk_t chunk;
			if (message.head.length < sizeof(*body))
				goto message_too_short;
			trace(printf("origin write query, %u ranges\n", body->count);)

			for (i = 0; i < body->count; i++, p++)
				for (j = 0, chunk = p->chunk; j < p->chunks; j++, chunk++)
					if (make_unique(sb, chunk, -1))
						waitfor_chunk(sb, chunk, &pending);
			finish_copyout(sb);
			commit_transaction(sb);

			message.head.code = REPLY_ORIGIN_WRITE;
			if (pending) {
				pending->client = client;
				memcpy(&pending->message, &message, message.head.length + sizeof(struct head));
				pending->holdcount--;
				break;
			}
			reply(sock, &message);
			break;
		} else {
			struct rw_request *body = (struct rw_request *)message.body;
			if (message.head.length < sizeof(*body))
				goto message_too_short;
			trace(printf("snapshot write request, %u ranges\n", body->count);)
			struct addto snap = { .nextchunk = -1 };

			for (i = 0; i < body->count; i++)
				for (j = 0; j < body->ranges[i].chunks; j++) {
					chunk_t chunk = body->ranges[i].chunk + j;
					chunk_t exception = make_unique(sb, chunk, client->snap);
					trace(printf("exception = %Lx\n", exception);)
					addto_response(&snap, chunk);
					check_response_full(&snap, sizeof(chunk_t));
					*(snap.top)++ = exception;
				}
			finish_copyout(sb);
			commit_transaction(sb);
			finish_reply(client->sock, &snap, REPLY_SNAPSHOT_WRITE, body->id);
			break;
		}

		case QUERY_SNAPSHOT_READ:
		{
			struct rw_request *body = (struct rw_request *)message. body;
			if (message.head.length < sizeof(*body))
				goto message_too_short;
			trace(printf("snapshot read request, %u ranges\n", body->count);)
			struct addto snap = { .nextchunk = -1 }, org = { .nextchunk = -1 };

			for (i = 0; i < body->count; i++)
				for (j = 0; j < body->ranges[i].chunks; j++) {
					chunk_t chunk = body->ranges[i].chunk + j, exception = 0;
					trace(warn("read %Lx", chunk));
					test_unique(sb, chunk, client->snap, &exception);
					if (exception) {
						trace(warn("read exception %Lx", exception));
						addto_response(&snap, chunk);
						check_response_full(&snap, sizeof(chunk_t));
						*(snap.top)++ = exception;
					} else {
						trace(warn("read origin %Lx", chunk));
						addto_response(&org, chunk);
						readlock_chunk(sb, chunk, client);
					}
				}
			finish_reply(client->sock, &org, REPLY_SNAPSHOT_READ_ORIGIN, body->id);
			finish_reply(client->sock, &snap, REPLY_SNAPSHOT_READ, body->id);
			break;
		}

		case FINISH_SNAPSHOT_READ:
		{
			struct rw_request *body = (struct rw_request *)message.body;
			if (message.head.length < sizeof(*body))
				goto message_too_short;
			trace(printf("finish snapshot read, %u ranges\n", body->count);)

			for (i = 0; i < body->count; i++)
				for (j = 0; j < body->ranges[i].chunks; j++)
					release_chunk(sb, body->ranges[i].chunk + j, client);

			break;
		}

		case IDENTIFY:
		{
			int tag = ((struct identify *)message.body)->snap, snap = tag2snapnum(sb, tag);
			if (snap >= 0)
				client->snap = snap;
			client->id = ((struct identify *)message.body)->id;
			warn("client id %Li, snapshot %i (snapnum %i)", client->id, tag, snap);
			outbead(sock, REPLY_IDENTIFY, struct { });
			break;
		}

		case UPLOAD_LOCK:
			break;

		case FINISH_UPLOAD_LOCK:
			break;

		case CREATE_SNAPSHOT:
			create_snapshot(sb, ((struct create_snapshot *)message.body)->snap);
			save_state(sb);
			outbead(sock, REPLY_CREATE_SNAPSHOT, struct { });
			break;

		case DELETE_SNAPSHOT:
			delete_snapshot(sb, ((struct create_snapshot *)message.body)->snap);
			save_state(sb);
			outbead(sock, REPLY_DELETE_SNAPSHOT, struct { });
			break;

		case INITIALIZE_SNAPSTORE:
			init_snapstore(sb);
			break;

		case DUMP_TREE:
			show_tree(sb);
			break;

		case START_SERVER:
			warn("Activating server");
			load_sb(sb);
			if (sb->image.flags & SB_BUSY) {
				warn("Server was not shut down properly");
				jtrace(show_journal(sb);)
				recover_journal(sb);
			} else {
				sb->image.flags |= SB_BUSY;
				mark_sb_dirty(sb);
				save_sb(sb);
			}
			break;

		case SHUTDOWN_SERVER:
			return -2;

		default: 
			outbead(sock, REPLY_ERROR, struct { int code; char error[50]; }, message.head.code, "Unknown message"); // wrong!!!
	}

#if 0
	static int messages = 0;
	if (++messages == 5) {
		warn(">>>>Simulate server crash<<<<");
		exit(1);
	}
#endif
	return 0;

message_too_long:
	warn("message %x too long (%u bytes)\n", message.head.code, message.head.length);
	return -1;
message_too_short:
	warn("message %x too short (%u bytes)\n", message.head.code, message.head.length);
	return -1;
pipe_error:
	return -1; /* we quietly drop the client if the connect breaks */
}

/* Signal Delivery via pipe */

static int sigpipe;

void sighandler(int signum)
{
	trace_off(printf("caught signal %i\n", signum);)
	write(sigpipe, (char[]){signum}, 1);
}

int cleanup(struct superblock *sb)
{
	warn("cleaning up");
	sb->image.flags &= ~SB_BUSY;
	mark_sb_dirty(sb);
	save_state(sb);
	return 0;
}

int resolve_host(char *name, int family, void *result, int length)
{
	struct hostent host, *bogus;
	char work[500];
	int err, dumb;

	if ((err = gethostbyname2_r(name, family, &host, work, sizeof(work), &bogus, &dumb))) {
		errno = err;
		return -1;
	}
	memcpy(result, host.h_addr_list[0], host.h_length);
	return host.h_length;
}

int resolve_self(int family, void *result, int length)
{
	char name[HOST_NAME_MAX + 1];
	if (gethostname(name, HOST_NAME_MAX) == -1)
		return -1;

	return resolve_host(name, family, result, length);
}

int csnap_server(struct superblock *sb, char *sockname, int port)
{
	unsigned maxclients = 100, clients = 0, others = 3;
	struct client *clientvec[maxclients];
	struct pollfd pollvec[others+maxclients];
	int listener, getsig, pipevec[2], err = 0;

	if (pipe(pipevec))
		error("Can't open pipe");
	sigpipe = pipevec[1];
	getsig = pipevec[0];

	struct server server = { .port = htons(port), .type = AF_INET,  };

	if ((listener = socket(AF_INET, SOCK_STREAM, 0)) < 0) 
		error("Can't get socket");

	if (bind(listener,
		(struct sockaddr *)&(struct sockaddr_in){
			.sin_family = server.type, 
			.sin_port = server.port,
			.sin_addr = { .s_addr = INADDR_ANY } },
		sizeof(struct sockaddr_in)) < 0) 
		error("Can't bind to socket");
	listen(listener, 5);

	warn("csnap server bound to port %i", port);

	/* Get agent connection */
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	int addr_len = sizeof(addr) - sizeof(addr.sun_path) + strlen(sockname);
	int sock, len;

	trace(warn("Connect to control socket %s", sockname);)
	if ((sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
		error("Can't get socket");
	strncpy(addr.sun_path, sockname, sizeof(addr.sun_path));
	if (sockname[0] == '@')
		addr.sun_path[0] = 0;

	if (connect(sock, (struct sockaddr *)&addr, addr_len) == -1)
		error("Can't connect to control socket");

	trace_on(warn("Received control connection");)
	pollvec[2] = (struct pollfd){ .fd = sock, .events = POLLIN };

	if ((len = resolve_self(AF_INET, server.address, sizeof(server.address))) == -1)
		error("Can't get own address, %s (%i)", strerror(errno), errno);
	server.address_len = len;
	warn("host = %x/%u", *(int *)server.address, server.address_len);
	writepipe(sock, &(struct head){ SERVER_READY, sizeof(server) }, sizeof(struct head));
	writepipe(sock, &server, sizeof(server));

#if 1
	switch (fork()) {
	case -1:
		error("fork failed");
	case 0: // !!! daemonize goes here
		break;
	default:
		return 0;
	}
#endif

	pollvec[0] = (struct pollfd){ .fd = listener, .events = POLLIN };
	pollvec[1] = (struct pollfd){ .fd = getsig, .events = POLLIN };

	signal(SIGINT, sighandler);
	signal(SIGTERM, sighandler);
	signal(SIGPIPE, SIG_IGN);

	while (1) {
		int activity = poll(pollvec, others+clients, -1);

		if (activity < 0) {
			if (errno != EINTR)
				error("poll failed, %s", strerror(errno));
			continue;
		}

		if (!activity) {
			printf("waiting...\n");
			continue;
		}

		/* New connection? */
		if (pollvec[0].revents) {
			struct sockaddr_in addr;
			int addr_len = sizeof(addr), sock;

			if (!(sock = accept(listener, (struct sockaddr *)&addr, &addr_len)))
				error("Cannot accept connection");

			trace_on(warn("Received connection");)
			assert(clients < maxclients); // !!! send error and disconnect

			struct client *client = malloc(sizeof(struct client));
			*client = (struct client){ .sock = sock };
			clientvec[clients] = client;
			pollvec[others+clients] = (struct pollfd){ .fd = sock, .events = POLLIN };
			clients++;
		}

		/* Signal? */
		if (pollvec[1].revents) {
			u8 sig = 0;
			/* it's stupid but this read also gets interrupted, so... */
			do { } while (read(getsig, &sig, 1) == -1 && errno == EINTR);
			trace_on(warn("caught signal %i", sig);)
			cleanup(sb); // !!! don't do it on segfault
			if (sig == SIGINT) { 
		        	signal(SIGINT, SIG_DFL);
        			kill(getpid(), sig); /* commit harikiri */
			}
			goto done;
		}

		/* Agent message? */
		if (pollvec[2].revents)
			incoming(sb, &(struct client){ .sock = sock, .id = -2, .snap = -2 });

		/* Client message? */
		unsigned i = 0;
		while (i < clients) {
			if (pollvec[others+i].revents) { // !!! check for poll error
				struct client *client = clientvec[i];
				int result;

				trace_off(printf("event on socket %i = %x\n", client->sock, pollvec[others+i].revents);)
				if ((result = incoming(sb, client)) == -1) {
					warn("Client %Li disconnected", client->id);
					save_state(sb); // !!! just for now
					close(client->sock);
					free(client);
					--clients;
					clientvec[i] = clientvec[clients];
					pollvec[others + i] = pollvec[others + clients];
					continue;
				}

				if (result == -2) { // !!! wrong !!!
					cleanup(sb);
					goto done;
				}
			}
			i++;
		}
	}
done:
	// in a perfect world we'd close all the connections
	close(listener);
	return err;
}

int main(int argc, char *argv[])
{
	struct superblock *sb = &(struct superblock){};

	init_buffers();
#ifdef SERVER
	if (argc < 5)
		error("usage: %s dev/snapshot dev/origin socket port", argv[0]);
#else
	if (argc < 3)
		error("usage: %s dev/snapshot dev/origin", argv[0]);
#endif
	if (!(sb->snapdev = open(argv[1], O_RDWR | O_DIRECT)))
		error("Could not open snapshot store %s", argv[1]);

	if (!(sb->orgdev = open(argv[2], O_RDONLY | O_DIRECT)))
		error("Could not open origin volume %s", argv[2]);
#ifdef SERVER
	return csnap_server(sb, argv[3], atoi(argv[4]));
#else
	return init_snapstore(sb); 
#endif

	void *useme = _show_journal;
	useme = useme;
}
