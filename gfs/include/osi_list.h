#ifndef __OSI_LIST_DOT_H__
#define __OSI_LIST_DOT_H__



struct osi_list
{
  struct osi_list *next, *prev;
};
typedef struct osi_list osi_list_t;



#define osi_list_decl(var) osi_list_t var = { &var, &var }

#define osi_list_empty(var) ((var)->next == (var))
#define osi_list_entry(var, type, mem) ((type *)((unsigned long)(var) - (unsigned long)(&((type *)NULL)->mem)))



#define osi_list_init(head) \
do \
{ \
  osi_list_t *osi_list_var = (head); \
  osi_list_var->next = osi_list_var->prev = osi_list_var; \
} \
while (0)

#define osi_list_add(new, head) \
do \
{ \
  osi_list_t *osi_list_var_new = (new); \
  osi_list_t *osi_list_var_head = (head); \
  osi_list_var_new->next = osi_list_var_head->next; \
  osi_list_var_new->prev = osi_list_var_head; \
  osi_list_var_head->next->prev = osi_list_var_new; \
  osi_list_var_head->next = osi_list_var_new; \
} \
while (0)

#define osi_list_add_next osi_list_add

#define osi_list_add_prev(new, head) \
do \
{ \
  osi_list_t *osi_list_var_new = (new); \
  osi_list_t *osi_list_var_head = (head); \
  osi_list_var_new->prev = osi_list_var_head->prev; \
  osi_list_var_new->next = osi_list_var_head; \
  osi_list_var_head->prev->next = osi_list_var_new; \
  osi_list_var_head->prev = osi_list_var_new; \
} \
while (0)

#define osi_list_del(var) \
do \
{ \
  osi_list_t *osi_list_var = (var); \
  osi_list_var->next->prev = osi_list_var->prev; \
  osi_list_var->prev->next = osi_list_var->next; \
} \
while (0)

#define osi_list_del_init(var) \
do \
{ \
  osi_list_t *osi_list_var = (var); \
  osi_list_var->next->prev = osi_list_var->prev; \
  osi_list_var->prev->next = osi_list_var->next; \
  osi_list_var->next = osi_list_var->prev = osi_list_var; \
} \
while (0)

#define osi_list_foreach(tmp, head) \
  for ((tmp) = (head)->next; (tmp) != (head); (tmp) = (tmp)->next) 

#define osi_list_foreach_safe(tmp, head, x) \
  for ((tmp) = (head)->next, (x) = (tmp)->next; \
       (tmp) != (head); \
       (tmp) = (x), (x) = (x)->next)



#endif  /*  __OSI_LIST_DOT_H__  */
