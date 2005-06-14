/*
 * Cluster Mirror Metadata Server
 *
 * Daniel Phillips,  May 2003 to Nov 2004
 * (c) 2003 Sistina Software Inc.
 * (c) 2004 Red Hat Software Inc.
 *
 */

#define _GNU_SOURCE /* O_DIRECT  */
#define _XOPEN_SOURCE 500 /* pread, pwrite */
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
#include "ddraid.h"
#include "../dm-ddraid.h"
#include "trace.h"

/*
 * To do:
 *   - sync and increment highwater (think about volume size)
 *   - download highwater updates
 *   - use list of clients instead of count on region
 *   - delay write grant if sync in progress
 *   - flush deferred cleans on low traffic or traffic timeout
 */

//#define TEST

#define trace trace_off
#define tracelog trace_off
#define SB_LOC 8
#define MAX_MEMBERS 10

typedef int fd_t;

/* Misc bits */

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

unsigned available(unsigned sock)
{
	unsigned bytes;
	ioctl(sock, FIONREAD, &bytes);
	trace(if (bytes) printf("%u bytes waiting\n", bytes);)
	return bytes;
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

#define COMBSORT(n, i, j, COMPARE, EXCHANGE) if (n) { \
	unsigned gap = n, more, i; \
	do { \
		if (gap > 1) gap = gap*10/13; \
		if (gap - 9 < 2) gap = 11; \
		for (i = n - 1, more = gap > 1; i >= gap; i--) { \
			int j = i - gap; \
			if (COMPARE) { EXCHANGE; more = 1; } } \
	} while (more); }

#define HASH_BUCKETS_BITS 8
#define HASH_BUCKETS (1 << HASH_BUCKETS_BITS)
#define MASK_BUCKETS (HASH_BUCKETS - 1)
#define MAX_CLIENTS 100

struct client { unsigned id, sock; };
struct grant { fd_t sock; region_t regnum; };

struct superblock {
	/* Persistent configuration saved to disk */
	struct {
		u64 flags;
		u32 blocksize_bits, regionsize_bits;
		u32 journal_base, journal_size;
	} image;

	/* Other state not saved to disk */
	u32 blocksize, regionsize, max_entries;
	unsigned flags;
	fd_t logdev;
	unsigned members;
	fd_t member[MAX_MEMBERS];
	unsigned clients;
	struct client client[MAX_CLIENTS];
	struct list_head hash[HASH_BUCKETS];
	unsigned copybuf_size;
	char *copybuf;
	struct buffer *oldbuf, *newbuf;
	unsigned newest_block;
	region_t highwater;
	region_t cleanmask;
	unsigned sectorshift;
	struct grant *grant;
	unsigned grants;
	region_t *unsync;
	unsigned unsyncs;
	fd_t sync_sock;
	struct list_head deferred_clean;
	unsigned timeout;
};

#define SB_BUSY 1

#define SB_DIRTY 1
#define STUCK_FLAG 2

static inline unsigned log2sector(struct superblock *sb, unsigned i)
{
	return (i << sb->sectorshift) + sb->image.journal_base;
}

struct buffer *readlog(struct superblock *sb, sector_t i)
{
	return bread(sb->logdev, log2sector(sb, i), sb->blocksize);
}

/* Journal operations */

struct journal_block
{
	u32 checksum;
	u32 sequence;
	u16 oldest_block; 
	u16 oldest_entry; 
	region_t highwater;
	u32 entries;
	region_t entry[] PACKED;
};

struct region
{
	int dirty;
	region_t regnum;
	atomic_t count;
	unsigned flags;
	unsigned dirtied_block; 
	unsigned dirtied_entry; 
	struct list_head hash;
	struct list_head list;
	struct list_head deferred_clean;
};

#define REGION_UNSYNCED_FLAG 1
#define REGION_DEFER_CLEAN_FLAG 2

static inline int is_unsynced(struct region *region)
{
	return region->flags & REGION_UNSYNCED_FLAG;
}

static inline unsigned hash_region(region_t value)
{
	return value & MASK_BUCKETS;
}

static struct region *find_region(struct superblock *info, region_t regnum)
{
	struct list_head *list, *bucket = info->hash + hash_region(regnum);
	struct region *region;

	trace_off(warn("Find region %Lx", (long long)regnum);)
	list_for_each(list, bucket)
		if ((region = list_entry(list, struct region, hash))->regnum == regnum)
			goto found;
	trace_off(warn("Region %Lx not found", (long long)regnum);)
	return NULL;
found:
	return region;
}

