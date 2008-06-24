/* Large chunks of this code were lifted from nbd.c */

#include <linux/major.h>

#include <linux/blkdev.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/bio.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/ioctl.h>
#include <net/sock.h>
#include <linux/in.h>
#include <linux/buffer_head.h>
#include <linux/miscdevice.h>
#include <linux/moduleparam.h>
#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif

#include <asm/uaccess.h>
#include <asm/types.h>

#include "gnbd.h"

static int major_nr = 0;
uint64_t insmod_time;


#define GNBD_MAGIC 0x74d06100

#ifdef NDEBUG
#define dprintk(flags, fmt...)
#else /* NDEBUG */
#define dprintk(flags, fmt...) do { \
	if (debugflags & (flags)) printk(KERN_DEBUG fmt); \
} while (0)
#define DBG_IOCTL       0x0004
#define DBG_INIT        0x0010
#define DBG_EXIT        0x0020
#define DBG_BLKDEV      0x0100
#define DBG_RX          0x0200
#define DBG_TX          0x0400
static unsigned int debugflags;
#endif /* NDEBUG */

static struct gnbd_device gnbd_dev[MAX_GNBD];

struct request shutdown_req;
struct request ping_req;

static spinlock_t gnbd_lock = SPIN_LOCK_UNLOCKED;

#define to_gnbd_dev(d) container_of(d, struct gnbd_device, class_dev)

static void gnbd_class_release(struct device *class_dev)
{
	/* FIXME -- What the hell do I have to free up here */
}

static struct class gnbd_class = {
	.name = "gnbd",
	.dev_release = gnbd_class_release
};

static ssize_t show_pid(struct device *class_dev, struct device_attribute *attr, char *buf)
{
	struct gnbd_device *dev = to_gnbd_dev(class_dev);
	return sprintf(buf, "%d\n", dev->receiver_pid);
}

DEVICE_ATTR(pid, S_IRUGO, show_pid, NULL);

static ssize_t show_server(struct device *class_dev, struct device_attribute *attr, char *buf)
{
	struct gnbd_device *dev = to_gnbd_dev(class_dev);
	if (dev->server_name)
		return sprintf(buf, "%s/%hx\n", dev->server_name,
				dev->server_port);
	else
		return sprintf(buf, "\n");
}

/* FIXME -- should a empty store free the memory */
static ssize_t store_server(struct device *class_dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int res;
	short unsigned int port;
	char *ptr;
	struct gnbd_device *dev = to_gnbd_dev(class_dev);
	if (down_trylock(&dev->do_it_lock))
		return -EBUSY;
	if (dev->server_name)
		kfree(dev->server_name);
	dev->server_name = kmalloc(count + 1, GFP_KERNEL);
	if (!dev->server_name)
		return -ENOMEM;
	memcpy(dev->server_name, buf, count);
	dev->server_name[count] = 0;
	ptr = strchr(dev->server_name, '/');
	if (!ptr)
		return -EINVAL;
	*ptr++ = 0;
	res = sscanf(ptr, "%4hx", &port);
	if (res != 1){
		up(&dev->do_it_lock);
		return -EINVAL;
	}
	dev->server_port = port;
	up(&dev->do_it_lock);
	return count;
}

DEVICE_ATTR(server, S_IRUGO | S_IWUSR, show_server, store_server);

static ssize_t show_name(struct device *class_dev, struct device_attribute *attr, char *buf)
{
	struct gnbd_device *dev = to_gnbd_dev(class_dev);
	return sprintf(buf, "%s\n", dev->name);
}

static ssize_t store_name(struct device *class_dev, struct device_attribute *attr,
                const char *buf, size_t count)
{
	int res;
	struct gnbd_device *dev = to_gnbd_dev(class_dev);
	if (down_trylock(&dev->do_it_lock))
		return -EBUSY;
	res = sscanf(buf, "%31s", dev->name);
	up(&dev->do_it_lock);
	if (res != 1)
		return -EINVAL;
	return count;
}

DEVICE_ATTR(name, S_IRUGO | S_IWUSR, show_name, store_name);


static ssize_t show_sectors(struct device *class_dev, struct device_attribute *attr, char *buf)
{
	struct gnbd_device *dev = to_gnbd_dev(class_dev);
	return sprintf(buf, "%Lu\n",
			(unsigned long long)get_capacity(dev->disk));
}

