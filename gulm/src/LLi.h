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
 * circular linked list without mallocs.
 *
 * Now if I did this right, you should be able to put this into the middle
 * of some other struct, and it will work.
 *
 * More importantly, you should be able to have that one larger struct be
 * in the middle of two different LLi lists.
 *
 * It is generally a wise idea to set the data pointer for the list head to
 * NULL.  So wise, that we'll do it for you.(LLi_init_head)
 *
 */
#ifndef __LLi_h__
#define __LLi_h__

typedef struct LLi_s {
   struct LLi_s *next;
   struct LLi_s *prev;
   void *data;
} LLi_t;

/**
 * LLi_init - initialize this list item to be useful.
 */
static void __inline__ LLi_init(LLi_t *i, void *d)
{
   i->next = i;
   i->prev = i;
   i->data = d;
}
#define LLi_init_head(i) LLi_init((i),NULL)
/* use this like:  LLi_t Item = LLi_Static_init_head;  */
#define LLi_Static_init_head {&item,&item,NULL}
#define LLi_Static_head_init(a) LLi_t (a) = {&(a),&(a),NULL}

/*
 * LLi_unhook - reset the next and prev pointers. DONOT call this before
 *              calling LLi_del!
 */
static void __inline__ LLi_unhook(LLi_t *i) 
{
   i->next = i;
   i->prev = i;
}

/*
 * LLi_add_after - add item n after item c
 */
static void __inline__ LLi_add_after(LLi_t *c, LLi_t *n)
{
   n->next = c->next;
   c->next = n;
   n->prev = c;
   n->next->prev = n;
}

/*
 * LLi_add_before - add item n before item c
 */
static void __inline__ LLi_add_before(LLi_t *c, LLi_t *n)
{
   n->prev = c->prev;
   c->prev = n;
   n->next = c;
   n->prev->next = n;
}

/*
 * LLi_del - remove item from list. NOTE! item's pointers are still valid!
 */
static void __inline__ LLi_del(LLi_t *c)
{
   c->prev->next = c->next;
   c->next->prev = c->prev;
}

/*
 * LLi_pop - pop an item off of the list.  Useful for Queues and Stacks
 */
static LLi_t __inline__ *LLi_pop(LLi_t *c)
{
   LLi_t *t;
   if( (c)->next == (c) ) return NULL;
   t = c->next;
   LLi_del(t);
   LLi_unhook(t);
   return t;
}

/* try to always use these if you want "low level" access to the
 * LinkedList.  That way the details can change and you shouldn't need to
 * fix your code.
 */
#define LLi_data(i) ((i)->data)
#define LLi_next(i) ((i)->next)
#define LLi_prev(i) ((i)->prev)

#define LLi_empty(i) ((i)->next == (i))

#endif /*__LLi_h__*/
/* vim: set ai cin et sw=3 ts=3 : */