struct region *add_region(struct superblock *info, region_t regnum)
{
	struct region *region = malloc(sizeof(struct region));
	*region = (struct region){ .regnum = regnum };
	list_add(&region->hash, info->hash + hash_region(regnum));
	return region;
}

void del_region(struct superblock *sb, struct region *region)
{
	list_del(&region->hash);
	free(region);
}

static void show_regions(struct superblock *info)
{
	unsigned i, regions = 0;

	for (i = 0; i < HASH_BUCKETS; i++) {
		struct list_head *list;
		list_for_each(list, info->hash + i) {
			struct region *region = list_entry(list, struct region, hash);
			printf(is_unsynced(region)? "*": "");
			printf("%Lx/%i ", (long long)region->regnum, atomic_read(&region->count));
			regions++;
		}
	}
	printf("(%u)\n", regions);
}

static u32 checksum_block(struct superblock *sb, u32 *data)
{
	int i, sum = 0;
	for (i = 0; i < sb->image.blocksize_bits >> 2; i++)
		sum += data[i];
	return sum;
}

static void empty_block(struct superblock *sb, struct journal_block *block, u32 sequence, int i)
{
	*block = (struct journal_block){ .sequence = sequence, .oldest_block = i };
	block->checksum = -checksum_block(sb, (void *)block);
}

static inline struct journal_block *buf2block(struct buffer *buf)
{
	return (struct journal_block *)buf->data;
}

static void add_entry(struct superblock *sb,  region_t entry)
{
	struct journal_block *newest = buf2block(sb->newbuf);
	newest->entry[newest->entries++] = entry;
}

// Deferred cleans:
//
// Needed for two reasons: 1) in a journal full state there's nowhere to put an
// incoming clean, so defer it.  2) Without deferred cleans, synchronous write
// performance suffer.  But actually, the second case is better handled on the
// client side, which eliminates the network round trip at no cost except some 
// additional complexity, e.g., a timer.

// Journal full state is a real problem because incoming requests may block the
// releases we need to retire the oldest journal block.  We could buffer up the
// grants, but that is unbounded.  Instead we put all clients into a pause state
// where no further requests are submitted, but releases are of course allowed.
// Region drain commands go out to all clients holding each region in the oldest
// journal block dirty.  Requests already in the pipeline are bounced back to
// the client.  After a while, only releases should be coming in, which are all
// defered, however, after each release we retire as many old entries as possible
// which will retire some of those deferred releases.  Eventually, the oldest
// block must become empty or some client is stuck.  The oldest block is retired
// and resume commands go out to all clients.  The clients then resumit bounced
// requests along with any new requests that arrived in the meantime.  Chances
// are, this will fill the journal again and the cycle above repeats.  Somewhere
// in all this we should try to log a few deferred releases.  It's far from clear
// what the optimal balance is.  The easiest thing to do is "never log" except
// when log traffic runs out.  Then we need a timer to close out the deferred
// clean list, which we need in any case.

static void add_deferred_clean(struct superblock *sb, struct region *region)
{
	region->flags |= REGION_DEFER_CLEAN_FLAG;
	list_add(&region->deferred_clean, &sb->deferred_clean);
}

static void del_deferred_clean(struct superblock *sb, struct region *region)
{
	region->flags &= ~REGION_DEFER_CLEAN_FLAG;
	list_del(&region->deferred_clean);

	assert(!atomic_read(&region->count));
	if (!(region->flags & REGION_UNSYNCED_FLAG))
		del_region(sb, region);
}

static void retire_old_block(struct superblock *sb)
{
	struct journal_block *newest = buf2block(sb->newbuf);

	trace(warn("retire old block, seq=%u", buf2block(sb->oldbuf)->sequence);)
	brelse(sb->oldbuf);
	newest->oldest_entry = 0;
	if (++newest->oldest_block == sb->image.journal_size)
		newest->oldest_block = 0;
	if (newest->oldest_block == sb->newest_block) {
		trace(warn("retired all old blocks");)
		(sb->oldbuf = sb->newbuf)->count++;
	} else
		sb->oldbuf = readlog(sb, newest->oldest_block);
}