static ssize_t store_sectors(struct device *class_dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int res;
	sector_t size;
	struct block_device *bdev;
	struct gnbd_device *dev = to_gnbd_dev(class_dev);
	
	if (down_trylock(&dev->do_it_lock))
		return -EBUSY;
	res = sscanf(buf, "%Lu\n", (unsigned long long int *)&size);
	if (res != 1){
		up(&dev->do_it_lock);
		return -EINVAL;
	}
	/* FIXME -- should I switch the order here, so that I don't have
	   capacity set to one thing and the bdev inode size set to another */ 
	set_capacity(dev->disk, size);
	bdev = bdget_disk(dev->disk, 0);
	if (bdev) {
		mutex_lock(&bdev->bd_inode->i_mutex);
		i_size_write(bdev->bd_inode, (loff_t)size << 9);
		mutex_unlock(&bdev->bd_inode->i_mutex);
		bdput(bdev);
	}
	up(&dev->do_it_lock);
	return count;
}

DEVICE_ATTR(sectors, S_IRUGO | S_IWUSR, show_sectors, store_sectors);

static ssize_t show_usage(struct device *class_dev, struct device_attribute *attr, char *buf)
{
	struct gnbd_device *dev = to_gnbd_dev(class_dev);
	return sprintf(buf, "%d\n", dev->open_count);
}

DEVICE_ATTR(usage, S_IRUGO, show_usage, NULL);

static ssize_t show_flags(struct device *class_dev, struct device_attribute *attr, char *buf)
{
	struct gnbd_device *dev = to_gnbd_dev(class_dev);
	return sprintf(buf, "0x%04x\n", dev->flags);
}

static ssize_t store_flags(struct device *class_dev, struct device_attribute *attr,
                const char *buf, size_t count)
{
	int res;
	
        struct gnbd_device *dev = to_gnbd_dev(class_dev);
        if (down_trylock(&dev->do_it_lock))
                return -EBUSY;
	res = sscanf(buf, "0x%hx", &dev->flags);
	up(&dev->do_it_lock);
        if (res != 1)
                return -EINVAL;
        return count;
}


DEVICE_ATTR(flags, S_IRUGO | S_IWUSR, show_flags, store_flags);

static ssize_t show_waittime(struct device *class_dev, struct device_attribute *attr, char *buf)
{
	struct gnbd_device *dev = to_gnbd_dev(class_dev);
	if (list_empty(&dev->queue_head))
		return sprintf(buf, "-1\n");
	return sprintf(buf, "%ld\n",
			((long)jiffies - (long)dev->last_received) / HZ);
}

DEVICE_ATTR(waittime, S_IRUGO, show_waittime, NULL);

static ssize_t show_connected(struct device *class_dev, struct device_attribute *attr, char *buf)
{
	struct gnbd_device *dev = to_gnbd_dev(class_dev);
	return sprintf(buf, "%d\n", (dev->sock != NULL));
}

DEVICE_ATTR(connected, S_IRUGO, show_connected, NULL);

#ifndef NDEBUG
static const char *ioctl_cmd_to_ascii(int cmd)
{
	switch (cmd) {
	case GNBD_DO_IT: return "do-it";
	case GNBD_CLEAR_QUE: return "clear-que";
	case GNBD_PRINT_DEBUG: return "print-debug";
	case GNBD_DISCONNECT: return "disconnect";
	}
	return "unknown";
}

static const char *gnbdcmd_to_ascii(int cmd)
{
	switch (cmd) {
	case  GNBD_CMD_READ: return "read";
	case GNBD_CMD_WRITE: return "write";
	case  GNBD_CMD_DISC: return "disconnect";
	case GNBD_CMD_PING: return "ping";
	}
	return "invalid";
}
#endif /* NDEBUG */


static int wait_for_send(struct request *req, struct gnbd_device *dev)
{
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue(&dev->tx_wait, &wait);
	while(dev->current_request == req) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (signal_pending(current)) {
			printk(KERN_WARNING "gnbd (pid %d: %s) wait interrupted by signal\n",
				current->pid, current->comm);
			return -EINTR;
		}
		schedule();
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&dev->tx_wait, &wait);
	return 0;
}

static void gnbd_end_request(struct request *req)
{
	int error = req->errors ? -EIO : 0;
	struct request_queue *q = req->q;
	unsigned long flags;

	dprintk(DBG_BLKDEV, "%s: request %p: %s\n", req->rq_disk->disk_name,
			req, error ? "failed" : "done");

	if (error)
		printk("%s %d called gnbd_end_request with an error\n",
		       current->comm, current->pid);	
	
	spin_lock_irqsave(q->queue_lock, flags);
	__blk_end_request(req, error, req->nr_sectors << 9);
	spin_unlock_irqrestore(q->queue_lock, flags);
}

