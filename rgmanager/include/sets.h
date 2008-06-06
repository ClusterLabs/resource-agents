/**
 @file sets.h - Header file for sets.c
 @author Lon Hohberger <lhh at redhat.com>
 */
#ifndef _SETS_H
#define _SETS_H

/* #include <stdint.h> */
typedef int set_type_t;

int s_add(set_type_t *, int *, set_type_t);
int s_union(set_type_t *, int, set_type_t *,
	    int, set_type_t **, int *);

int s_intersection(set_type_t *, int, set_type_t *,
		   int, set_type_t **, int *);
int s_delta(set_type_t *, int, set_type_t *,
	    int, set_type_t **, int *);
int s_subtract(set_type_t *, int, set_type_t *, int, set_type_t **, int *);
int s_shuffle(set_type_t *, int);

#endif
