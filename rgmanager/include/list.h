#ifndef _LIST_H
#define _LIST_H

/* Must be first if structure is going to use it. */
struct list_entry {
	struct list_entry *le_next, *le_prev;
};

#define le(p) ((struct list_entry *)p)

#define list_insert(list, newnode) \
do { \
	if (!(*list)) { \
		le(newnode)->le_next = \
		le(newnode)->le_prev = le(newnode); \
		le(*list) = le(newnode); \
	} else { \
		le(*list)->le_prev->le_next = le(newnode); \
		le(newnode)->le_next = le(*list); \
		le(newnode)->le_prev = le(*list)->le_prev; \
		le(*list)->le_prev = le(newnode); \
	} \
} while (0)

#define list_remove(list, oldnode) \
do { \
	if (le(oldnode) == le(*list)) { \
		le(*list) = le(*list)->le_next; \
	} \
	if (le(oldnode) == le(*list)) { \
		le(oldnode)->le_next = NULL; \
		le(oldnode)->le_prev = NULL; \
		le(*list) = NULL; \
	} else { \
		le(oldnode)->le_next->le_prev = le(oldnode)->le_prev; \
		le(oldnode)->le_prev->le_next = le(oldnode)->le_next; \
		le(oldnode)->le_prev = NULL; \
	       	le(oldnode)->le_next = NULL; \
	} \
} while (0)

#define list_do(list, curr) \
	if (*list && (le(curr) = le(*list))) do

#define list_done(list, curr) \
	(((le(curr) = le(curr)->le_next)) && (curr == *list))

#endif