/*
 *  Send or receive packet.
 */
static int sock_xmit(struct socket *sock, int send, void *buf, int size,
		int msg_flags, int can_signal)
{
	mm_segment_t oldfs;
	int result;
	struct msghdr msg;
	struct iovec iov;
	unsigned long flags;
	sigset_t oldset;

	oldfs = get_fs();
	set_fs(get_ds());
	spin_lock_irqsave(&current->sighand->siglock, flags);
	oldset = current->blocked;
	sigfillset(&current->blocked);
	if (can_signal)
		sigdelsetmask(&current->blocked, sigmask(SIGKILL) |
			      sigmask(SIGTERM) | sigmask(SIGHUP));
	recalc_sigpending();
	spin_unlock_irqrestore(&current->sighand->siglock, flags);

	do {
		sock->sk->sk_allocation = GFP_NOIO;
		iov.iov_base = buf;
		iov.iov_len = size;
		msg.msg_name = NULL;
		msg.msg_namelen = 0;
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;
		msg.msg_control = NULL;
		msg.msg_controllen = 0;
		msg.msg_namelen = 0;
		msg.msg_flags = msg_flags | MSG_NOSIGNAL;

		if (send)
			result = sock_sendmsg(sock, &msg, size);
		else
			result = sock_recvmsg(sock, &msg, size, 0);

		if (can_signal && signal_pending(current)) {
			printk(KERN_WARNING "gnbd (pid %d: %s) got signal\n",
				current->pid, current->comm);
			result = -EINTR;
			break;
		}

		if (result <= 0) {
			if (result == 0)
				result = -EPIPE; /* short read */
			break;
		}
		size -= result;
		buf += result;
	} while (size > 0);

	spin_lock_irqsave(&current->sighand->siglock, flags);
	current->blocked = oldset;
	recalc_sigpending();
	spin_unlock_irqrestore(&current->sighand->siglock, flags);

	set_fs(oldfs);
	return result;
}

static inline int sock_send_bvec(struct socket *sock, struct bio_vec *bvec,
		int flags, int can_signal)
{
	int result;
	void *kaddr = kmap(bvec->bv_page);
	result = sock_xmit(sock, 1, kaddr + bvec->bv_offset, bvec->bv_len,
			flags, can_signal);
	kunmap(bvec->bv_page);
	return result;
}


#define gnbd_send_req(dev, req, can_sig) \
__gnbd_send_req((dev), (dev)->sock, (req), (can_sig))
	
int __gnbd_send_req(struct gnbd_device *dev, struct socket *sock,
		struct request *req, int can_signal)
{
	int result, flags;
	struct gnbd_request request;
	unsigned long size = req->nr_sectors << 9;

	request.magic = htonl(GNBD_REQUEST_MAGIC);
	request.type = htonl(gnbd_cmd(req));
	request.from = cpu_to_be64((u64) req->sector << 9);
	request.len = htonl(size);
	memcpy(request.handle, &req, sizeof(req));

	down(&dev->tx_lock);

	if (dev->corrupt) {
		printk(KERN_ERR "%s: Attempted to send on a faulty socket\n",
		       dev->disk->disk_name);
		result = -EBADFD;
		goto error_out;
	}
	if (!sock) {
		printk(KERN_ERR "%s: Attempted send on closed socket\n",
				dev->disk->disk_name);
		result = -ENOTCONN;
		goto error_out;
	}

	dprintk(DBG_TX, "%s: request %p: sending control (%s@%llu,%luB)\n",
			dev->disk->disk_name, req,
			gnbdcmd_to_ascii(gnbd_cmd(req)),
			(unsigned long long)req->sector << 9,
			req->nr_sectors << 9);
	dev->current_request = req;
	result = sock_xmit(sock, 1, &request, sizeof(request),
			(gnbd_cmd(req) == GNBD_CMD_WRITE)? MSG_MORE: 0,
			can_signal);
	if (result < 0) {
		printk(KERN_ERR "%s: Send control failed (result %d)\n",
				dev->disk->disk_name, result);
		goto send_error_out;
	}

