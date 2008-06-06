#ifndef _BITMAP_H
#define _BITMAP_H

struct bmap {
	uint64_t size;
	uint64_t mapsize;
	int chunksize;
	int chunks_per_byte;
	char *map;
};

int bitmap_create(struct bmap *bmap, uint64_t size, uint8_t bitsize);
int bitmap_set(struct bmap *bmap, uint64_t offset, uint8_t val);
int bitmap_get(struct bmap *bmap, uint64_t bit, uint8_t *val);
int bitmap_clear(struct bmap *bmap, uint64_t offset);
void bitmap_destroy(struct bmap *bmap);
uint64_t bitmap_size(struct bmap *bmap);


#endif /* _BITMAP_H */