static void retire_old_entries(struct superblock *sb)
{
	struct journal_block *newest = buf2block(sb->newbuf);

	while (1) {
		while (newest->oldest_entry == buf2block(sb->oldbuf)->entries)
			retire_old_block(sb);

		if (sb->oldbuf == sb->newbuf || newest->entries == sb->max_entries)
			break;

		region_t entry = buf2block(sb->oldbuf)->entry[newest->oldest_entry];

		if (!(entry & sb->cleanmask)) {
			struct region *region = find_region(sb, entry);

			if (region) {
				if (atomic_read(&region->count) &&
				    region->dirtied_block == newest->oldest_block &&
				    region->dirtied_entry == newest->oldest_entry) {
					trace(warn("keep old entry %Lx", (long long)entry);)
					region->dirtied_block = sb->newest_block;
					region->dirtied_entry = newest->entries;
					newest->entry[newest->entries++] = entry;
				} else {
					trace(warn("drop old entry %Lx", (long long)entry);)
					if (region->flags & REGION_DEFER_CLEAN_FLAG)
						del_deferred_clean(sb, region);
				}
			}
		}
		newest->oldest_entry++;
	}
}

static int try_to_retire_old_entries(struct superblock *sb)
{
	struct journal_block *newest = buf2block(sb->newbuf);
	struct journal_block *oldest = buf2block(sb->newbuf);

	while (newest->oldest_entry < oldest->entries) {
		region_t entry = oldest->entry[newest->oldest_entry];

		if (!(entry & sb->cleanmask)) {
			struct region *region = find_region(sb, entry);

			if (region) {
				if (atomic_read(&region->count) &&
				    region->dirtied_block == newest->oldest_block &&
				    region->dirtied_entry == newest->oldest_entry) {
					return 0;
				} else {
					trace(warn("drop old entry %Lx", (long long)entry);)
					if (region->flags & REGION_DEFER_CLEAN_FLAG)
						del_deferred_clean(sb, region);
				}
			}
		}
		newest->oldest_entry++;
	}
	return 1;
}

static void send_grants(struct superblock *sb)
{
	int i;
	for (i = 0; i < sb->grants; i++) {
		fd_t sock = sb->grant[i].sock;
		region_t regnum = sb->grant[i].regnum;
		unsigned reply = GRANT_UNSYNCED;

		if (regnum < sb->highwater) {
			struct region *region = find_region(sb, regnum);
			if (region && !(region->flags & REGION_UNSYNCED_FLAG))
				reply = GRANT_SYNCED;
		}
		outbead(sock, reply, struct region_message, .regnum = regnum);
	}
	sb->grants = 0;
}

static void advance_new_block(struct superblock *sb, int commit)
{
	struct journal_block *newest = buf2block(sb->newbuf);
	u32 sequence = newest->sequence;
	unsigned oldest_block = newest->oldest_block;
	unsigned oldest_entry = newest->oldest_entry;

	if (commit) {
		tracelog(warn("committed block, seq=%u", sequence);)
		// fill top with zero (should we?)
		// memset(&newest->entry[newest->entries], 0, sizeof(region_t) * (sb->max_entries - newest->entries));
		newest->checksum = 0;
		newest->checksum = -checksum_block(sb, (void *)newest);
		write_buffer(sb->newbuf);
		send_grants(sb);
	}

	brelse(sb->newbuf);
	if (++sb->newest_block == sb->image.journal_size)
		sb->newest_block = 0;

	if (newest->oldest_block == sb->newest_block) {
		warn("log full, losing dirty state!");
		retire_old_block(sb);
	}

	tracelog(warn("new block, pos=%i seq=%u oldest=%i:%i", sb->newest_block, sequence + 1, oldest_block, oldest_entry);)
	sb->newbuf = getblk(sb->logdev, log2sector(sb, sb->newest_block), sb->blocksize);
	*buf2block(sb->newbuf) = (struct journal_block){
		.sequence = sequence + 1,
		.oldest_block = oldest_block,
		.oldest_entry = oldest_entry,
	};

	sb->timeout = 100;
}

void try_to_advance(struct superblock *sb)
{
	struct journal_block *newest = buf2block(sb->newbuf);

	if ((sb->newest_block + 1) % sb->image.journal_size != newest->oldest_block) {
		advance_new_block(sb, 1);
		return;
	}

	int i;
	for (i = 0; i < sb->clients; i++)
		outbead(sb->client[i].sock, PAUSE_REQUESTS, struct { });

	sb->flags |= STUCK_FLAG;
}

void advance_if_full(struct superblock *sb)
{
	if (buf2block(sb->newbuf)->entries == sb->max_entries)
		try_to_advance(sb);
}