	if (gnbd_cmd(req) == GNBD_CMD_WRITE) {
		struct req_iterator iter;
		struct bio_vec *bvec;
		/*
		 * we are really probing at internals to determine
		 * whether to set MSG_MORE or not...
		 */
		rq_for_each_segment(bvec, req, iter) {
			flags = 0;
			if (!rq_iter_last(req, iter))
				flags = MSG_MORE;
			dprintk(DBG_TX, "%s: request %p: sending %d bytes data\n",
					dev->disk->disk_name, req,
					bvec->bv_len);
			result = sock_send_bvec(sock, bvec, flags,
						can_signal);
			if (result < 0) {
				printk(KERN_ERR "%s: Send data failed (result %d)\n",
						dev->disk->disk_name,
						result);
				goto send_error_out;
			}
		}
	}
	dev->current_request = NULL;
	wake_up(&dev->tx_wait);
	up(&dev->tx_lock);
	return 0;

send_error_out:
	dev->corrupt = 1;
	dev->current_request = NULL;
	wake_up(&dev->tx_wait);
error_out:
	up(&dev->tx_lock);
	return result;
}

	
static int gnbd_find_request(struct gnbd_device *dev, struct request *xreq)
{
	struct request *req;
	struct list_head *tmp;

	list_for_each(tmp, &dev->queue_head) {
		req = list_entry(tmp, struct request, queuelist);
		if (req != xreq)
			continue;
		return 1;
	}
	return 0;
}

static inline int sock_recv_bvec(struct socket *sock, struct bio_vec *bvec)
{
	int result;
	void *kaddr = kmap(bvec->bv_page);
	result = sock_xmit(sock, 0, kaddr + bvec->bv_offset, bvec->bv_len,
			MSG_WAITALL, 1);
	kunmap(bvec->bv_page);
	return result;
}

int gnbd_recv_req(struct gnbd_device *dev, struct request *req)
{
	int result;
	struct bio_vec *bvec;
	struct req_iterator iter;

	rq_for_each_segment(bvec, req, iter) {
		result = sock_recv_bvec(dev->sock, bvec);
		if (result < 0) {
			printk(KERN_ERR "%s: Receive data failed (result %d)\n",
					dev->disk->disk_name,
					result);
			return result;
		}
		dprintk(DBG_RX, "%s: request %p: got %d bytes data\n",
				dev->disk->disk_name, req, bvec->bv_len);
	}
	return 0;
}

int gnbd_do_it(struct gnbd_device *dev)
{
	int result;
	struct gnbd_reply reply;
	struct request *req;
	struct socket *sock = dev->sock;

	BUG_ON(dev->magic != GNBD_MAGIC);

	while((result = sock_xmit(sock, 0, &reply,sizeof(reply), MSG_WAITALL, 1)) > 0){
		if (dev->corrupt) {
			printk(KERN_ERR "%s: faulty socket\n",dev->disk->disk_name);
			return -EBADFD;
		}

		if (ntohl(reply.magic) == GNBD_KEEP_ALIVE_MAGIC)
			/* FIXME -- I should reset the wait time here */
			continue;

		memcpy(&req, reply.handle, sizeof(req));
		if (req == &shutdown_req)
			return 0;

		if (!gnbd_find_request(dev, req)){
			printk(KERN_ERR "%s: Unexpected reply (%p)\n",
					dev->disk->disk_name, reply.handle);
			return -EBADR;
		}
		if (ntohl(reply.magic) != GNBD_REPLY_MAGIC) {
			printk(KERN_ERR "%s: Wrong magic (0x%lx)\n",
					dev->disk->disk_name,
					(unsigned long)ntohl(reply.magic));
			return -EPROTO;
		}
		if (ntohl(reply.error)) {
			printk(KERN_ERR "%s: Other side returned error (%d)\n",
					dev->disk->disk_name, ntohl(reply.error));
			req->errors++;
			goto remove_req;
		}
		dprintk(DBG_RX, "%s: request %p: got reply\n",
				dev->disk->disk_name, req);

		if (gnbd_cmd(req) == GNBD_CMD_READ){
			result = gnbd_recv_req(dev, req);
			if (result < 0)
				return result;
		}
remove_req:
		result = wait_for_send(req, dev);
		if (result != 0)
			return result;
		spin_lock(&dev->queue_lock);
		list_del_init(&req->queuelist);
		dev->last_received = jiffies;
		spin_unlock(&dev->queue_lock);
		if (req != &ping_req)
			gnbd_end_request(req);
	}
	printk(KERN_ERR "%s: Receive control failed (result %d)\n",
			dev->disk->disk_name, result);
	return result;
}

