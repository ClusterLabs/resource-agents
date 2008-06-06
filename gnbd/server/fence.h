#ifndef __fence_h__
#define __fence_h__

int update_timestamp_list(char *node, uint64_t timestamp);
int check_banned_list(char *node);
int add_to_banned_list(char *node);
void remove_from_banned_list(char *node);
int list_banned(char **buffer, uint32_t *list_size);

#endif /* __fence_h__ */
