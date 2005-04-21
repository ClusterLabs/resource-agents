void add_timer(struct cman_timer *timer, time_t sec, int usec);
void del_timer(struct cman_timer *timer);
int send_status_return(struct connection *con, uint32_t cmd, int status);
int send_reply_message(struct connection *con, struct sock_header *msg);
int send_data_reply(struct connection *con, int nodeid, int port, char *data, int len);
void set_cman_timeout(int secs);
void notify_listeners(struct connection *con, int reason, int arg);