int gnbd_clear_que(struct gnbd_device *dev)
{
	int err;
	struct request *req;

	BUG_ON(dev->magic != GNBD_MAGIC);

	do {
		req = NULL;
		if (!list_empty(&dev->queue_head)) {
			req = list_entry(dev->queue_head.next, struct request, queuelist);
			err = wait_for_send(req, dev);
			if (err)
				return err;
			list_del_init(&req->queuelist);
		}
		if (req && req != &ping_req) {
			req->errors++;
			gnbd_end_request(req);
		}
	} while (req);

	return 0;
}

/*
 * We always wait for result of write, for now. It would be nice to make it optional
 * in future
 * if ((req->cmd == WRITE) && (dev->flags & GNBD_WRITE_NOCHK)) 
 *   { printk( "Warning: Ignoring result!\n"); gnbd_end_request( req ); }
 */

static void do_gnbd_request(struct request_queue *q)
{
	int err;
	struct request *req;
	
	while ((req = elv_next_request(q)) != NULL) {
		struct gnbd_device *dev;

		blkdev_dequeue_request(req);
		dprintk(DBG_BLKDEV, "%s: request %p: dequeued (flags=%x)\n",
				req->rq_disk->disk_name, req, req->cmd_type);

		if (!blk_fs_request(req))
			goto error_out;

		dev = req->rq_disk->private_data;

		if (dev->receiver_pid == -1)
			goto error_out;
		
		BUG_ON(dev->magic != GNBD_MAGIC);

		gnbd_cmd(req) = GNBD_CMD_READ;
		if (rq_data_dir(req) == WRITE) {
			gnbd_cmd(req) = GNBD_CMD_WRITE;
			if (dev->flags & GNBD_READ_ONLY) {
				printk(KERN_ERR "%s: Write on read-only\n",
						dev->disk->disk_name);
				goto error_out;
			}
		}

		req->errors = 0;
		spin_unlock_irq(q->queue_lock);

		spin_lock(&dev->queue_lock);

		if (list_empty(&dev->queue_head))
			dev->last_received = jiffies;
		list_add(&req->queuelist, &dev->queue_head);
		spin_unlock(&dev->queue_lock);

		err = gnbd_send_req(dev, req, 0);

		spin_lock_irq(q->queue_lock);
		if (err)
			goto sock_error;
		continue;

error_out:
		req->errors++;
		spin_unlock(q->queue_lock);
		gnbd_end_request(req);
		spin_lock(q->queue_lock);
	}
	return;

sock_error:
	return;
}

/*
 * This is called before dev-sock is set, so you dodn't need
 * to worry about the tx_lock or the queue_lock
 */
static int gnbd_resend_requests(struct gnbd_device *dev, struct socket *sock)
{
	int err = 0;
	struct request *req;
	struct list_head *tmp;
	
	printk("resending requests\n");
	list_for_each(tmp, &dev->queue_head) {
		req = list_entry(tmp, struct request, queuelist);
		err = __gnbd_send_req(dev, sock, req, 1);

		if (err){
			printk("failed trying to resend request (%d)\n", err);
			break;
		}
	}

	return err;
}
/*
static int get_server_info(struct gnbd_device *dev, struct socket *sock)
{
	struct sockaddr_in server;
	int len;
	int err;

	err = sock->ops->getname(sock, (struct sockaddr *) &server, &len, 1);
	if (err) {
		printk(KERN_WARNING "cannot get socket info, shutting down\n");
	} else{
		dev->server_addr = server.sin_addr;
		dev->server_port = server.sin_port;
	}
	return err;
}
*/

