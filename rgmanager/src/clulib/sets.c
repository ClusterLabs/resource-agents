/**
 @file sets.c - Order-preserving set functions (union / intersection / delta)
                (designed for integer types; a la int, uint64_t, etc...)
 @author Lon Hohberger <lhh at redhat.com>
 */
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <stdlib.h>
#include <sets.h>
#include <sys/time.h>


/**
 Add a value to a set.  This function disregards an add if the value is already
 in the set.  Note that the maximum length of set s must be preallocated; this
 function doesn't do error or bounds checking. 

 @param s		Set to modify
 @param curlen		Current length (modified if added)
 @param val		Value to add
 @return		0 if not added, 1 if added
 */
int
s_add(set_type_t *s, int *curlen, set_type_t val)
{
	int idx=0;

	for (; idx < *curlen; idx++)
		if (s[idx] == val)
			return 0;
	s[*curlen] = val;
	++(*curlen);
	return 1;
}


/**
 Union-set function.  Allocates and returns a new set which is the union of
 the two given sets 'left' and 'right'.  Also returns the new set length.

 @param left		Left set - order is preserved on this set; that is,
			this is the set where the caller cares about ordering.
 @param ll		Length of left set.
 @param right		Right set - order is not preserved on this set during
			the union operation
 @param rl		Length of right set
 @param ret		Return set.  Should * not * be preallocated.
 @param retl		Return set length.  Should be ready to accept 1 integer
			upon calling this function
 @return 		0 on success, -1 on error
 */
int
s_union(set_type_t *left, int ll, set_type_t *right, int rl,
	set_type_t **ret, int *retl)
{
	int l, r, cnt = 0, total;

	total = ll + rl; /* Union will never exceed both sets */

	*ret = malloc(sizeof(set_type_t)*total);
	if (!*ret) {
		return -1;
	}
	memset((void *)(*ret), 0, sizeof(set_type_t)*total);

	cnt = 0;

	/* Add all the ones on the left */
	for (l = 0; l < ll; l++)
		s_add(*ret, &cnt, left[l]);

	/* Add the ones on the left */
	for (r = 0; r < rl; r++)
		s_add(*ret, &cnt, right[r]);

	*retl = cnt;

	return 0;
}


/**
 Intersection-set function.  Allocates and returns a new set which is the 
 intersection of the two given sets 'left' and 'right'.  Also returns the new
 set length.

 @param left		Left set - order is preserved on this set; that is,
			this is the set where the caller cares about ordering.
 @param ll		Length of left set.
 @param right		Right set - order is not preserved on this set during
			the union operation
 @param rl		Length of right set
 @param ret		Return set.  Should * not * be preallocated.
 @param retl		Return set length.  Should be ready to accept 1 integer
			upon calling this function
 @return 		0 on success, -1 on error
 */
int
s_intersection(set_type_t *left, int ll, set_type_t *right, int rl,
	       set_type_t **ret, int *retl)
{
	int l, r, cnt = 0, total;

	total = ll; /* Intersection will never exceed one of the two set
		       sizes */

	*ret = malloc(sizeof(set_type_t)*total);
	if (!*ret) {
		return -1;
	}
	memset((void *)(*ret), 0, sizeof(set_type_t)*total);

	cnt = 0;
	/* Find duplicates */
	for (l = 0; l < ll; l++) {
		for (r = 0; r < rl; r++) {
			if (left[l] != right[r])
				continue;
			if (s_add(*ret, &cnt, right[r]))
				break;
		}
	}

	*retl = cnt;
	return 0;
}


/**
 Delta-set function.  Allocates and returns a new set which is the delta (i.e.
 numbers not in both sets) of the two given sets 'left' and 'right'.  Also
 returns the new set length.

 @param left		Left set - order is preserved on this set; that is,
			this is the set where the caller cares about ordering.
 @param ll		Length of left set.
 @param right		Right set - order is not preserved on this set during
			the union operation
 @param rl		Length of right set
 @param ret		Return set.  Should * not * be preallocated.
 @param retl		Return set length.  Should be ready to accept 1 integer
			upon calling this function
 @return 		0 on success, -1 on error
 */
