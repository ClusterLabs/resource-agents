/**
  @file

  Bounded memory allocator.  This is designed for applications which require
  bounded memory allocation and which ought not be swapped out to disk.

  What it is:
  - Replacement for malloc, calloc, free, and realloc which allocates
  memory from a fixed-size heap, which is either allocated at the time
  of the first allocation or during program initialization with a call
  to 'malloc_init(size_t count)'.

  - Designed for applications requiring small amounts of RAM.  Note
  that though we use size_t arguments, the maximum supported in the
  header structure is a 32-bit integer.

  - Fairly fast.


  What it's _not_:
  - General purpose.  The heap is allocated either at the first call to
  malloc() or the call to malloc_init(), and is a fixed size.  There is no
  memalign/valloc/pvalloc currently. 

  - Super-efficient.  It's reasonably fast in many situations compared to
  glibc's malloc, but records states in the memory blocks to help prevent
  accidental double-frees and such (it's still possible, mind you).  In 
  general, your program will consume MORE memory when using this allocator,
  because it preallocates a huge block.  However, your program should run
  faster.  It's also probably not terribly efficient in a threaded
  program, but it does work (and properly zap its mutex on fork()).


  malloc algorithm in detail:

  (1) Init the pool as necessary.  After the pool is initialized, we have
  one large free block which is the size of the memory pool less the header.

  (2) Whenever we get a call to malloc, we look for a free block.  If the
  size of a free block matches within 25% of the requested size (bounded at
  MIN_SAVE), we immediately return that chunk to the user.  If no blocks are
  found matching this criteria, we look for chunks which are large enough
  to split (i.e. with a size >= requested_size + MIN_EXTRA).

  (3) If we find one, we split the block, create a new (smaller) free block,
  and return the block to the user.

  [ Note: Steps 4 and 5 only if AGGR_RECLAIM is 0 ]
  (4) If none are found, we perform an aggressive consolidation which searches
  the entire memory pool for free blocks next to each other and combines
  them into one larger block.

  (5) Repeat steps (2) and (3).

  (6) If no blocks are found again, we're done.  Return NULL/ENOMEM.


  free algorithm in detail:

  (1) Sanity check the pointer and block structure it would point to.

  (2) [ If AGGR_RECLAIM = 0 ] Consolidate this block with all free blocks
  with a higher address than it in the pool.

  (2) [ If AGGR_RECLAIM = 1 ] Aggressively consolidate all free blocks 
  in the pool which are next to one another.


  realloc:

  ... Just an obvious wrapper around malloc.  Potentially could resize if
  the next block was free and has enough space.  I.e.  Join-blocks,
  resize, and split again if necessary.  For now, it works, so we'll
  leave it alone.  This would be both an increase in speed and memory
  efficiency (while performing the operation), but otherwise isn't necessary.


  calloc:

  ... Really obvious wrapper.  Uses memset to clear the memory.

  TODO: Use futex, perhaps, instead of pthread stuff.
 */
#include <stdint.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <malloc.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

#ifndef DEBUG
#define DEBUG			/* Record program counter of malloc/calloc */
#endif				/* or realloc call; print misc stuff out */

#if defined(__ia64__) || defined(__hppa__)
#undef DEBUG
#endif

/* Tunable stuff XXX This should be external */
#define PARANOID		/* Trade off a bit of space and speed for
				   extra sanity checks */
#define DIE_ON_FAULT		/* Kill program if we do something bad
				   (double free, free after overrun, etc.
				   for instance) */
#undef  AGGR_RECLAIM		/* consolidate_all on free (*slow*) */

//#undef  STACKSIZE	/*4	   backtrace to store if DEBUG is set */
#define STACKSIZE 1		/* at least 1 gets you free addr */

#undef	GDB_HOOK		/* Dump program addresses in malloc_table
				   using a fork/exec of gdb (SLOW but fun)
				   building this defeats the purpose of
				   a bounded memory allocator, and is only
				   useful for debugging memory leaks.
				   This does not harm anything except code
				   size until "malloc_dump_table" is called.
				   Given that this is not a normal malloc
				   API, this should not matter. */