static int gnbd_ctl_ioctl(struct inode *inode, struct file *file,
		     unsigned int cmd, unsigned long arg)
{
	struct gnbd_device *dev = NULL;
	struct block_device *bdev;
        do_it_req_t req;
	int error;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (cmd == GNBD_DISCONNECT || cmd == GNBD_CLEAR_QUE ||
                        cmd == GNBD_PING || cmd == GNBD_PRINT_DEBUG) {
                if (arg >= MAX_GNBD)
                        return -EINVAL;
                dev = &gnbd_dev[arg];
                BUG_ON(dev->magic != GNBD_MAGIC);
        }

	/* Anyone capable of this syscall can do *real bad* things */
	dprintk(DBG_IOCTL, "%s: gnbd_ioctl cmd=%s(0x%x) arg=%lu\n",
			dev->disk->disk_name, ioctl_cmd_to_ascii(cmd), cmd, arg);

	switch (cmd) {
	case GNBD_DISCONNECT:
	        printk(KERN_INFO "%s: GNBD_DISCONNECT\n", dev->disk->disk_name);
		spin_lock(&dev->open_lock);
		if (dev->open_count > 0){
			spin_unlock(&dev->open_lock);
			return -EBUSY;
		}
		dev->receiver_pid = -1;
		spin_unlock(&dev->open_lock);
		/* There is no one using the device, you can disconnect it */
		if (dev->sock == NULL)
			return -ENOTCONN;
		gnbd_send_req(dev, &shutdown_req, 1);
                return 0;
	case GNBD_CLEAR_QUE:
		if (down_interruptible(&dev->do_it_lock))
			return -EBUSY;
		dev->receiver_pid = -1;
		error = gnbd_clear_que(dev);
		if (error)
			return error;
		bdev = dev->bdev;
		if (bdev) {
			blk_run_queue(dev->disk->queue);
#if 0
			fsync_bdev(bdev);
			invalidate_bdev(bdev);
#endif
		}
		up(&dev->do_it_lock);
		return 0;
	case GNBD_DO_IT:
		if (copy_from_user(&req, (do_it_req_t *)arg, sizeof(req)))
                        return -EFAULT;
                if (req.minor >= 128)
                        return -EINVAL;
                dev = &gnbd_dev[req.minor];
                BUG_ON(dev->magic != GNBD_MAGIC);
		if (dev->file)
			return -EBUSY;
		error = -EINVAL;
		file = fget(req.sock_fd);
		if (!file)
			return error;
		inode = file->f_dentry->d_inode;
		if (!S_ISSOCK(inode->i_mode)) {
			fput(file);
			return error;
		}
		if (down_trylock(&dev->do_it_lock)){
			fput(file);
			return -EBUSY;
		}
		error = gnbd_resend_requests(dev, SOCKET_I(inode));
		if (error){
			printk("quitting GNBD_DO_IT\n");
			up(&dev->do_it_lock);
			fput(file);
			return error;
		}
		dev->file = file;
		dev->sock = SOCKET_I(inode);
		dev->corrupt = 0;
		dev->receiver_pid = current->pid; 
		blk_run_queue(dev->disk->queue);
		error = gnbd_do_it(dev);
		/* should I kill the socket first */
		up(&dev->do_it_lock);
		down(&dev->tx_lock);
		if (dev->sock) {
			printk(KERN_WARNING "%s: shutting down socket\n",
					dev->disk->disk_name);
			dev->sock->ops->shutdown(dev->sock,
					SEND_SHUTDOWN|RCV_SHUTDOWN);
			dev->sock = NULL;
		}
		up(&dev->tx_lock);
		file = dev->file;
		dev->file = NULL;
		if (file)
			fput(file);
		printk("exiting GNBD_DO_IT ioctl\n");
		return error;
	case GNBD_PING:
		/* FIXME -- should I allow pings if everything is compeletely
		 * shutdown */
		spin_lock(&dev->queue_lock);
		/* only one outstanding ping at a time */
		if (list_empty(&ping_req.queuelist)){
			if (list_empty(&dev->queue_head))
				dev->last_received = jiffies;
			list_add(&ping_req.queuelist, &dev->queue_head);
		}
		spin_unlock(&dev->queue_lock);
		gnbd_send_req(dev, &ping_req, 1); /* ignore the errors */
		return 0;
	case GNBD_PRINT_DEBUG:
		printk(KERN_INFO "%s: next = %p, prev = %p, head = %p\n",
			dev->disk->disk_name,
			dev->queue_head.next, dev->queue_head.prev,
			&dev->queue_head);
		return 0;
	case GNBD_GET_TIME:
		if (copy_to_user((void *)arg, &insmod_time, sizeof(uint64_t))){
			printk(KERN_WARNING "couldn't copy time argument to user\n");
			return -EFAULT;
		}
		return 0;
	}
	/* FIXME -- should I print something, is EINVAL the right error */
	return -EINVAL;
}

#ifdef CONFIG_COMPAT
static long gnbd_ctl_compat_ioctl(struct file *f, unsigned cmd,
				  unsigned long arg)
{
	int ret;
	switch (cmd) {
        case GNBD_DISCONNECT:
        case GNBD_CLEAR_QUE:
	case GNBD_PING:
	case GNBD_PRINT_DEBUG:
		lock_kernel();
		ret = gnbd_ctl_ioctl(f->f_dentry->d_inode, f, cmd, arg);
		unlock_kernel();
		return ret;
	case GNBD_DO_IT:
	case GNBD_GET_TIME:
		lock_kernel();
		ret = gnbd_ctl_ioctl(f->f_dentry->d_inode, f, cmd,
				     (unsigned long)compat_ptr(arg));
		unlock_kernel();
		return ret;
	default:
		return -ENOIOCTLCMD;
	}
}
#endif

