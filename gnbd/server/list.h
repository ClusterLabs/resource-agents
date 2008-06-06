#ifndef __list_h__
#define __list_h__

struct list
{
  struct list *next, *prev;
};
typedef struct list list_t;



#define list_decl(var) list_t var = { &var, &var }

#define list_empty(var) ((var)->next == (var))
#define list_entry(var, type, mem) ((type *)((unsigned long)(var) - (unsigned long)(&((type *)NULL)->mem)))



#define list_init(head) \
do \
{ \
  list_t *list_var = (head); \
  list_var->next = list_var->prev = list_var; \
} \
while (0)

#define list_add(new, head) \
do \
{ \
  list_t *list_var_new = (new); \
  list_t *list_var_head = (head); \
  list_var_new->next = list_var_head->next; \
  list_var_new->prev = list_var_head; \
  list_var_head->next->prev = list_var_new; \
  list_var_head->next = list_var_new; \
} \
while (0)

#define list_add_next list_add

#define list_add_prev(new, head) \
do \
{ \
  list_t *list_var_new = (new); \
  list_t *list_var_head = (head); \
  list_var_new->prev = list_var_head->prev; \
  list_var_new->next = list_var_head; \
  list_var_head->prev->next = list_var_new; \
  list_var_head->prev = list_var_new; \
} \
while (0)

#define list_del(var) \
do \
{ \
  list_t *list_var = (var); \
  list_var->next->prev = list_var->prev; \
  list_var->prev->next = list_var->next; \
} \
while (0)

#define list_del_init(var) \
do \
{ \
  list_t *list_var = (var); \
  list_var->next->prev = list_var->prev; \
  list_var->prev->next = list_var->next; \
  list_var->next = list_var->prev = list_var; \
} \
while (0)

#define list_foreach(tmp, head) \
  for ((tmp) = (head)->next; (tmp) != (head); (tmp) = (tmp)->next) 

#define list_foreach_safe(tmp, head, x) \
  for ((tmp) = (head)->next, (x) = (tmp)->next; \
       (tmp) != (head); \
       (tmp) = (x), (x) = (x)->next)



#endif  /*  __OSI_LIST_DOT_H__  */
