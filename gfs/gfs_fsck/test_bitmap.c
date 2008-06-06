#include <stdint.h>
#include <stdio.h>
#include "log.h"
#include "bitmap.h"

int main(int argc, char **argv)
{
	struct bmap map;
	uint8_t val = 0;

	bitmap_create(&map, 1000, 8);

	bitmap_set(&map, 1, 3);

	bitmap_get(&map, 1, &val);

	printf("%d\n", val);

	bitmap_set(&map, 2, 7);

	bitmap_get(&map, 2, &val);

	printf("%d\n", val);

	bitmap_get(&map, 3, &val);

	printf("%d\n", val);

	bitmap_clear(&map, 2);

	bitmap_get(&map, 2, &val);

	printf("%d\n", val);

	bitmap_destroy(&map);


}
