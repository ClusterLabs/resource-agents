/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/
/******************************************************************************
 * Hash table abstraction
 * takes any lengthed keys.
 *
 */
#include <stdlib.h> /* for malloc */
#include <string.h> /* for memcmp */
#include <stdint.h>

#include "hash.h"

#undef MIN
#define MIN(a,b) ((a<b)?a:b)

struct hash_root_s {
	LLi_t *buckets;
	unsigned int numbkt;
	unsigned char *(*getkey)(void *item);
	int (*getkeylen)(void *item);
};

/*
--------------------------------------------------------------------
mix -- mix 3 32-bit values reversibly.
For every delta with one or two bits set, and the deltas of all three
  high bits or all three low bits, whether the original value of a,b,c
  is almost all zero or is uniformly distributed,
* If mix() is run forward or backward, at least 32 bits in a,b,c
  have at least 1/4 probability of changing.
* If mix() is run forward, every bit of c will change between 1/3 and
  2/3 of the time.  (Well, 22/100 and 78/100 for some 2-bit deltas.)
mix() takes 36 machine instructions, but only 18 cycles on a superscalar
  machine (like a Pentium or a Sparc).  No faster mixer seems to work,
  that's the result of my brute-force search.  There were about 2^^68
  hashes to choose from.  I only tested about a billion of those.
--------------------------------------------------------------------
*/
#define mix(a,b,c) \
{ \
  a -= b; a -= c; a ^= (c>>13); \
  b -= c; b -= a; b ^= (a<<8); \
  c -= a; c -= b; c ^= (b>>13); \
  a -= b; a -= c; a ^= (c>>12);  \
  b -= c; b -= a; b ^= (a<<16); \
  c -= a; c -= b; c ^= (b>>5); \
  a -= b; a -= c; a ^= (c>>3);  \
  b -= c; b -= a; b ^= (a<<10); \
  c -= a; c -= b; c ^= (b>>15); \
}

/*
--------------------------------------------------------------------
hash() -- hash a variable-length key into a 32-bit value
  k       : the key (the unaligned variable-length array of bytes)
  len     : the length of the key, counting by bytes
  initval : can be any 4-byte value
Returns a 32-bit value.  Every bit of the key affects every bit of
the return value.  Every 1-bit and 2-bit delta achieves avalanche.
About 6*len+35 instructions.

The best hash table sizes are powers of 2.  There is no need to do
mod a prime (mod is sooo slow!).  If you need less than 32 bits,
use a bitmask.  For example, if you need only 10 bits, do
  h = (h & hashmask(10));
In which case, the hash table should have hashsize(10) elements.

If you are hashing n strings (ub1 **)k, do it like this:
  for (i=0, h=0; i<n; ++i) h = hash( k[i], len[i], h);

By Bob Jenkins, 1996.  bob_jenkins@burtleburtle.net.  You may use this
code any way you wish, private, educational, or commercial.  It's free.

See http://burtleburtle.net/bob/hash/evahash.html
Use for hash table lookup, or anything where one collision in 2^^32 is
acceptable.  Do NOT use for cryptographic purposes.
--------------------------------------------------------------------
*/

/* XXX this thing doesn't appear to maintain results across
 * cpu/libc/kernels So must use a real crc for that.
 */