static void commit(struct superblock *sb)
{
	if (buf2block(sb->newbuf)->entries) {
		retire_old_entries(sb);
		try_to_advance(sb);
	}
}

static void log_deferred_cleans(struct superblock *sb)
{
	warn("");

	while (!list_empty(&sb->deferred_clean) && !(sb->flags & STUCK_FLAG)) {
		struct list_head *entry = sb->deferred_clean.next;
		struct region *region = list_entry(entry, struct region, list);
		assert(region->flags & REGION_DEFER_CLEAN_FLAG);
		add_entry(sb,  region->regnum | sb->cleanmask);
		del_deferred_clean(sb, region);
		advance_if_full(sb);
	};
}

static int get_region(struct superblock *sb, region_t regnum)
{
	struct region *region = find_region(sb, regnum);

	if (!region)
		region = add_region(sb, regnum);

	if (atomic_read(&region->count)) {
		atomic_inc(&region->count);
		return 0;
	}

	trace(warn("+%Lx", (long long)regnum);)
	atomic_inc(&region->count);

	if (region->flags & REGION_DEFER_CLEAN_FLAG) {
		del_deferred_clean(sb, region);
		return 1;
	}

	region->dirtied_block = sb->newest_block;
	region->dirtied_entry = buf2block(sb->newbuf)->entries;
	add_entry(sb, regnum);
	return 1;
}

static void put_region(struct superblock *sb, region_t regnum)
{
	struct region *region = find_region(sb, regnum);
	if (!region || !atomic_read(&region->count)) {
		warn("redundant clean");
		return;
	}

	if (atomic_dec_and_test(&region->count)) {
		// We can't sanely bounce releases because the client won't know when
		// to resubmit them, and anyway, that works against shrinking the
		// log.  But we can defer releases pretty much indefinitely, and the
		// only downside is, we'll see additional dirty regions on crash or
		// failover.
		// If we don't have room in the journal to add the release, leave the
		// region in the hash with zero count and put the region on a defered
		// release queue.
		if ((sb->flags & STUCK_FLAG)) {
			add_deferred_clean(sb, region);
			return;
		}

		del_region(sb, region);
		add_entry(sb,  regnum | sb->cleanmask);
		trace(warn("-%Lx", (long long)regnum);)
	}
}

void request_next_sync(struct superblock *sb)
{
	if (sb->unsyncs)
		outbead(sb->sync_sock, SYNC_REGION, struct region_message, .regnum = sb->unsync[sb->unsyncs - 1]);
}

static void _show_journal(struct superblock *sb)
{
	int i, j;
	for (i = 0; i < sb->image.journal_size; i++) {
		struct buffer *buf = readlog(sb, i);
		struct journal_block *block = buf2block(buf);
		printf("[%i] seq=%i (%i)", i, block->sequence, block->entries);
		for (j = 0; j < block->entries; j++) {
			region_t entry = block->entry[j];
			printf(" %c%Lx", (entry & sb->cleanmask)? '-': '+', (long long)(entry & ~sb->cleanmask));
		}
		printf("\n");
		brelse(buf);
	}
}

static void show_journal(struct superblock *sb)
{
	warn("oldest block:entry = %i:%i", buf2block(sb->newbuf)->oldest_block, buf2block(sb->newbuf)->oldest_entry);
	_show_journal(sb);
}