#define DEFAULT_SIZE	(1<<23) /* 8MB default giant block size */
#define BUCKET_COUNT	(13)	/* 2^BUCKET_COUNT = max. interesting size */
#define MIN_POWER	(3)	/* 2^MIN_POWER = minimum size */
#define MIN_SIZE	(1<<MIN_POWER)
#define ALIGN		(sizeof(void *))
#define NOBUCKET	((uint16_t)(~0))
#define MIN_EXTRA	(1<<5)	/* 64 bytes to split a block */

/* Misc stuff */
#define ST_FREE		0xfec3  /* Block is free */
#define ST_ALLOC	0x08f7  /* Block is in use */


#ifndef NOPTHREADS
#include <pthread.h>
static pthread_mutex_t _alloc_mutex = PTHREAD_MUTEX_INITIALIZER;
#else
#define pthread_mutex_trylock(x) (0)
#define pthread_mutex_lock(x)
#define pthread_mutex_unlock(x)
#endif

#ifdef DIE_ON_FAULT

#include <signal.h>

#define die_or_return(val)\
do { \
	raise(SIGSEGV); \
	return val; \
} while (0)

#else

#define die_or_return(val)\
do { \
	return val; \
} while (0)

#endif


typedef struct _memblock {
	/*
	   Need to align for 64-bit arches, so for now, we'll keep
	   the header size to 8 bytes.
	 */
	uint32_t	mb_size;
	uint16_t	mb_bucket;
	uint16_t	mb_state;
#ifdef DEBUG
#ifdef STACKSIZE
	void		*mb_pc[STACKSIZE];
#endif
#endif
	/* If PARANOID isn't defined, we use the following pointer for
	   more space. */
	struct _memblock *mb_next;
} memblock_t;


/**
  mmap(2)ed memory pool.
 */
static void *_pool = NULL;

/**
  Pool size
 */
static size_t _poolsize = 0;

/**
  Free buckets
 */
static memblock_t *free_buckets[BUCKET_COUNT];


#ifdef PARANOID

/**
  Allocated buckets
 */
static memblock_t *alloc_buckets[BUCKET_COUNT];

/*
   We use the next pointer for the alloc_bucket list if we're PARANOID, as
   we record the allocated block list in that mode.
 */
#define HDR_SIZE (sizeof(memblock_t))

#else /* ... not PARANOID */

/*
   We use the next pointer for extra data space if we're secure.  Makes the
   allocator slightly more memory efficient.
 */
#define HDR_SIZE (sizeof(memblock_t) - sizeof(memblock_t *))

#endif /* PARANOID */

/* Return the user pointer for a given memblock_t structure. */
#define pointer(block) (void *)((void *)block + HDR_SIZE)

/* Return the memblock_t structure given a pointer */
#define block(pointer) (memblock_t *)((void *)pointer - HDR_SIZE)

/* Calculate and return the next memblock_t pointer in the memory pool
   given a memblock_t pointer. */
#define nextblock(pointer) \
	(memblock_t *)((void *)pointer + pointer->mb_size + HDR_SIZE)

/* Doesn't *ensure* a block is free, but is a pretty good heuristic */
#define is_valid_free(block) \
	(block->mb_bucket < BUCKET_COUNT && block->mb_state == ST_FREE)

/* Doesn't *ensure* a block is allocated, but is a pretty good heuristic */
#define is_valid_alloc(block) \
	(block->mb_bucket == NOBUCKET && block->mb_state == ST_ALLOC && \
	 block->mb_size != 0)


/**
  Find the proper bucket index, given a size
 */
static inline int
find_bucket(size_t size)
{
	int rv = 0;
	size_t s = size;
	
	s >>= MIN_POWER;
	while (s && (rv < (BUCKET_COUNT-1))) {
		s >>= 1;
		rv++;
	}

	return rv;
}


#ifdef PARANOID
/**
  Check for and remove a block from its free list.
 */