uint32_t bob_hash( k, length, initval)
register uint8_t  *k;        /* the key */
register uint32_t  length;   /* the length of the key */
register uint32_t  initval;  /* the previous hash, or an arbitrary value */
{
   register uint32_t a,b,c,len;

   /* Set up the internal state */
   len = length;
   a = b = 0x9e3779b9;  /* the golden ratio; an arbitrary value */
   c = initval;         /* the previous hash value */

   /*---------------------------------------- handle most of the key */
   while (len >= 12)
   {
      a += (k[0]+((uint32_t)k[1]<<8)+((uint32_t)k[2]<<16)+((uint32_t)k[3]<<24));
      b += (k[4]+((uint32_t)k[5]<<8)+((uint32_t)k[6]<<16)+((uint32_t)k[7]<<24));
      c += (k[8]+((uint32_t)k[9]<<8)+((uint32_t)k[10]<<16)+((uint32_t)k[11]<<24));
      mix(a,b,c);
      k += 12; len -= 12;
   }

   /*------------------------------------- handle the last 11 bytes */
   c += length;
   switch(len)              /* all the case statements fall through */
   {
   case 11: c+=((uint32_t)k[10]<<24);
   case 10: c+=((uint32_t)k[9]<<16);
   case 9 : c+=((uint32_t)k[8]<<8);
      /* the first byte of c is reserved for the length */
   case 8 : b+=((uint32_t)k[7]<<24);
   case 7 : b+=((uint32_t)k[6]<<16);
   case 6 : b+=((uint32_t)k[5]<<8);
   case 5 : b+=k[4];
   case 4 : a+=((uint32_t)k[3]<<24);
   case 3 : a+=((uint32_t)k[2]<<16);
   case 2 : a+=((uint32_t)k[1]<<8);
   case 1 : a+=k[0];
     /* case 0: nothing left to add */
   }
   mix(a,b,c);
   /*-------------------------------------------- report the result */
   return c;
}
/* public functions */
#define hash_init_val 0x6d696b65
uint32_t gen_hash_key(uint8_t *a, uint32_t b, uint32_t c)
{return bob_hash(a, b, c);}

/**
 * hash_create - create a new hash table
 * @table_size: < number of buckets to have
 * @(*getkey): < function that gets the key out of an item
 * @(*getkeylen): < function that gets the length of the key from an item
 * 
 * This is the only place that memory is created.  Mostly it just fills in
 * the structure.
 * 
 * Returns: hash_t
 */
hash_t *hash_create(unsigned int table_size,
      unsigned char *(*getkey)(void *item),
      int (*getkeylen)(void *item))
{
	hash_t *h;
   int i;
	if( getkey == NULL )return NULL;
	if( getkeylen == NULL) return NULL;

	h = malloc(sizeof(hash_t));
	if( h == NULL ) return NULL;
	h->buckets = malloc(table_size * sizeof(LLi_t));
	if( h->buckets == NULL ) { free(h); return NULL; }
	h->numbkt = table_size;
   h->getkey = getkey;
   h->getkeylen = getkeylen;

   for(i=0; i< table_size; i++) {
      LLi_init_head(&h->buckets[i]);
   }

	return h;
}

/**
 * hash_destroy - free up the memory
 * @hsh: <> the hash table to die
 * 
 * WARN! this does NOT free the items in the hash table!
 * 
 * Returns: void
 */
void hash_destroy(hash_t *hsh)
{
   if(hsh==NULL) return;
   if(hsh->buckets) free(hsh->buckets);
   free(hsh);
}

/**
 * hash_find - Finds an element in the hash table
 * @hsh: < the hash table
 * @key: < key to search for
 * @klen: < length of key
 * 
 * 
 * Returns: LLi_t
 */
LLi_t *hash_find(hash_t *hsh, unsigned char *key, unsigned int klen)
{
	unsigned int hash = 0;
	LLi_t *tmp;

   if( hsh == NULL || key == NULL || klen <= 0 ) {
      return NULL;
   }

	hash = bob_hash(key, klen, hash_init_val);
	hash %= hsh->numbkt;

	tmp = &hsh->buckets[hash];
	if( ! LLi_empty( tmp ) ) {
      /* have to skip over head, else the end condition will trigger right
       * away.
       */
		for(tmp = LLi_next(tmp); LLi_data(tmp) != NULL; tmp = LLi_next(tmp)) {
			if( klen == hsh->getkeylen(LLi_data(tmp)) ) {
				if( memcmp(key, hsh->getkey(LLi_data(tmp)), klen) == 0) {
					/* Found a match! */
					return tmp;
				}
			}
		}
	}
	return NULL;
}

/**
 * hash_add - 
 * @hsh: 
 * @item: 
 * 
 * 
 * Returns: int
 */
