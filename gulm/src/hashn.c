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
 * Doesn't do per-item allocs, expects things to come in as LLis.
 * This differs from old in that it does not relie on memory buffers as
 * keys.  This allows one to use multiple fields in a structure to be the
 * `key' for comparing and storing.
 *
 */
#include <stdlib.h> /* for malloc */
#include <string.h> /* for memcmp */
#include <stdint.h>

#include "hashn.h"

struct hashn_root_s {
	LLi_t *buckets;
	unsigned int numbkt;

   int (*cmp)(void *a, void *b);
   int (*hash)(void *a);
};

/**
 * hash_create - create a new hash table
 * @table_size: < number of buckets to have
 * @(*cmp): < 
 * @(*hash): < 
 *
 * cmp is a compare function that returns -1, 0, 1 if a is less than,
 *  equal to, or greather than b.
 * hash is a function that returns a low collision digest of what you think
 *  is important of an item.
 * For both funtions, generally they only operate on a single key field in
 * the structures you store in this map.  (though it can be the whole
 * thing, or multiple parts if you want.)
 * 
 * This is the only place that memory is created.  Mostly it just fills in
 * the structure.
 * 
 * Returns: hash_t
 */
hashn_t *hashn_create(unsigned int table_size,
      int (*cmp)(void *a, void *b),
      int (*hash)(void *a))
{
	hashn_t *h;
   int i;
	if( cmp == NULL )return NULL;
	if( hash == NULL) return NULL;

	h = malloc(sizeof(hashn_t));
	if( h == NULL ) return NULL;
	h->buckets = malloc(table_size * sizeof(LLi_t));
	if( h->buckets == NULL ) { free(h); return NULL; }
	h->numbkt = table_size;
   h->cmp = cmp;
   h->hash = hash;

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
void hashn_destroy(hashn_t *hsh)
{
   if(hsh==NULL) return;
   if(hsh->buckets) free(hsh->buckets);
   free(hsh);
}

/**
 * hash_find - Finds an element in the hash table
 * @hsh: < the hash table
 * @item: < LLi whose data points to a struct containing what to look for
 * 
 * The only fields needed in item->data are those that your cmp function
 * uses.  Returns a different LLi
 * 
 * Returns: LLi_t
 */
LLi_t *hashn_find(hashn_t *hsh, LLi_t *item)
{
	unsigned int bkt = 0;
	LLi_t *tmp;

   if( hsh == NULL || item == NULL ) {
      return NULL;
   }

   bkt = hsh->hash(LLi_data(item));
	bkt %= hsh->numbkt;

	tmp = &hsh->buckets[bkt];
	if( ! LLi_empty( tmp ) ) {
      /* have to skip over head, else the end condition will trigger right
       * away.
       */
		for(tmp = LLi_next(tmp); LLi_data(tmp) != NULL; tmp = LLi_next(tmp)) {
         if( hsh->cmp( LLi_data(item), LLi_data(tmp) ) == 0 ) {
            /* Found a match! */
            return tmp;
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
int hashn_add(hashn_t *hsh, LLi_t *item)
{
	unsigned int bkt;
   int ret;
	LLi_t *tmp;

   if( hsh == NULL || item == NULL ) return -5;

   bkt = hsh->hash(LLi_data(item));
	bkt %= hsh->numbkt;

	tmp = &hsh->buckets[bkt];

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
      ret = hsh->cmp(LLi_data(item), LLi_data(tmp));
      if( ret == 0 ) {
         /* already exists */
         return -1;
      }else
      if( ret < 0 ) {
         LLi_add_before( tmp, item);
         return 0;
      }
   }
   LLi_add_before( &hsh->buckets[bkt], item);/*bigger than everyone in list*/
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
LLi_t *hashn_del(hashn_t *hsh, LLi_t *item)
{
	unsigned int hash = 0;
	LLi_t *tmp;

   if( hsh == NULL || item == NULL ) {
      return NULL;
   }

	hash = hsh->hash(LLi_data(item));
	hash %= hsh->numbkt;

	tmp = &hsh->buckets[hash];
	if( ! LLi_empty( tmp ) ) {
      /* have to skip over head, else the end condition will trigger right
       * away.
       */
		for(tmp = LLi_next(tmp); LLi_data(tmp) != NULL; tmp = LLi_next(tmp)) {
         if( hsh->cmp(LLi_data(item), LLi_data(tmp)) == 0) {
            /* Found a match! */
            LLi_del(tmp);
            LLi_unhook(tmp);
            return tmp; /* caller still needs to free memory!!! */
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
int hashn_walk(hashn_t *hsh, int (*on_each)(LLi_t *item, void *misc), void *misc)
{
   unsigned int i;
   int ret=0;
   LLi_t *tmp,*next;

   if( hsh == NULL || on_each == NULL ) return -1;

   for(i=0; i < hsh->numbkt; i++) {
      tmp = &hsh->buckets[i];
      for(tmp = LLi_next(tmp); LLi_data(tmp) != NULL; tmp = next) {
         next = LLi_next(tmp);
         if( (ret=on_each(tmp, misc)) != 0 ) return ret;
      }
   }
   return 0;
}
/* vim: set ai cin et sw=3 ts=3 : */
