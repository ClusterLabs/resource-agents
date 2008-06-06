/* Basic bitmap manipulation */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "libgfs2.h"

#define BITMAP_SIZE(size, cpb) (size / cpb)

#define BITMAP_BYTE_OFFSET(x, map) ((x % map->chunks_per_byte) \
                                    * map->chunksize )

#define BITMAP_MASK(chunksize) ((2 << (chunksize - 1)) - 1)

uint64_t gfs2_bitmap_size(struct gfs2_bmap *bmap) {
	return bmap->size;
}

int gfs2_bitmap_create(struct gfs2_bmap *bmap, uint64_t size,
					   uint8_t chunksize)
{
	if((((chunksize >> 1) << 1) != chunksize) && chunksize != 1)
		return -1;
	if(chunksize > 8)
		return -1;
	bmap->chunksize = chunksize;
	bmap->chunks_per_byte = 8 / chunksize;

	bmap->size = size;

	/* Have to add 1 to BITMAP_SIZE since it's 0-based and mallocs
	 * must be 1-based */
	bmap->mapsize = BITMAP_SIZE(size, bmap->chunks_per_byte)+1;

	if(!(bmap->map = malloc(sizeof(char) * bmap->mapsize)))
		return -ENOMEM;
	if(!memset(bmap->map, 0, sizeof(char) * bmap->mapsize)) {
		free(bmap->map);
		bmap->map = NULL;
		return -ENOMEM;
	}
	return 0;
}

int gfs2_bitmap_set(struct gfs2_bmap *bmap, uint64_t offset, uint8_t val)
{
	char *byte = NULL;
	uint64_t b = offset;

	if(offset < bmap->size) {
		byte = bmap->map + BITMAP_SIZE(offset, bmap->chunks_per_byte);
		b = BITMAP_BYTE_OFFSET(offset, bmap);

		*byte |= (val & BITMAP_MASK(bmap->chunksize)) << b;
		return 0;
	}
	return -1;
}

int gfs2_bitmap_get(struct gfs2_bmap *bmap, uint64_t bit, uint8_t *val)
{
	char *byte = NULL;
	uint64_t b = bit;

	if(bit < bmap->size) {
		byte = bmap->map + BITMAP_SIZE(bit, bmap->chunks_per_byte);
		b = BITMAP_BYTE_OFFSET(bit, bmap);

		*val = (*byte & (BITMAP_MASK(bmap->chunksize) << b )) >> b;
		return 0;
	}
	return -1;
}

int gfs2_bitmap_clear(struct gfs2_bmap *bmap, uint64_t offset)
{
	char *byte = NULL;
	uint64_t b = offset;

	if(offset < bmap->size) {
		byte = bmap->map + BITMAP_SIZE(offset, bmap->chunks_per_byte);
		b = BITMAP_BYTE_OFFSET(offset, bmap);

		*byte &= ~(BITMAP_MASK(bmap->chunksize) << b);
		return 0;
	}
	return -1;

}

void gfs2_bitmap_destroy(struct gfs2_bmap *bmap)
{
	if(bmap->map)
		free(bmap->map);
	bmap->size = 0;
	bmap->mapsize = 0;
	bmap->chunksize = 0;
	bmap->chunks_per_byte = 0;
}