int
s_delta(set_type_t *left, int ll, set_type_t *right, int rl,
	set_type_t **ret, int *retl)
{
	int l, r, cnt = 0, total, found;

	total = ll + rl; /* Union will never exceed both sets */

	*ret = malloc(sizeof(set_type_t)*total);
	if (!*ret) {
		return -1;
	}
	memset((void *)(*ret), 0, sizeof(set_type_t)*total);

	cnt = 0;

	/* not efficient, but it works */
	/* Add all the ones on the left */
	for (l = 0; l < ll; l++) {
		found = 0;
		for (r = 0; r < rl; r++) {
			if (right[r] == left[l]) {
				found = 1;
				break;
			}
		}
		
		if (found)
			continue;
		s_add(*ret, &cnt, left[l]);
	}


	/* Add all the ones on the right*/
	for (r = 0; r < rl; r++) {
		found = 0;
		for (l = 0; l < ll; l++) {
			if (right[r] == left[l]) {
				found = 1;
				break;
			}
		}
		
		if (found)
			continue;
		s_add(*ret, &cnt, right[r]);
	}

	*retl = cnt;

	return 0;
}


/**
 Subtract-set function.  Allocates and returns a new set which is the
 subtraction of the right set from the left set.
 Also returns the new set length.

 @param left		Left set - order is preserved on this set; that is,
			this is the set where the caller cares about ordering.
 @param ll		Length of left set.
 @param right		Right set - order is not preserved on this set during
			the union operation
 @param rl		Length of right set
 @param ret		Return set.  Should * not * be preallocated.
 @param retl		Return set length.  Should be ready to accept 1 integer
			upon calling this function
 @return 		0 on success, -1 on error
 */
int
s_subtract(set_type_t *left, int ll, set_type_t *right, int rl,
	   set_type_t **ret, int *retl)
{
	int l, r, cnt = 0, total, found;

	total = ll; /* Union will never exceed left set length*/

	*ret = malloc(sizeof(set_type_t)*total);
	if (!*ret) {
		return -1;
	}
	memset((void *)(*ret), 0, sizeof(set_type_t)*total);

	cnt = 0;

	/* not efficient, but it works */
	for (l = 0; l < ll; l++) {
		found = 0;
		for (r = 0; r < rl; r++) {
			if (right[r] == left[l]) {
				found = 1;
				break;
			}
		}
		
		if (found)
			continue;
		s_add(*ret, &cnt, left[l]);
	}

	*retl = cnt;

	return 0;
}


/**
 Shuffle-set function.  Weakly randomizes ordering of a set in-place.

 @param set		Set to randomize
 @param sl		Length of set
 @return		0
 */
int
s_shuffle(set_type_t *set, int sl)
{
	int x, newidx;
	unsigned r_state = 0;
	set_type_t t;
	struct timeval tv;

	gettimeofday(&tv, NULL);
	r_state = (int)(tv.tv_usec);

	for (x = 0; x < sl; x++) {
		newidx = (rand_r(&r_state) % sl);
		if (newidx == x)
			continue;
		t = set[x];
		set[x] = set[newidx];
		set[newidx] = t;
	}

	return 0;
}


#ifdef STANDALONE
/* Testbed */
/*
  gcc -o sets sets.c -DSTANDALONE -ggdb -I../../include \
       -Wall -Werror -Wstrict-prototypes -Wextra
 */
int
main(int __attribute__ ((unused)) argc, char __attribute__ ((unused)) **argv)
{
	set_type_t a[] = { 1, 2, 3, 3, 3, 2, 2, 3 };
	set_type_t b[] = { 2, 3, 4 };
	set_type_t *i;
	int ilen = 0, x;

	s_union(a, 8, b, 3, &i, &ilen);

	/* Should return length of 4 - { 1 2 3 4 } */
	printf("set_union [%d] = ", ilen);
	for ( x = 0; x < ilen; x++) {
		printf("%d ", (int)i[x]);
	}
	printf("\n");

	s_shuffle(i, ilen);
	printf("shuffled [%d] = ", ilen);
	for ( x = 0; x < ilen; x++) {
		printf("%d ", (int)i[x]);
	}
	printf("\n");


	free(i);

	/* Should return length of 2 - { 2 3 } */
	s_intersection(a, 8, b, 3, &i, &ilen);

	printf("set_intersection [%d] = ", ilen);
	for ( x = 0; x < ilen; x++) {
		printf("%d ", (int)i[x]);
	}
	printf("\n");

	free(i);

	/* Should return length of 2 - { 1 4 } */
	s_delta(a, 8, b, 3, &i, &ilen);

	printf("set_delta [%d] = ", ilen);
	for ( x = 0; x < ilen; x++) {
		printf("%d ", (int)i[x]);
	}
	printf("\n");

	free(i);

	/* Should return length of 1 - { 1 } */
	s_subtract(a, 8, b, 3, &i, &ilen);

	printf("set_subtract [%d] = ", ilen);
	for ( x = 0; x < ilen; x++) {
		printf("%d ", (int)i[x]);
	}
	printf("\n");

	free(i);


	return 0;
}
#endif
