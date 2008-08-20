uint32_t gfs2_block_size(char *dev);
int gfs2_parse(char *dev, void (*func)(long int b, char *t, long int p, char *f));
void gfs2_stop(void);