static inline int
remove_alloc_block(memblock_t *b)
{
	memblock_t *block, **prev;
	uint16_t bucket;

	/* Could improve performance if NOBUCKET wasn't used
	   as an indicator of an allocated block */
	bucket = find_bucket(b->mb_size);
	prev = &(alloc_buckets[bucket]);

	if (!(block = alloc_buckets[bucket]))
		return 0;

	do {
		if (b == block) {
			*prev = b->mb_next;
			b->mb_next = NULL;
			return 1;
		}

		prev = &(*prev)->mb_next;
		block = block->mb_next;
	} while (block);

	/* Couldn't find in its appropriate bucket */
	return 0;
}
#endif


/**
  Check for and remove a block from its free list.
 */
static inline int
remove_free_block(memblock_t *b)
{
	memblock_t *block, **prev;
	uint16_t bucket;

	bucket = b->mb_bucket;
	prev = &(free_buckets[bucket]);

	if (!(block = free_buckets[bucket]))
		return 0;

	do {
		if (b == block) {
			*prev = b->mb_next;
			b->mb_next = NULL;
			return 1;
		}

		prev = &(*prev)->mb_next;
		block = block->mb_next;
	} while (block);

	/* Couldn't find in its appropriate bucket */
	return 0;
}


#ifdef PARANOID
/**
  Insert a block on to the allocated bucket list
 */
static inline memblock_t *
insert_alloc_block(memblock_t *b)
{
	uint16_t bucket = find_bucket(b->mb_size);

	b->mb_bucket = NOBUCKET;
	b->mb_state = ST_ALLOC;
	b->mb_next = NULL;

	if (alloc_buckets[bucket] != NULL)
		b->mb_next = alloc_buckets[bucket];

	alloc_buckets[bucket] = b;

	return b;
}
#endif


/**
  Insert a block on to the free bucket list
 */
static inline memblock_t *
insert_free_block(memblock_t *b)
{
	uint16_t bucket = find_bucket(b->mb_size);

	b->mb_bucket = bucket;
	b->mb_state = ST_FREE;
	b->mb_next = NULL;

	if (free_buckets[bucket] != NULL)
		b->mb_next = free_buckets[bucket];

	free_buckets[bucket] = b;

	return b;
}


/**
  Consolidate a block with all free blocks to the right of it in the
  pool.

  @param left		Left block
  @return		Number of blocks consolidated.
 */
static inline int
consolidate(memblock_t *left)
{
	memblock_t *right;
	int merged = 0;

	while (1) {
		right = nextblock(left);
		if ((void *)right >= (_pool + _poolsize))
			return merged;

		if (!is_valid_free(right)) {
			if (is_valid_alloc(right))
				return merged;

			/* Not valid free and not valid allocated. BAD. */
			fprintf(stderr, "consolidate: Block %p corrupt. "
				"(Overflow from block %p?)\n", right, left);
			die_or_return(-1);
		}
		if (!remove_free_block(right))
			return merged;

		left->mb_size += (right->mb_size + HDR_SIZE);
		right->mb_state = 0;

		++merged;
	}
	/* Not reached */
	return merged;
}


/**
  Consolidate all free blocks next to each-other in the pool in to larger
  blocks.  This algorithm is slow...
 */
static inline void
consolidate_all(void)
{
	memblock_t *b, *p;
	int total = 0;

	p = NULL;
	b = _pool;

	while ((void *)b < (void *)(_pool + _poolsize)) {

		if (is_valid_free(b)) {
			if (!remove_free_block(b)) {
				fprintf(stderr, "consolidate: Free block %p "
					"was not in our free list.\n", b);
			}

			total += consolidate(b);
			insert_free_block(b);
		} else if (!is_valid_alloc(b)) {
			/* Not valid free and not valid allocated. BAD. */
			fprintf(stderr, "consolidate_all: Block %p corrupt. "
				"(Overflow from block %p?)\n", b, p);
			die_or_return();
		}

		/* Consolidated or we're a valid allocated block */
		p = b;
		b = nextblock(b);
	}

#ifdef DEBUG
	if (total)
		fprintf(stderr, "%s: consolidated %d\n", __FUNCTION__, total);
#endif
	
}