int hash_add(hash_t *hsh, LLi_t *item)
{
	uint8_t *key;
	unsigned int klen, hash;
	LLi_t *tmp;

   if( hsh == NULL || item == NULL ) return -5;

	key = hsh->getkey(LLi_data(item));
	klen = hsh->getkeylen(LLi_data(item));
	hash = bob_hash(key, klen, hash_init_val);
	hash %= hsh->numbkt;

	tmp = &hsh->buckets[hash];

   /* jic: */
   LLi_unhook(item);

	/* The idea here is that since we have things in sorted order, (and
	 * hopefully the right order.) you just start walking through, and you
	 * will either first find a match.  Or find the place where you belong.
	 *
	 * I could toss this, and just say that if you're dumb enough to insert
	 * multiple keys that are equal, you deserve what you'll get.
	 *  i duno....
	 */
	if( LLi_empty(tmp) ) { /* nothing else there. just add it.*/
		LLi_add_after(tmp, item);
		return 0;
	}
   for(tmp = LLi_next(tmp); tmp->data != NULL; tmp = tmp->next) {
      uint8_t *tmp_key;
      unsigned int tmp_klen;
      tmp_key = hsh->getkey(LLi_data(tmp));
      tmp_klen = hsh->getkeylen(LLi_data(tmp));

      if( klen == tmp_klen ) {
         if( memcmp(key, tmp_key, klen) == 0) {
            return -1; /* all ready there. */
         }
         if( memcmp( key, tmp_key, klen) < 0) {
            LLi_add_before( tmp, item);
            return 0;
         }
      }
      if( memcmp(key, tmp_key, MIN(klen,tmp_klen)) < 0) {
         LLi_add_before( tmp, item);
         return 0;
      }
   }
   LLi_add_before( &hsh->buckets[hash], item);/*bigger than everyone in list*/
   return 0;/* always gets added. */
}

/**
 * hash_del - 
 * @hsh: 
 * @key: 
 * @klen: 
 * 
 * 
 * Returns: LLi_t
 */
LLi_t *hash_del(hash_t *hsh, unsigned char *key, unsigned int klen)
{
	unsigned int hash = 0;
	LLi_t *tmp;

   if( hsh == NULL || key == NULL || klen <= 0 ) {
      return NULL;
   }

	hash = bob_hash(key, klen, hash_init_val);
	hash %= hsh->numbkt;

	tmp = &hsh->buckets[hash];
	if( ! LLi_empty( tmp ) ) {
      /* have to skip over head, else the end condition will trigger right
       * away.
       */
		for(tmp = LLi_next(tmp); LLi_data(tmp) != NULL; tmp = LLi_next(tmp)) {
			if( klen == hsh->getkeylen(LLi_data(tmp)) ) {
				if( memcmp(key, hsh->getkey(LLi_data(tmp)), klen) == 0) {
					/* Found a match! */
					LLi_del(tmp);
					return tmp; /* caller still needs to free memory!!! */
				}
			}
		}
	}
	return NULL;
}


/**
 * hash_walk - iterrate over every item in hash
 * @hsh: <> hash table
 * @on_each: < function to call on each item
 * @misc: < misc data that you want available in the above function
 * 
 * If the on_each function returns non-zero, the itterration is stopped
 * immeaditily, and this funtion returns with that value.
 * 
 * Returns: int
 */
int hash_walk(hash_t *hsh, int (*on_each)(LLi_t *item, void *misc), void *misc)
{
   unsigned int i;
   int ret=0;
   LLi_t *tmp,*next;

   if( hsh == NULL || on_each == NULL ) return -1;

   for(i=0; i < hsh->numbkt; i++) {
      tmp = &hsh->buckets[i];
#if 0
      for(tmp = LLi_next(tmp); LLi_data(tmp) != NULL; tmp = LLi_next(tmp)) {
         if( (ret=on_each(tmp, misc)) != 0 ) return ret;
      }
#endif
/* code above will core if the on_each function makes a LLi_del call on
 * item.  So now we use below.
 */
      for(tmp = LLi_next(tmp); LLi_data(tmp) != NULL; tmp = next) {
         next = LLi_next(tmp);
         if( (ret=on_each(tmp, misc)) != 0 ) return ret;
      }
   }
   return 0;
}
/* vim: set ai cin et sw=3 ts=3 : */
