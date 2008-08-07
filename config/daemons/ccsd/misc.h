#ifndef __MISC_H__
#define __MISC_H__

typedef struct open_doc {
  int od_refs;
  xmlDocPtr od_doc;
} open_doc_t;


extern volatile int quorate;
extern int update_required;
extern pthread_mutex_t update_lock;
extern open_doc_t *master_doc;

char *get_cluster_name(xmlDocPtr ldoc);
int get_doc_version(xmlDocPtr ldoc);
int set_ccs_logging(xmlDocPtr ldoc);

#endif /* __MISC_H__ */