int recover_journal(struct superblock *sb)
{
	struct buffer *oldbuf;
	typeof(((struct journal_block *)NULL)->sequence) sequence = -1;
	int scribbled = -1, newest_block = -1;
	unsigned i;
	char *why = "";

	/* Scan full journal, find newest commit */

	for (i = 0; i < sb->image.journal_size; brelse(oldbuf), i++) {
		oldbuf = readlog(sb, i);
		struct journal_block *block = buf2block(oldbuf);

		if (checksum_block(sb, (void *)block)) {
			warn("block %i failed checksum", i);
			hexdump(block, 40);
			if (scribbled != -1) {
				why = "Too many bad blocks in journal";
				goto failed;
			}
			if (newest_block != -1 && newest_block != i - 1) {
				why = "Bad block not last written";
				goto failed;
			}
			scribbled = i;
			if (sequence != -1)
				sequence++;
			continue;
		}

		tracelog(warn("[%i] seq=%i", i, block->sequence);)
		if (sequence != -1 && block->sequence != sequence + 1) {
			if  (block->sequence != sequence + 1 - sb->image.journal_size) {
				why = "Block out of sequence";
				goto failed;
			}
	
			if (newest_block != -1) {
				why = "Multiple sequence wraps";
				goto failed;
			}
	
			if (scribbled == -1)
				newest_block = i - 1;
			else {
				if (scribbled != i - 1) {
					why = "Bad block not last written";
					goto failed;
				}
				newest_block = i - 2;
			}
		}
		sequence = block->sequence;
	}

	if (newest_block == -1) {
		if (scribbled == -1 || scribbled == 0)
			newest_block = sb->image.journal_size - 1;
		else {
			if (scribbled != sb->image.journal_size - 1) {
				why = "Bad block not last written";
				goto failed;
			}
			newest_block = sb->image.journal_size - 2;
		}
	}

	assert(scribbled == -1 || scribbled == (newest_block + 1) % sb->image.journal_size);
	/* Now we know the latest commit, all set to go */

	sb->newbuf = readlog(sb, newest_block);
	struct journal_block *newest = buf2block(sb->newbuf);
	unsigned oldest_block = newest->oldest_block;
	unsigned oldest_entry = newest->oldest_entry;

	/* If the final commit was a scribble, rewrite it */

	if (scribbled != -1) {
		struct buffer *oldbuf = readlog(sb, scribbled);
		empty_block(sb, buf2block(oldbuf), newest->sequence - sb->image.journal_size, scribbled);
		write_buffer(oldbuf);
		brelse(oldbuf);
	}

	/* Now load entries starting from journal head */

	tracelog(warn("oldest block:entry = %i:%i, newest block = %i", oldest_block, oldest_entry, newest_block);)
	sb->oldbuf = oldbuf = readlog(sb, newest->oldest_block);
	oldbuf->count++;
	sb->unsyncs = 0;

	while (1) {
		struct journal_block *oldest = buf2block(oldbuf);
		while (oldest_entry < oldest->entries) {
			region_t raw = oldest->entry[oldest_entry++];
			region_t regnum = raw & ~sb->cleanmask;
			int dirty = !(raw & sb->cleanmask);
			struct region *region = find_region(sb, regnum);

			if (dirty) {
				if (region)
					warn("redundant dirty");
				else {
					add_region(sb, regnum);
					sb->unsyncs++;
				}
			} else if (region) {
				del_region(sb, region);
				sb->unsyncs--;
			}
		}
		brelse(oldbuf);
		if (oldest_block == newest_block)
			break;
		if (++oldest_block == sb->image.journal_size)
			oldest_block = 0;
		oldbuf = readlog(sb, oldest_block);
		oldest_entry = 0;
	}

	/*
	 * Extract the dirty region list.
	 * Walk the hash and put all the dirty regions in a vec
	 * Use a vec instead of a list just because a sorter is available
	 * Sort the vec, partly for good taste and partly to reduce seeking on recovery
	 * Sort in reverse order for easy delete
	 */
	int n = 0;
	region_t *u = sb->unsync = malloc(sb->unsyncs * sizeof(region_t)); // !!! can't malloc on failover

	for (i = 0; i < HASH_BUCKETS; i++) {
		struct list_head *list;
		list_for_each(list, sb->hash + i) {
			struct region *region = list_entry(list, struct region, hash);
			u[n++] = region->regnum;
			region->flags |= REGION_UNSYNCED_FLAG;
		}
	}
	assert(n == sb->unsyncs);
	COMBSORT(n, i, j, u[i] > u[j], { region_t x = u[i]; u[i] = u[j]; u[j] = x; });
	tracelog(warn("Unsynced regions:");)

	tracelog(for (i = 0; i < n; i++))
		tracelog(printf("%Lx ", (long long)u[i]);)
	tracelog(printf("\n");)

	sb->newest_block = newest_block;
	sb->highwater = buf2block(oldbuf)->highwater;
	advance_new_block(sb, 0);
	return 0;

failed:
	errno = EIO; /* return a misleading error (be part of the problem) */
	error("Journal recovery failed, %s", why);
	return -1;
}

/* Initialization, State load/save */

void mark_sb_dirty(struct superblock *sb)
{
	sb->flags |= SB_DIRTY;
}