static int gnbd_open(struct inode *inode, struct file *file)
{
	struct gnbd_device *dev = inode->i_bdev->bd_disk->private_data;
	spin_lock(&dev->open_lock);
	if (dev->receiver_pid == -1){
		spin_unlock(&dev->open_lock);
		return -ENXIO;
	}
	spin_unlock(&dev->open_lock);
	if ((file->f_mode & FMODE_WRITE) && (dev->flags & GNBD_READ_ONLY)){
		printk(KERN_INFO "cannot open read only gnbd device read/write");
		return -EROFS;
	}

	dev->open_count++;
	dev->bdev = inode->i_bdev;
	return 0;
}

/* FIXME -- I don't sync the device at close. This means that If you write
 * something, and close the device, and expect that then it is written,
 * you are wrong.... This might cause problems */
static int gnbd_release(struct inode *inode, struct file *file)
{
	struct gnbd_device *dev = inode->i_bdev->bd_disk->private_data;

	dev->open_count--;
	if (dev->open_count == 0)
		dev->bdev = NULL;
	return 0;
}

static struct file_operations _gnbd_ctl_fops =
{
        .ioctl = gnbd_ctl_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = gnbd_ctl_compat_ioctl,
#endif
        .owner = THIS_MODULE,
};

static struct miscdevice _gnbd_misc =
{
        .minor = MISC_DYNAMIC_MINOR,
        .name  = "gnbd_ctl",
        .fops = &_gnbd_ctl_fops
};

/* FIXME -- I should probably do more here */
int __init gnbd_ctl_init(void)
{
        int err;
        
        err = misc_register(&_gnbd_misc);
        if (err) {
                printk("cannot register control device\n");
                return err;
        }
        return 0;
}

void gnbd_ctl_cleanup(void)
{
        if (misc_deregister(&_gnbd_misc) < 0)
                printk("cannot deregister control device\n");
}

static struct block_device_operations gnbd_fops =
{
	.open =		gnbd_open,
	.release =	gnbd_release,
	.owner =	THIS_MODULE,
};

/*
 * And here should be modules and kernel interface 
 *  (Just smiley confuses emacs :-)
 */

