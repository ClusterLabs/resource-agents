#define SECTOR_BITS 9
#define BUFFER_STATE_INVAL 0
#define BUFFER_STATE_CLEAN 1
#define BUFFER_STATE_DIRTY 2
#define BUFFER_STATE_MASK 3
#define BUFFER_BUCKETS 9999

typedef unsigned long long sector_t;
typedef unsigned long long offset_t;

struct buffer
{
	struct buffer *hashlist;
	struct list_head list;
	unsigned count; // should be atomic_t
	unsigned flags;
	unsigned size;
	sector_t sector;
	unsigned char *data;
	unsigned fd;
};

struct list_head dirty_buffers;
extern unsigned dirty_buffer_count;

void show_dirty_buffers(void);
void set_buffer_dirty(struct buffer *buffer);
void set_buffer_uptodate(struct buffer *buffer);
void brelse(struct buffer *buffer);
void brelse_dirty(struct buffer *buffer);
int write_buffer_to(struct buffer *buffer, offset_t pos);
int write_buffer(struct buffer *buffer);
int read_buffer(struct buffer *buffer);
unsigned buffer_hash(sector_t sector);
struct buffer *new_buffer(sector_t sector, unsigned size);
struct buffer *getblk(unsigned fd, sector_t sector, unsigned size);
struct buffer *bread(unsigned fd, sector_t sector, unsigned size);
void evict_buffer(struct buffer *buffer);
void evict_buffers(void);
void flush_buffers(void);
void show_buffer(struct buffer *buffer);
void show_active_buffers(void);
void show_buffers(void);
void init_buffers(void);

static inline int buffer_dirty(struct buffer *buffer)
{
	return (buffer->flags & BUFFER_STATE_MASK) == BUFFER_STATE_DIRTY;
}

static inline int buffer_uptodate(struct buffer *buffer)
{
	return (buffer->flags & BUFFER_STATE_MASK) == BUFFER_STATE_CLEAN;
}

static inline void *malloc_aligned(size_t size, unsigned binalign)
{
	unsigned long p = (unsigned long)malloc(size + binalign - 1);
	return (void *)(p + (-p & (binalign - 1)));
}