void setup_sb(struct superblock *sb)
{
	int i;

	sb->blocksize = 1 << sb->image.blocksize_bits;
	sb->regionsize = 1 << sb->image.regionsize_bits;
	sb->max_entries = (sb->blocksize - sizeof(struct journal_block)) / sizeof(region_t);
#ifdef TEST
	sb->max_entries = 5;
#endif
	sb->grant = malloc(sizeof(struct grant) * sb->max_entries);
	sb->cleanmask = ~(((typeof(region_t))-1LL) >> 1);
	sb->sectorshift = sb->image.blocksize_bits - SECTOR_BITS;
	for (i = 0; i < HASH_BUCKETS; i++)
		INIT_LIST_HEAD(&sb->hash[i]);
	INIT_LIST_HEAD(&sb->deferred_clean);
	sb->copybuf_size = sb->regionsize;
	sb->copybuf = malloc(sb->copybuf_size);
};

void load_sb(struct superblock *sb)
{
	struct buffer *buffer = bread(sb->logdev, SB_LOC, 4096);
	memcpy(&sb->image, buffer->data, sizeof(sb->image));
	brelse(buffer);
	setup_sb(sb);
}

void save_sb(struct superblock *sb)
{
	if (sb->flags & SB_DIRTY) {
		struct buffer *buffer = getblk(sb->logdev, SB_LOC, 4096);
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

int incoming_message(struct superblock *sb, struct client *client)
{
	struct messagebuf message;
	unsigned sock = client->sock;
	int err;

	if ((err = readpipe(sock, &message.head, sizeof(message.head))))
		goto pipe_error;
	trace(warn("%x/%u", message.head.code, message.head.length);)
	if (message.head.length > maxbody)
		goto message_too_long;
	if ((err = readpipe(sock, &message.body, message.head.length)))
		goto pipe_error;

	switch (message.head.code) {
		case START_SERVER:
			warn("Activating server");
			load_sb(sb);
			if (sb->image.flags & SB_BUSY)
				warn("Server was not shut down properly");
			sb->image.flags |= SB_BUSY;
			mark_sb_dirty(sb);
			save_sb(sb);
			recover_journal(sb);
			request_next_sync(sb);
			break;

		case SHUTDOWN_SERVER:
			return -2;

		case IDENTIFY:
		{
			client->id = ((struct identify *)message.body)->id;
			warn("client id %u", client->id);

			outbead(sock, SET_HIGHWATER, struct region_message, .regnum = buf2block(sb->newbuf)->highwater);

			int i;
			for (i = 0; i < sb->unsyncs; i++)
				outbead(sock, ADD_UNSYNCED, struct region_message, sb->unsync[i]);

			outbead(sock, REPLY_IDENTIFY, struct reply_identify, .region_bits = 20);
			break;
		}

		case REQUEST_WRITE:
		{
			struct region_message *body = (void *)&message.body;
			trace(warn("received write request, region %Lx", (long long)body->regnum);)

			if ((sb->flags & STUCK_FLAG)) {
				outbead(sock, BOUNCE_REQUEST, struct region_message, .regnum = body->regnum);
				break;
			}

			if (get_region(sb, body->regnum)) {
				assert(sb->grants < sb->max_entries);
				sb->grant[sb->grants++] = (struct grant){ .sock = sock, .regnum = body->regnum };
			} else
				outbead(sock, GRANT_UNSYNCED, struct region_message, .regnum = body->regnum);
			advance_if_full(sb);
			break;
		}

		case RELEASE_WRITE:
		{
			struct region_message *body = (void *)&message.body;
			trace(warn("received write release, region %Lx", (long long)body->regnum);)
			put_region(sb, body->regnum);

			if (!(sb->flags & STUCK_FLAG)) {
				advance_if_full(sb);
				break;
			}

			if (try_to_retire_old_entries(sb)) {
				sb->flags &= ~STUCK_FLAG;
				int i;
				for (i = 0; i < sb->clients; i++)
					outbead(sb->client[i].sock, RESUME_REQUESTS, struct { });
				retire_old_block(sb);
				advance_new_block(sb, 1);
			}
		}

		case SYNC_REGION:
		{
			struct region_message *body = (void *)&message.body;
			off_t pos = body->regnum << sb->image.regionsize_bits;
			int i;

			trace(warn("sync region %Lx", (long long)body->regnum);)
			pread(sb->member[0], sb->copybuf, sb->regionsize, pos);
			for (i = 1; i < sb->members; i++)
				pwrite(sb->member[i], sb->copybuf, sb->regionsize, pos);
			outbead(sock, REGION_SYNCED, struct region_message, .regnum = body->regnum);
			break;
		}

		case REGION_SYNCED:
		{
			struct region_message *body = (void *)&message.body;
			int i;

			trace(warn("region synced %Lx", (long long)body->regnum);)
			if (!sb->unsyncs) {
				warn("what the???");
				break;
			}
			if (body->regnum != sb->unsync[--sb->unsyncs]) {
				warn("synced wrong region");
				break;
			}
			for (i = 0; i < sb->clients; i++)
				outbead(sb->client[i].sock, DEL_UNSYNCED, struct region_message, .regnum = body->regnum);
			request_next_sync(sb);
			break;
		}

		default: 
			warn("Unknown message");
	}
	return 0;

message_too_long:
	warn("message %x too long (%u bytes)\n", message.head.code, message.head.length);
	return -1;
pipe_error:
	return -1; /* quietly drop the client if the connect breaks */
}

int incoming(struct superblock *sb, struct client *client)
{
	int err;

	do {
		if ((err = incoming_message(sb, client))) {
			sb->grants = 0;
			return err;
		}
	} while (available(client->sock) > sizeof(struct head)); // !!! stop this if journal full

	commit(sb);
	assert(!sb->grants);
	trace(show_regions(sb);)
	return 0;
}

int syncd(struct superblock *sb, int sock)
{
	trace_on(warn("Sync daemon started");)
	int err;

	while (!(err = incoming_message(sb, &(struct client){ .sock = sock })))
		;
	return err;
}

/* Signal Delivery via pipe */

static int sigpipe;

void sighandler(int signum)
{
	trace_off(warn("caught signal %i", signum);)
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

fd_t open_agent_connection(char *sockname)
{
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	int addr_len = sizeof(addr) - sizeof(addr.sun_path) + strlen(sockname);
	int sock;

	trace(warn("Connect to agent %s", sockname);)
	if ((sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
		error("Can't get socket");
	strncpy(addr.sun_path, sockname, sizeof(addr.sun_path));
	if (sockname[0] == '@')
		addr.sun_path[0] = 0;

	if (connect(sock, (struct sockaddr *)&addr, addr_len) == -1)
		error("Can't connect to agent");

	trace_on(warn("Connected to agent");)
	return sock;
}

int server(struct superblock *sb, char *sockname, int port)
{
	unsigned others = 4;
	struct pollfd pollvec[others+MAX_CLIENTS];
	int listener, getsig, agent, pipevec[2], err = 0;

	if (pipe(pipevec))
		error("Can't open pipe");
	sigpipe = pipevec[1];
	getsig = pipevec[0];

	struct server server = { .port = htons(port), .type = AF_INET,  };

	if ((listener = socket(AF_INET, SOCK_STREAM, 0)) < 0) 
		error("Can't get socket");

	if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (int[1]){ 1 }, sizeof(int)) == -1)
		error("Can't set SO_REUSEADDR, %s", strerror(errno));

	if (bind(listener,
		(struct sockaddr *)&(struct sockaddr_in){
			.sin_family = server.type, 
			.sin_port = server.port,
			.sin_addr = { .s_addr = INADDR_ANY } },
		sizeof(struct sockaddr_in)) < 0) 
		error("Can't bind to socket, %s", strerror(errno));
	listen(listener, 5);

	agent = open_agent_connection(sockname);

	int len;
	if ((len = resolve_self(AF_INET, server.address, sizeof(server.address))) == -1)
		error("Can't get own address, %s (%i)", strerror(errno), errno);
	server.address_len = len;
	warn("host = %x/%u", *(int *)server.address, server.address_len);
	writepipe(agent, &(struct head){ SERVER_READY, sizeof(server) }, sizeof(struct head));
	writepipe(agent, &server, sizeof(server));

#if 1
	if (daemon(0, 0) == -1)
		error("Can't start daemon, %s (%i)", strerror(errno), errno);
#endif

	int sockpair[2];

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockpair) == -1)
		error("Can't create socket pair");
	sb->sync_sock = sockpair[0];

	switch (fork()) {
	case -1:
		error("fork failed");
	case 0:
		return syncd(sb, sockpair[1]);
	}

	pollvec[0] = (struct pollfd){ .fd = listener, .events = POLLIN };
	pollvec[1] = (struct pollfd){ .fd = getsig, .events = POLLIN };
	pollvec[2] = (struct pollfd){ .fd = agent, .events = POLLIN };
	pollvec[3] = (struct pollfd){ .fd = sockpair[0], .events = POLLIN };

	signal(SIGINT, sighandler);
	signal(SIGTERM, sighandler);
	signal(SIGPIPE, SIG_IGN);

	while (1) {
		switch (poll(pollvec, others+sb->clients, sb->timeout)) {
		case -1:
			if (errno == EINTR)
				continue;
			error("poll failed, %s", strerror(errno));
		case 0:
			/* Timeouts come here */
			sb->timeout = -1;
			log_deferred_cleans(sb);
			continue;
		}

		/* New connection? */
		if (pollvec[0].revents) {
			struct sockaddr_in addr;
			int addr_len = sizeof(addr), sock;

			if (!(sock = accept(listener, (struct sockaddr *)&addr, &addr_len)))
				error("Cannot accept connection");

			trace_on(warn("Received connection");)
			assert(sb->clients < MAX_CLIENTS); // !!! send error and disconnect
			sb->client[sb->clients] = (struct client){ .sock = sock, .id = -1 };
			pollvec[others+sb->clients] = (struct pollfd){ .fd = sock, .events = POLLIN };
			sb->clients++;
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

		/* Agent traffic? */
		if (pollvec[2].revents) {
			warn("message from agent");
			incoming_message(sb, &(struct client){ .sock = agent });
		}

		/* Resync traffic? */
		if (pollvec[3].revents) {
			warn("got resync completion");
			incoming_message(sb, &(struct client){ .sock = sockpair[0] });
		}

		/* Client activity? */
		unsigned i = 0;
		while (i < sb->clients) {
			if (pollvec[others+i].revents) { // !!! check for poll error
				int result = incoming(sb, sb->client + i);
				if (result == -1) {
					warn("Client %i disconnected", sb->client[i].id);
					save_state(sb); // !!! just for now
					close(sb->client[i].sock);
					memmove(sb->client + i, sb->client + i + 1, sizeof(struct client) * --sb->clients);
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
	int i;

	init_buffers();
#ifdef CREATE
	if (argc != 2)
		error("usage: %s logdev", argv[0]);

	if ((sb->logdev = open(argv[1], O_RDWR | O_DIRECT)) == -1)
		error("Could not open log device %s, %s (%i)", argv[argc - 3], strerror(errno), errno);
	
	warn("create mirror log");
	sb->members = 0; // !!! we could depend on a client to do the copying and not need to know the members at all, just a thought
	sb->image.journal_base = 16;
	sb->image.journal_size = 1000;
#ifdef TEST
	sb->image.journal_size = 10;
#endif
	sb->image.blocksize_bits = 9;
	setup_sb(sb);
	for (i = 0; i < sb->image.journal_size; i++) {
		struct buffer *logbuf = getblk(sb->logdev, log2sector(sb, i), sb->blocksize);
		empty_block(sb, buf2block(logbuf), i, i);
		trace_off(hexdump(buf2block(logbuf), sizeof(struct journal_block));)
		write_buffer(logbuf);
		brelse(logbuf);
	}
	mark_sb_dirty(sb);
	save_sb(sb);
#ifdef TEST
	recover_journal(sb);
	for (i = 0; i < 10; i++)
		get_region(sb,  0x101 + i);
	show_journal(sb);
	for (i = 0; i < 8; i += 2)
		put_region(sb,  0x101 + i);
	commit(sb);
	put_region(sb,  0x102);
	commit(sb);
	get_region(sb,  0x111);
	commit(sb);
	get_region(sb,  0x112);
	commit(sb);
	show_journal(sb);
	show_regions(sb);
//	add_entry(sb,  0x4d3 | sb->cleanmask);
#if 1
	brelse(sb->oldbuf);
	brelse(sb->newbuf);
	show_buffers();
	evict_buffers();
	setup_sb(sb);
	warn("--------");
	recover_journal(sb);
	show_journal(sb);
	show_regions(sb);
#endif
#endif
	return 0;
#else
	if (argc < 6)
		error("usage: %s member... logdev socket port", argv[0]);

	sb->members = argc - 4;
	for (i = 0; i < sb->members; i++)
		if ((sb->member[i] = open(argv[i + 2], O_RDWR | O_DIRECT)) == -1)
			error("Could not open mirror member %s, %s (%i)", argv[i + 2], strerror(errno), errno);
	if ((sb->logdev = open(argv[1], O_RDWR | O_DIRECT)) == -1)
		error("Could not open log device %s, %s (%i)", argv[argc - 3], strerror(errno), errno);
	sb->timeout = -1;

	return server(sb, argv[argc - 2], atoi(argv[argc - 1]));
#endif
	void *useme = show_regions;
	useme = show_journal;
}
