/* Basic bitmap manipulation */
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include "osi_user.h"
#include "libgfs.h"
#include "incore.h"


#define BITMAP_SIZE(size, cpb) (size / cpb)

#define BITMAP_BYTE_OFFSET(x, map) ((x % map->chunks_per_byte) \
                                    * map->chunksize )

#define BITMAP_MASK(chunksize) ((2 << (chunksize - 1)) - 1)

uint64_t bitmap_size(struct bmap *bmap) {
	return bmap->size;
}

int bitmap_create(struct bmap *bmap, uint64_t size, uint8_t chunksize)
{
	if((((chunksize >> 1) << 1) != chunksize) && chunksize != 1) {
		log_err("chunksize must be a power of 2\n");
		return -1;
	}
	if(chunksize > 8) {
		log_err("chunksize must be <= 8\n");
		return -1;
	}
	bmap->chunksize = chunksize;
	bmap->chunks_per_byte = 8 / chunksize;

	bmap->size = size;

	/* Have to add 1 to BITMAP_SIZE since it's 0-based and mallocs
	 * must be 1-based */
	bmap->mapsize = BITMAP_SIZE(size, bmap->chunks_per_byte)+1;

	if(!(bmap->map = malloc(sizeof(char) * bmap->mapsize))) {
		log_err("Unable to allocate bitmap of size %"PRIu64"\n",
			bmap->mapsize);
		return ENOMEM;
	}
	if(!memset(bmap->map, 0, sizeof(char) * bmap->mapsize)) {
		log_err("Unable to zero bitmap of size %"PRIu64"\n",
			bmap->mapsize);
		free(bmap->map);
		bmap->map = NULL;
		return ENOMEM;
	}
	log_debug("Allocated bitmap of size %"PRIu64
		  " with %d chunks per byte\n",
		  bmap->mapsize, bmap->chunks_per_byte);
	return 0;
}

int bitmap_set(struct bmap *bmap, uint64_t offset, uint8_t val)
{
	char *byte = NULL;
	uint64_t b = offset;

	if(offset < bmap->size) {
		byte = bmap->map + BITMAP_SIZE(offset, bmap->chunks_per_byte);
		b = BITMAP_BYTE_OFFSET(offset, bmap);

		*byte |= (val & BITMAP_MASK(bmap->chunksize)) << b;
		return 0;
	}
	log_debug("offset %d out of bounds\n", offset);
	return -1;
}

int bitmap_get(struct bmap *bmap, uint64_t bit, uint8_t *val)
{
	char *byte = NULL;
	uint64_t b = bit;

	if(bit < bmap->size) {
		byte = bmap->map + BITMAP_SIZE(bit, bmap->chunks_per_byte);
		b = BITMAP_BYTE_OFFSET(bit, bmap);

		*val = (*byte & (BITMAP_MASK(bmap->chunksize) << b )) >> b;
		return 0;
	}
	log_debug("offset %d out of bounds\n", bit);
	return -1;
}


int bitmap_clear(struct bmap *bmap, uint64_t offset)
{
	char *byte = NULL;
	uint64_t b = offset;

	if(offset < bmap->size) {
		byte = bmap->map + BITMAP_SIZE(offset, bmap->chunks_per_byte);
		b = BITMAP_BYTE_OFFSET(offset, bmap);

		*byte &= ~(BITMAP_MASK(bmap->chunksize) << b);
		return 0;
	}
	log_debug("offset %d out of bounds\n", offset);
	return -1;

}

void bitmap_destroy(struct bmap *bmap)
{
	if(bmap->map)
		free(bmap->map);
	bmap->size = 0;
	bmap->mapsize = 0;
	bmap->chunksize = 0;
	bmap->chunks_per_byte = 0;
}
