#ifndef __OLD_PARSER_H__
#define __OLD_PARSER_H__

/* A special error number for CCS.  This error indicates that there was **
** an error while parsing data.  This number should not collide with    **
** other Linux error numbers, however, if someone has a better errno in **
** mind, please let me know............................................ */
#define EPARSE 2050

enum {
  CCS_STRING,
  CCS_FLOAT,
  CCS_INT,
};


typedef struct ccs_value {
  int type;
  union {
    int i;
    float r;
    char *str;
  } v;
  struct ccs_value *next; /* for arrays */
} ccs_value_t;

typedef struct ccs_node_s{
  char *key;
  struct ccs_node_s *sib, *child;
  struct ccs_value *v;
} ccs_node_t;


void free_ccs_node(ccs_node_t *cn);

int parse_ccs_file(int fd, ccs_node_t **cn, char *filename);

ccs_node_t *find_ccs_node(ccs_node_t *cn,
                          const char *path,
                          char seperator);

const char *find_ccs_str(ccs_node_t *cn,
                         const char *path,
                         char sep,
                         const char *fail);

int find_ccs_int(ccs_node_t *cn,
                 const char *path,
                 char sep,
                 int fail);

#ifndef __KERNEL__
float find_ccs_float(ccs_node_t *cn,
                     const char *path,
                     char sep,
                     float fail);
#endif

int find_ccs_bool(ccs_node_t *cn,
                  const char *path,
                  char sep,
                  int fail);

#endif /* __OLD_PARSER_H__ */