#ifndef NOTHREADS
/**
  After a fork, we need to kill the mutex so the child can still call malloc
  without getting stuck.
 */
void
malloc_zap_mutex(void)
{
	pthread_mutex_init(&_alloc_mutex, NULL);
}
#endif


/**
  Initialize the giant mmap pool storage.
 */
#ifndef NOTHREADS
static inline int
_malloc_init(size_t poolsize)
#else
int
malloc_init(size_t poolsize)
#endif
{
	int e;
	memblock_t *first = NULL;

	if (_pool)
		return -1;

	if (poolsize % 32)
		poolsize += (32 - (poolsize % 32));

	_pool = mmap(NULL, poolsize, PROT_READ | PROT_WRITE, MAP_LOCKED |
		     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if ((_pool == MAP_FAILED) && (errno == EAGAIN)) {
		/* Try again without MAP_LOCKED */
		_pool = mmap(NULL, poolsize, PROT_READ | PROT_WRITE,
			     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (_pool != MAP_FAILED)
			fprintf(stderr, "malloc_init: Warning: using unlocked"
				" memory pages (got root?)\n");
	}

	if (_pool == MAP_FAILED) {
		return -1;
	}

	_poolsize = poolsize;

	for (e = 0; e < BUCKET_COUNT; e++)
		free_buckets[e] = NULL;

	first = _pool;
	first->mb_size = (_poolsize - HDR_SIZE);
	first->mb_state = ST_FREE;
	first->mb_next = NULL;
	first->mb_bucket = NOBUCKET;

#ifdef PARANOID
	for (e = 0; e < BUCKET_COUNT; e++)
		alloc_buckets[e] = NULL;
#endif
#if 0
#ifdef DEBUG
	fprintf(stderr, "malloc_init: %lu/%lu available\n",
		(long unsigned)first->mb_size, (long unsigned)_poolsize);
#endif
#endif

	insert_free_block(first);
	return 0;
}


#ifndef NOTHREADS
/* Same as above, but lock first! */
int
malloc_init(size_t poolsize)
{
	int e = 0, ret = -1;
	pthread_mutex_lock(&_alloc_mutex);
	if (!_pool) {
		ret = _malloc_init(poolsize);
		e = errno;
	}
	pthread_atfork(NULL, NULL, malloc_zap_mutex);
	pthread_mutex_unlock(&_alloc_mutex);
	errno = e;
	return ret;
}
#endif


static inline memblock_t *
split(memblock_t *block, size_t size)
{
	memblock_t *nb;
	size_t oldsz;

	oldsz = block->mb_size;

	/* Ok, we got it. */
	block->mb_size = size;
	block->mb_next = NULL;
	block->mb_state = ST_ALLOC;
	block->mb_bucket = NOBUCKET;

	nb = nextblock(block);
	nb->mb_state = ST_FREE;
	nb->mb_bucket = NOBUCKET;
	nb->mb_size = (size_t)(oldsz - (HDR_SIZE + size));
	insert_free_block(nb);

	/* Created a new next-block. */

	return block;
}


/**
  Search the freestore and return an available block which accomodates the
  requested size, splitting up a free block if necessary.
 */
static inline memblock_t *
search_freestore(size_t size)
{
	uint16_t bucket;
	memblock_t *block, **prev;

	for (bucket = find_bucket(size); bucket < BUCKET_COUNT; bucket++) {
		block = free_buckets[bucket];
		if (!block)
			continue;

		prev = &(free_buckets[bucket]);

		do {
			/*
		    	  Look for block with size within 25%
			 */
			if (block->mb_size >= size) {

				/*
				   Split if size >= size + MIN_EXTRA.

				   25% of 1MB = 256kb -- which is way too much
				   unused space to leave hanging around when
				   we're tuned for small bits.
				 */
				if (block->mb_size >= (size + MIN_EXTRA)) {
					*prev = block->mb_next;
					block->mb_state = ST_ALLOC;
					block->mb_bucket = NOBUCKET;
					return split(block, size);
				}

				/*
				   Otherwise, return if the size matches
				   within 25%

				   So, the max unused bytes from a malloc
				   operation for a given size is 56 (since
				   64 == MIN_EXTRA).  So the following is 
				   very inefficient:

				      p = malloc(280);
				      free(p);
				      p = malloc(224);
				 */
			    	if (block->mb_size < (size + size / 4)) {
					*prev = block->mb_next;
					block->mb_state = ST_ALLOC;
					block->mb_bucket = NOBUCKET;
					return block; 
				}
			}

			prev = &(*prev)->mb_next;
			block = block->mb_next;
		} while (block);
	}

	/* Ok, nothing big enough in the free store */
	return NULL;
}


#ifdef DEBUG

#define stack_pointer(n) \
	(__builtin_frame_address(n)?__builtin_return_address(n):NULL)

#define assign_address(_ptr, _cnt) \
{ \
	switch(_cnt) { \
	case 0: \
		(_ptr)[_cnt] = stack_pointer(0); \
		break; \
	case 1: \
		(_ptr)[_cnt] = stack_pointer(1); \
		break; \
	case 2: \
		(_ptr)[_cnt] = stack_pointer(2); \
		break; \
	case 3: \
		(_ptr)[_cnt] = stack_pointer(3); \
		break; \
	case 4: \
		(_ptr)[_cnt] = stack_pointer(4); \
		break; \
	case 5: \
		(_ptr)[_cnt] = stack_pointer(5); \
		break; \
	case 6: \
		(_ptr)[_cnt] = stack_pointer(6); \
		break; \
	case 7: \
		(_ptr)[_cnt] = stack_pointer(7); \
		break; \
	case 8: \
		raise(SIGKILL); \
		break; \
	}\
}
#endif




/**
  Memory allocation
 */
void *
malloc(size_t size)
{
#ifndef NOROUND
	size_t r;
#endif
	memblock_t *block;
#ifdef DEBUG
#ifdef STACKSIZE
	int sp;
#endif
#endif

	if (size < MIN_SIZE)
		size = MIN_SIZE;
#ifndef NOROUND
	else {
		r = size % MIN_SIZE;
		size += (r ? (MIN_SIZE - r) : 0);
	}
#endif /* NOROUND */

#ifndef NOTHREADS
	while (pthread_mutex_trylock(&_alloc_mutex) != 0);
#endif
	if (!_pool) {
#if 0
#ifdef DEBUG
		fprintf(stderr,
			"malloc: Initializing region default size %lu\n",
			(long unsigned)DEFAULT_SIZE);
#endif
#endif
#ifndef NOTHREADS
		if (_malloc_init(DEFAULT_SIZE) < 0)
#else
		if (malloc_init(DEFAULT_SIZE) < 0)
#endif
			return NULL;
	}

	block = search_freestore(size);
	if (block) {
#ifdef DEBUG
#ifdef STACKSIZE
		for (sp = 0; sp < STACKSIZE; sp++) {
			assign_address(block->mb_pc, sp);
			if (!block->mb_pc[sp])
				break;
		}
#endif
#endif
#ifdef PARANOID
		insert_alloc_block(block);
#endif
#ifndef NOTHREADS
		pthread_mutex_unlock(&_alloc_mutex);
#endif
		return pointer(block);
	}

#ifdef AGGR_RECLAIM
	consolidate_all();
	block = search_freestore(size);
	if (block) {
#ifdef DEBUG
#ifdef STACKSIZE
		for (sp = 0; sp < STACKSIZE; sp++) {
			assign_address(block->mb_pc, sp);
			if (!block->mb_pc[sp])
				break;
		}
#endif
#endif
#ifdef PARANOID
		insert_alloc_block(block);
#endif
		pthread_mutex_unlock(&_alloc_mutex);
		return pointer(block);
	}
#endif /* AGGR_RECLAIM */

#ifdef DEBUG
	fprintf(stderr, "Out of memory malloc(%lu) @ %p\n",
		(long unsigned)size, __builtin_return_address(0));
#endif
	errno = ENOMEM;
	return NULL;
}


/**
  Memory free
 */
void
free(void *p)
{
	memblock_t *b;
#ifdef DEBUG
#ifdef STACKSIZE
	void *pc = __builtin_return_address(0);
	int x;
#endif
#endif

	if (!p) {
#if 0
		fprintf(stderr, "free(NULL) @ %p\n",
		       	__builtin_return_address(0));
#endif
		/* POSIX allows for free(NULL) */
		return;
	}

	b = ((void *)p - HDR_SIZE);

	pthread_mutex_lock(&_alloc_mutex);
	if (((void *)b < _pool) || ((void *)b >= (_pool + _poolsize))) {
		fprintf(stderr, "free(%p) @ %p - Out of bounds\n",
			p, __builtin_return_address(0));
		pthread_mutex_unlock(&_alloc_mutex);
		die_or_return();
	}

	if (!is_valid_alloc(b)) {
#ifdef DEBUG
		if (!is_valid_free(b))
			fprintf(stderr,
				"free(%p) @ %p - Invalid address\n",
				p, __builtin_return_address(0));
		else
#ifdef STACKSIZE
			fprintf(stderr,
				"free(%p) @ %p - Already free @ %p\n",
				p, __builtin_return_address(0), b->mb_pc[0]);
#else
			fprintf(stderr,
				"free(%p) @ %p - Already free\n",
				p, __builtin_return_address(0));
#endif

#endif
		pthread_mutex_unlock(&_alloc_mutex);
		die_or_return();
	}

#ifdef PARANOID
	/* Remove from the allocated list if we're tracking it. */
	if (!remove_alloc_block(b)) {
		fprintf(stderr, "free(%p) @ %p - Not allocated\n",
			p, __builtin_return_address(0));
		pthread_mutex_unlock(&_alloc_mutex);
		die_or_return();
	}
#endif

#ifdef DEBUG
#ifdef STACKSIZE
	for (x = 0; x < STACKSIZE; x++)
		b->mb_pc[x] = NULL;
	b->mb_pc[0] = pc;
#endif
#endif

	b->mb_state = ST_FREE;
	b->mb_next = NULL;

#ifdef AGGR_RECLAIM
	/* Aggressively search the whole pool and combine all side-by-side
	   free blocks */
	insert_free_block(b);
	consolidate_all();
#else
	/* Combine with all blocks to the right in the pool */
	b->mb_bucket = find_bucket(b->mb_size);
	consolidate(b);
	insert_free_block(b);
#endif
	pthread_mutex_unlock(&_alloc_mutex);
}


/**
   Slow realloc.  It *should* resize the memory, but since we're dealing
   with a static heap, it doesn't.
 */
void *
realloc(void *oldp, size_t newsize)
{
	memblock_t *oldb;
#ifdef DEBUG
	memblock_t *newb;
#ifdef STACKSIZE
	int sp;
#endif
#endif
	void *newp;

	if (oldp) {
		oldb = block(oldp);
		if (newsize <= oldb->mb_size)
			return oldp;
	}

	newp = malloc(newsize);

	if (!newp) {
		return NULL;
	}

	if (oldp) {
		oldb = block(oldp);
		memcpy(newp, oldp, (newsize > oldb->mb_size) ?
		       oldb->mb_size : newsize);
		free(oldp);
	}
#ifdef DEBUG
	newb = block(newp);
#ifdef STACKSIZE
	for (sp = 0; sp < STACKSIZE; sp++) {
		assign_address(newb->mb_pc, sp);
		if (!newb->mb_pc[sp])
			break;
	}
#endif
#endif
	return newp;
}


/**
   simple calloc.
 */
void *
calloc(size_t sz, size_t nmemb)
{
	void *p;
#ifdef DEBUG
	memblock_t *newb;
#ifdef STACKSIZE
	int sp;
#endif
#endif

	sz *= nmemb;
	p = malloc(sz);
	if (!p)
		return NULL;

#ifdef DEBUG
	newb = block(p);
#ifdef STACKSIZE
	for (sp = 0; sp < STACKSIZE; sp++) {
		assign_address(newb->mb_pc, sp);
		if (!newb->mb_pc[sp])
			break;
	}
#endif
#endif
	memset(p, 0, sz);
	return p;
}


void resolve_stack_gdb(void **, size_t);


/**
  Dump the allocated memory table.  Only does anything useful if PARANOID
  is set.
 */
void
malloc_dump_table(size_t minsize, size_t maxsize)
{
#ifdef PARANOID
	int any = 0;
	int x;
#ifdef DEBUG
#ifdef STACKSIZE
#ifndef GDB_HOOK
	int sp;
#endif
#endif
#endif
	memblock_t *b;

	fflush(stdout);
	pthread_mutex_lock(&_alloc_mutex);
	for (x=0; x<BUCKET_COUNT; x++) {
		for (b = alloc_buckets[x]; b; b = b->mb_next) {

			if (b->mb_size < minsize || b->mb_size > maxsize)
				continue;

			if (!any)
				fprintf(stderr,
					"+++ Memory table dump +++\n");
			any++;
#ifndef DEBUG
			fprintf(stderr, "  %p (%lu bytes)\n", pointer(b),
				(unsigned long)b->mb_size);
#else /* DEBUG */
			fprintf(stderr,
				"  %p (%lu bytes) allocation trace:\n",
				pointer(b), (unsigned long)b->mb_size);
#ifdef STACKSIZE
#ifdef GDB_HOOK
			resolve_stack_gdb(b->mb_pc, STACKSIZE);
			fprintf(stderr,"\n");
#else
			for (sp = 0; sp < STACKSIZE; sp++)
				fprintf(stderr,"\t%p\n",b->mb_pc[sp]);
#endif
#endif /* STACKSIZE */
#endif /* DEBUG */
		}
	}
	pthread_mutex_unlock(&_alloc_mutex);
	if (any)
		fprintf(stderr, "--- End Memory table dump ---\n");

#else /* PARANOID */
	fprintf(stderr, "malloc_dump_table: Unimplemented\n");
#endif /* PARANOID */
}


/**
  Print general stats about how we're doing with memory.
 */
void
malloc_stats(void)
{
	int fb = 0, ub = 0, x;
	size_t metadata = 0, ucount = 0, fcount = 0, ps = 0;
	memblock_t *b;
	void *p;

	pthread_mutex_lock(&_alloc_mutex);
	if (!_pool) {
		pthread_mutex_unlock(&_alloc_mutex);
		fprintf(stderr,"malloc_stats: No information\n");
		return;
	}


	for (x=0; x<BUCKET_COUNT; x++) {
		for (b = free_buckets[x]; b; b = b->mb_next) {
			metadata += HDR_SIZE;
			fcount += b->mb_size;
			++fb;
		}
	}

#ifdef PARANOID
	for (x=0; x<BUCKET_COUNT; x++) {
		for (b = alloc_buckets[x]; b; b = b->mb_next) {
			metadata += HDR_SIZE;
			ucount += b->mb_size;
			++ub;
		}
	}
#else
	/* Estimate only... :( */
	ucount = fcount - metadata;
	ub = 0;
#endif
	p = _pool;
	ps = _poolsize;

	pthread_mutex_unlock(&_alloc_mutex);

	fprintf(stderr, "malloc_stats:\n");
	fprintf(stderr, "  Total: %lu bytes\n", (unsigned long)ps);
	fprintf(stderr, "  Base address: %p\n", p);
	fprintf(stderr, "  Free: %lu bytes in %d blocks\n",
		(unsigned long)fcount, fb);
#ifdef PARANOID
	fprintf(stderr, "  Used: %lu bytes in %d blocks\n",
		(unsigned long)ucount, ub);
	fprintf(stderr, "  Metadata Usage: %lu bytes\n",
		(unsigned long)metadata);
#else
	fprintf(stderr,
		"  Used: %lu bytes (debugging off, block count unknown)\n",
		(unsigned long)ucount);
	fprintf(stderr,
		"  Metadata Usage: %lu bytes (debugging off, estimate)\n",
		(unsigned long)metadata);
#endif
}


#ifdef DEBUG
#ifdef STACKSIZE
#ifdef GDB_HOOK
void
show_gdb_address(char *buf, size_t buflen, void *address)
{
	char *line;
	char *end = buf + buflen;
	char foo[32];

	snprintf(foo, sizeof(foo), "%p", address);

	line = buf;
	while ((line = strchr(line, '\n'))) {

		if ((line + strlen(foo) + 1) > end)
			return;
		++line;

		if (!strncmp(line, foo, strlen(foo))) {
			end = strchr(line, ':');
			if (end)
				*end = 0;
			fprintf(stderr,"\t%s\n", line);
			if (end)
				*end = ':';
			return;
		}
	}
}


int
my_system(char *foo, char *outbuf, size_t buflen)
{
	char cmd[4096];
	char *args[128];
	int x = 0, y;
	int pid, arg;
	int p[2];

	strncpy(cmd, foo, sizeof(cmd));
	foo = NULL;
	do {
		if (!x)
			args[x] = strtok_r(cmd, " ", &foo);
		else
			args[x] = strtok_r(NULL, " ", &foo);

	} while (args[x++]);

	pipe(p);
	pid = fork();
	if (!pid) {
		close(STDOUT_FILENO);
		dup2(p[1], STDOUT_FILENO);
		execv(args[0], args);
		exit(1);
	}

	close(p[1]);

	y = 0;
	arg = WNOHANG;
	memset(outbuf, 0, buflen);
	while (waitpid(pid, NULL, arg) != pid) {

		/* Interrupt after we decided to block for child */
		if (!arg)
			continue;

		if (y >= buflen) {
			/* Out of space.  Wait for child without
			   the WNOHANG flag now... */
			arg = 0;
			continue;
		}
		x = read(p[0], outbuf + y, 1);
		++y;
	}
		
	return 0;
}
	

/*
   Yes, it's slow.  Painfully slow.
 */
void
resolve_stack_gdb(void **stack, size_t stacksize)
{
	int pid, fd;
	char fname[1024];
	char programname[1024];
	char commandline[4096];
	char tmp[4096];
	int s;

	pid = fork();
	if (pid < 0) {
		return;
	}

	if (pid) {
		while (waitpid(pid, &s, 0) != pid);
		return;
	}

	/* Child */

	pid = getppid();

	snprintf(fname, sizeof(fname), "/proc/%d/exe", pid);
	memset(programname, 0, sizeof(programname));
	readlink(fname, programname, sizeof(programname));

	snprintf(fname, sizeof(fname), "/tmp/alloc.gdb.XXXXXX");

	fd = mkstemp(fname);
	for (s = 0; s < stacksize; s++) {
		if (!stack[s])
			break;
		snprintf(tmp, sizeof(tmp), "x/i %p\n", stack[s]);
		write(fd, tmp, strlen(tmp));
	}
	snprintf(tmp, sizeof(tmp), "quit\n");
	write(fd, tmp, strlen(tmp));
	fsync(fd);
	fdatasync(fd);

	snprintf(commandline, sizeof(commandline),
		 "/usr/bin/gdb %s %d -batch -x %s",
		 programname, pid, fname);
	my_system(commandline, tmp, sizeof(tmp));
	for (s = 0; s < stacksize; s++) {
		if (!stack[s])
			break;
		show_gdb_address(tmp, sizeof(tmp), stack[s]);
	}
	
	unlink(fname);
	close(fd);
	exit(0);
}
#endif
#endif
#endif

