#include <stdio.h>
#include <stdint.h>
#include "block_list.h"

#define BITS 100
void print_map(struct block_list *il, int count);

int main(int argc, char **argv)
{
	int i;
	struct block_list *il;

	il = block_list_create(BITS, gbmap);

	/*for(i = 0; i < BITS; i++) {
		block_check(il, i, &k);
		printf("Block %d is %lu\n", i, k);
		}*/
	print_map(il, BITS);

	block_mark(il, 3, meta_free);
	block_mark(il, 6, inode_lnk);
	block_mark(il, 6, bad_block);
	block_mark(il, BITS-2, meta_inval);
	block_mark(il, BITS-1, meta_free);
	if(block_mark(il, BITS, meta_free)) {
		fprintf(stderr, "Block %d out of bounds\n", BITS);
	}

	/*for(i = 0; i < BITS; i++) {
		block_check(il, i, &k);
		printf("Block %d is %lu\n", i, k);
		}*/
	print_map(il, BITS);

	for(i = 70; i < 80; i++) {
		block_mark(il, i, meta_free);
	}

	block_clear(il, BITS-2, meta_free);

	/*for(i = 0; i < BITS; i++) {
		block_check(il, i, &k);
		printf("Block %d is %lu\n", i, k);
		}*/
	print_map(il, BITS);
	return 0;

}

void print_map(struct block_list *il, int count)
{
	int i, j;
	struct block_query q;

	printf("Printing map of blocks - 60 blocks per row\n");
	j = 0;
	for(i = 0; i < count; i++) {

		if(j > 59) {
			printf("\n");
			j = 0;
		}
		else if(!(j %10) && j != 0) {
			printf(" ");
		}
		j++;
		block_check(il, i, &q);
		printf("%X", q.block_type);

	}
	printf("\n");

	printf("Printing map of bad blocks - 60 blocks per row\n");
	j = 0;
	for(i = 0; i < count; i++) {

		if(j > 59) {
			printf("\n");
			j = 0;
		}
		else if(!(j %10) && j != 0) {
			printf(" ");
		}
		j++;
		block_check(il, i, &q);
		printf("%X", q.bad_block);

	}
	printf("\n");
}