static int __init gnbd_init(void)
{
	int err = -ENOMEM;
	struct timeval tv;
	int i;

	BUILD_BUG_ON(sizeof(struct gnbd_request) != 28);

	shutdown_req.cmd_type = REQ_TYPE_SPECIAL;
	gnbd_cmd(&shutdown_req) = GNBD_CMD_DISC;
	shutdown_req.sector = 0;
	shutdown_req.nr_sectors = 0;

	ping_req.cmd_type = REQ_TYPE_SPECIAL;
	gnbd_cmd(&ping_req) = GNBD_CMD_PING;
	ping_req.sector = 0;
	ping_req.nr_sectors = 0;

	for (i = 0; i < MAX_GNBD; i++) {
		struct gendisk *disk = alloc_disk(1);
		if (!disk)
			goto out;
		gnbd_dev[i].disk = disk;
		/*
		 * The new linux 2.5 block layer implementation requires
		 * every gendisk to have its very own request_queue struct.
		 * These structs are big so we dynamically allocate them.
		 */
		disk->queue = blk_init_queue(do_gnbd_request, &gnbd_lock);
		if (!disk->queue) {
			put_disk(disk);
			goto out;
		}
		elevator_exit(disk->queue->elevator);
		err = elevator_init(disk->queue, "deadline");
		if (err) {
			blk_cleanup_queue(disk->queue);
			put_disk(disk);
			goto out;
		}
	}
	major_nr = register_blkdev(major_nr, "gnbd");
	if (major_nr < 0) {
		printk("gnbd: unable to get a major number\n");
		err = major_nr;
		goto out;
	}

	printk(KERN_INFO "gnbd: registered device at major %d\n", major_nr);
	dprintk(DBG_INIT, "gnbd: debugflags=0x%x\n", debugflags);

	err = class_register(&gnbd_class);
	if (err)
		goto out_unregister;
	for (i = 0; i < MAX_GNBD; i++) {
		struct gendisk *disk = gnbd_dev[i].disk;
		gnbd_dev[i].file = NULL;
		gnbd_dev[i].magic = GNBD_MAGIC;
		gnbd_dev[i].flags = 0;
		gnbd_dev[i].open_count = 0;
		gnbd_dev[i].receiver_pid = -1;
		gnbd_dev[i].server_name = NULL;
		gnbd_dev[i].server_port = 0;
		gnbd_dev[i].name[0] = '\0';
		gnbd_dev[i].bdev = NULL;
		spin_lock_init(&gnbd_dev[i].queue_lock);
		spin_lock_init(&gnbd_dev[i].open_lock);
		INIT_LIST_HEAD(&gnbd_dev[i].queue_head);
		init_MUTEX(&gnbd_dev[i].tx_lock);
		init_MUTEX(&gnbd_dev[i].do_it_lock);
		init_waitqueue_head(&gnbd_dev[i].tx_wait);
		gnbd_dev[i].current_request = NULL;
		gnbd_dev[i].class_dev.class = &gnbd_class;
		sprintf(gnbd_dev[i].class_dev.bus_id, "gnbd%d", i);
		err = device_register(&gnbd_dev[i].class_dev);
		if (err){
			printk("device_register failed with %d\n", err);
			goto out_unregister_class;
		}
		if(device_create_file(&gnbd_dev[i].class_dev,
					&dev_attr_pid))
			goto out_remove_file;
		if(device_create_file(&gnbd_dev[i].class_dev,
					&dev_attr_server))
			goto out_remove_file;
		if(device_create_file(&gnbd_dev[i].class_dev,
					&dev_attr_name))
			goto out_remove_file;
		if(device_create_file(&gnbd_dev[i].class_dev,
					&dev_attr_sectors))
			goto out_remove_file;
		if(device_create_file(&gnbd_dev[i].class_dev,
					&dev_attr_usage))
			goto out_remove_file;
		if(device_create_file(&gnbd_dev[i].class_dev,
					&dev_attr_flags))
			goto out_remove_file;
		if(device_create_file(&gnbd_dev[i].class_dev,
					&dev_attr_waittime))
			goto out_remove_file;
		if(device_create_file(&gnbd_dev[i].class_dev,
					&dev_attr_connected))
			goto out_remove_file;
		disk->major = major_nr;
		disk->first_minor = i;
		disk->fops = &gnbd_fops;
		disk->private_data = &gnbd_dev[i];
		sprintf(disk->disk_name, "gnbd%d", i);
		set_capacity(disk, 0);
		add_disk(disk);
		if(sysfs_create_link(&gnbd_dev[i].class_dev.kobj,
					&gnbd_dev[i].disk->dev.kobj, "block"))
			goto out_remove_disk;
		
	}

        err = gnbd_ctl_init();
        if (err)
                goto out_unregister_class;
        
	do_gettimeofday(&tv); 
	insmod_time = (uint64_t) tv.tv_sec * 1000000 + tv.tv_usec;

	return 0;
out_remove_disk:
	del_gendisk(gnbd_dev[i].disk);
out_remove_file:
	device_unregister(&gnbd_dev[i].class_dev);
out_unregister_class:
	while(i--){
		del_gendisk(gnbd_dev[i].disk);
		device_unregister(&gnbd_dev[i].class_dev);
	}
	i = MAX_GNBD;
	class_unregister(&gnbd_class);
out_unregister:
	unregister_blkdev(major_nr, "gnbd");
out:
	while (i--) {
		blk_cleanup_queue(gnbd_dev[i].disk->queue);
		put_disk(gnbd_dev[i].disk);
	}
	return err;
}

static void __exit gnbd_cleanup(void)
{
	int i;

	gnbd_ctl_cleanup();
	for (i = 0; i < MAX_GNBD; i++) {
		struct gendisk *disk = gnbd_dev[i].disk;
		device_unregister(&gnbd_dev[i].class_dev);
		if (disk) {
			del_gendisk(disk);
			blk_cleanup_queue(disk->queue);
			put_disk(disk);
		}
		if (gnbd_dev[i].server_name)
			kfree(gnbd_dev[i].server_name);
	}
	class_unregister(&gnbd_class);
	unregister_blkdev(major_nr, "gnbd");
	printk(KERN_INFO "gnbd: unregistered device at major %d\n", major_nr);
}

module_init(gnbd_init);
module_exit(gnbd_cleanup);

MODULE_DESCRIPTION("Network Block Device");
MODULE_LICENSE("GPL");

#ifndef NDEBUG
module_param(debugflags, uint, 0644);
MODULE_PARM_DESC(debugflags, "flags for controlling debug output");
#endif
