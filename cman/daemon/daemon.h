extern int send_status_return(struct connection *con, uint32_t cmd, int status);
extern int send_data_reply(struct connection *con, int nodeid, int port, char *data, int len);
extern void set_cman_timeout(int secs);
extern void notify_listeners(struct connection *con, int reason, int arg);
extern int num_listeners(void);
extern void cman_set_realtime(void);
extern int cman_init(void);
extern int cman_finish(void);
extern void notify_confchg(struct sock_header *message);

extern volatile sig_atomic_t quit_threads;
extern int num_connections;
