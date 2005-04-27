/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

/*
 * This is a bit of an abstraction layer to get this working in both kernel
 * and userspace.
 */
#define TRUE  (1)
#define FALSE (0)
#define MIN(a,b) ((a<b)?a:b)

#ifdef __linux__
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#define __KERNEL_SYSCALLS__
#include <linux/unistd.h>
#endif /*__linux__*/

#include "xdr.h"

/**
 * xdr_realloc - a realloc for kernel space.
 * @a: < pointer to realloc
 * @nl: < desired new size
 * @ol: < current old size
 * 
 * Not as good as the real realloc, since it always moves memory.  But good
 * enough for as little as it will get used here.
 *
 * XXX this is broken.
 * 
 * Returns: void*
 */
static void *
xdr_realloc (void *a, size_t nl, size_t ol)
{
	if (nl == ol) {
		return a;
	} else if (nl == 0) {
		kfree (a);
		return NULL;
	} else if (a == NULL && nl > 0) {
		return kmalloc (nl, GFP_KERNEL);
	} else {
		void *tmp;
		tmp = kmalloc (nl, GFP_KERNEL);
		if (tmp == NULL)
			return NULL;
		memcpy (tmp, a, MIN (nl, ol));
		kfree (a);
		return tmp;
	}
}

typedef enum { xdr_enc, xdr_dec } xdr_type;

/* encoders have this sorta non-blocking, growing buffering stunt.
 * makes them a bit different from the decoders now.
 */
struct xdr_enc_s {
	size_t default_buf_size;
	xdr_socket fd;
	xdr_type type;
	size_t length;
	size_t curloc;
	uint8_t *stream;
};

/* decoders only pull a single item off of the socket at a time.
 * so this is all they need.
 */
struct xdr_dec_s {
	size_t length;		/* total byte length of the stream */
	size_t curloc;		/* current byte offset from start */
	uint8_t *stream;	/* start of the encoded stream. */
	xdr_socket fd;
	xdr_type type;
};

/* the types of data we support. */

#define XDR_NULL          0x00	/* NOT A VALID TAG!!! used in dec code. */
#define XDR_LIST_START    0x01
#define XDR_LIST_STOP     0x02
/* list is a variable length device.  It is a start tag, some number of
 * xdr_enc_*, then an stop tag.  It's main purpose is to provide a method
 * of encasing data.
 * */
#define XDR_STRING        0x04
/* string tag is followed by a uint16 which is the byte length */
#define XDR_RAW           0x05
/* raw tag is followed by a uint16 which is the byte length
 * if 65535 bytes isn't enough, split your data and put multiples of these
 * back to back.  (idea of xdr is to avoid this twit.)
 * */

/* note, if the size of these should variate, I'm screwed.  Should consider
 * changing this all to the bit shift and array access to be more concrete.
 * later.
 */
#define XDR_UINT64        0x06
#define XDR_UINT32        0x07
#define XDR_UINT16        0x08
#define XDR_UINT8         0x09
/* should add signed ints */

#define XDR_IPv6          0x0a	/* 16 bytes, IPv6 address */

/* any other base types?
 */

#define XDR_DEFAULT_BUFFER_SIZE 4096
/*****************************************************************************/

/**
 * xdr_enc_init - 
 * @fd: 
 * @buffer_size: 
 * 
 * 
 * Returns: xdr_enc_t*
 */
xdr_enc_t *
xdr_enc_init (xdr_socket fd, int buffer_size)
{
	xdr_enc_t *xdr;

	if (buffer_size <= 0)
		buffer_size = XDR_DEFAULT_BUFFER_SIZE;

	xdr = kmalloc (sizeof (xdr_enc_t), GFP_KERNEL);
	if (xdr == NULL)
		return NULL;
	xdr->stream = kmalloc (buffer_size, GFP_KERNEL);
	if (xdr->stream == NULL) {
		kfree (xdr);
		return NULL;
	}
	xdr->fd = fd;
	xdr->type = xdr_enc;
	xdr->default_buf_size = buffer_size;
	xdr->length = buffer_size;
	xdr->curloc = 0;

	return xdr;
}

/**
 * xdr_dec_init - 
 * @fd: 
 * @buffer_size: 
 * 
 * 
 * Returns: xdr_dec_t*
 */
xdr_dec_t *
xdr_dec_init (xdr_socket fd, int buffer_size)
{
	xdr_dec_t *xdr;

	if (buffer_size <= 0)
		buffer_size = XDR_DEFAULT_BUFFER_SIZE;

	xdr = kmalloc (sizeof (xdr_dec_t), GFP_KERNEL);
	if (xdr == NULL)
		return NULL;
	xdr->length = buffer_size;
	xdr->curloc = 0;
	xdr->stream = kmalloc (buffer_size, GFP_KERNEL);
	xdr->fd = fd;
	xdr->type = xdr_dec;
	if (xdr->stream == NULL) {
		kfree (xdr);
		return NULL;
	}
	*(xdr->stream) = XDR_NULL;	/* so the first dec_call will call get_next */
	return xdr;
}

/*****************************************************************************/
/**
 * xdr_enc_flush - 
 * @xdr: 
 * 
 * Returns: int
 */
int
xdr_enc_flush (xdr_enc_t * xdr)
{
	int err;
	if (xdr == NULL)
		return -EINVAL;
	if (xdr->type != xdr_enc)
		return -EINVAL;
	if (xdr->curloc == 0)
		return 0;

	err = xdr_send (xdr->fd, xdr->stream, xdr->curloc);
	if (err < 0)
		return err;
	if (err == 0)
		return -EPROTO;	/* why? */
	xdr->curloc = 0;

	return 0;
}

/**
 * xdr_release - 
 * @xdr: 
 *
 * Free the memory, losing whatever may be there.
 */
void
xdr_dec_release (xdr_dec_t * xdr)
{
	if (xdr == NULL)
		return;
	kfree (xdr->stream);
	kfree (xdr);
}

/**
 * xdr_enc_force_release - 
 * @xdr: 
 * 
 * Free the memory, losing whatever may be there.
 */
void
xdr_enc_force_release (xdr_enc_t * xdr)
{
	if (xdr == NULL)
		return;
	if (xdr->stream != NULL)
		kfree (xdr->stream);
	kfree (xdr);
}

/**
 * xdr_enc_release - 
 * @xdr: 
 * 
 * Free things up, trying to send any possible leftover data first.
 * 
 * Returns: int
 */
int
xdr_enc_release (xdr_enc_t * xdr)
{
	int e;
	if (xdr == NULL)
		return -EINVAL;
	if ((e = xdr_enc_flush (xdr)) != 0)
		return e;
	xdr_enc_force_release (xdr);
	return 0;
}

/*****************************************************************************/
/**
 * grow_stream - 
 * @xdr: 
 * @len: 
 * 
 * each single encoded call needs to fit within a buffer.  So we make sure
 * the buffer is big enough.
 *
 * If the buffer is big enough, but just doesn't have room, we send the
 * data in the buffer, emptying it, first.
 * 
 * Returns: int
 */
static int
grow_stream (xdr_enc_t * enc, size_t len)
{
	int err;
	uint8_t *c;

	/* buffer must be big enough for one type entry. */
	if (len > enc->length) {
		c = xdr_realloc (enc->stream, len, enc->length);
		if (c == NULL)
			return -ENOMEM;
		enc->stream = c;
		enc->length = len;
	}

	/* if there isn't room on the end of this chunk,
	 * try sending what we've got.
	 */
	if (enc->curloc + len > enc->length) {
		err = xdr_enc_flush (enc);
		if (err != 0) {
			/* error, better pass this up. */
			return err;
		}
	}

	return 0;
}

/**
 * append_bytes - 
 * @xdr: 
 * @xdr_type: 
 * @bytes: 
 * @len: 
 * 
 * 
 * Returns: int
 */
static int
append_bytes (xdr_enc_t * xdr, uint8_t xdr_type, void *bytes, size_t len)
{
	int e;
	if (xdr == NULL)
		return -EINVAL;
	if (xdr->type != xdr_enc)
		return -EINVAL;

	/* len + 1; need the one byte for the type code. */
	if ((e = grow_stream (xdr, len + 1)) != 0)
		return e;
	*(xdr->stream + xdr->curloc) = xdr_type;
	xdr->curloc += 1;
	memcpy ((xdr->stream + xdr->curloc), bytes, len);
	xdr->curloc += len;

	return 0;
}

int
xdr_enc_uint64 (xdr_enc_t * xdr, uint64_t i)
{
	uint64_t b = cpu_to_be64 (i);
	return append_bytes (xdr, XDR_UINT64, &b, sizeof (uint64_t));
}

int
xdr_enc_uint32 (xdr_enc_t * xdr, uint32_t i)
{
	uint32_t b = cpu_to_be32 (i);
	return append_bytes (xdr, XDR_UINT32, &b, sizeof (uint32_t));
}

int
xdr_enc_uint16 (xdr_enc_t * xdr, uint16_t i)
{
	uint16_t b = cpu_to_be16 (i);
	return append_bytes (xdr, XDR_UINT16, &b, sizeof (uint16_t));
}

int
xdr_enc_uint8 (xdr_enc_t * xdr, uint8_t i)
{
	return append_bytes (xdr, XDR_UINT8, &i, sizeof (uint8_t));
}

int
xdr_enc_ipv6 (xdr_enc_t * xdr, struct in6_addr *ip)
{				/* bytes should already be in the right order. */
	return append_bytes (xdr, XDR_IPv6, ip->s6_addr, 16);
}

int
xdr_enc_raw (xdr_enc_t * xdr, void *p, uint16_t len)
{
	int e;
	if (xdr == NULL)
		return -EINVAL;
	if ((e = grow_stream (xdr, len + 3)) != 0)
		return e;
	*(xdr->stream + xdr->curloc) = XDR_RAW;
	xdr->curloc += 1;
   *((uint16_t*)xdr->stream + xdr->curloc) = osi_cpu_to_be16(len);
	xdr->curloc += 2;
	memcpy ((xdr->stream + xdr->curloc), p, len);
	xdr->curloc += len;
	return 0;
}

int
xdr_enc_raw_iov (xdr_enc_t * xdr, int count, struct iovec *iov)
{
	size_t total = 0;
	int i, err;
	if (xdr == NULL || count < 1 || iov == NULL)
		return -EINVAL;
	for (i = 0; i < count; i++)
		total += iov[i].iov_len;
	/* make sure it fits in a uint16_t */
	if (total > 0xffff)
		return -EFBIG;
	/* grow to fit */
	if ((err = grow_stream (xdr, total + 3)) != 0)
		return err;
	/* copy in header and size */
	*(xdr->stream + xdr->curloc) = XDR_RAW;
	xdr->curloc += 1;
   *((uint16_t*)xdr->stream + xdr->curloc) = osi_cpu_to_be16(total);
	xdr->curloc += 2;
	/* copy in all iovbufs */
	for (i = 0; i < count; i++) {
		if (iov[i].iov_base == NULL)
			continue;
		memcpy ((xdr->stream + xdr->curloc), iov[i].iov_base,
			iov[i].iov_len);
		xdr->curloc += iov[i].iov_len;
	}
	return 0;
}

int
xdr_enc_string (xdr_enc_t * xdr, uint8_t * s)
{
	int len, e;
	if (xdr == NULL)
		return -EINVAL;
	if (s == NULL)
		len = 0;
	else
		len = strlen (s);
	if ((e = grow_stream (xdr, len + 3)) != 0)
		return e;
	*(xdr->stream + xdr->curloc) = XDR_STRING;
	xdr->curloc += 1;
   *((uint16_t*)xdr->stream + xdr->curloc) = osi_cpu_to_be16(len);
	xdr->curloc += 2;
	if (len > 0) {
		memcpy ((xdr->stream + xdr->curloc), s, len);
		xdr->curloc += len;
	}
	return 0;
}

int
xdr_enc_list_start (xdr_enc_t * xdr)
{
	int e;
	if (xdr == NULL)
		return -EINVAL;
	if ((e = grow_stream (xdr, 1)) != 0)
		return e;
	*(xdr->stream + xdr->curloc) = XDR_LIST_START;
	xdr->curloc += 1;
	return 0;
}

int
xdr_enc_list_stop (xdr_enc_t * xdr)
{
	int e;
	if (xdr == NULL)
		return -EINVAL;
	if ((e = grow_stream (xdr, 1)) != 0)
		return e;
	*(xdr->stream + xdr->curloc) = XDR_LIST_STOP;
	xdr->curloc += 1;
	return 0;
}

/*****************************************************************************/

/**
 * get_next - 
 * @xdr: 
 * 
 * get what ever may be next, and put it into the buffer.
 * 
 * Returns: int
 */
static int
get_next (xdr_dec_t * xdr)
{
	int err;
	uint16_t len;
	if ((err = xdr_recv (xdr->fd, xdr->stream, 1)) < 0)
		return err;
	if (err == 0)
		return -EPROTO;
	xdr->curloc = 1;
	if (*(xdr->stream) == XDR_UINT64) {
		len = sizeof (uint64_t);
	} else if (*(xdr->stream) == XDR_UINT32) {
		len = sizeof (uint32_t);
	} else if (*(xdr->stream) == XDR_UINT16) {
		len = sizeof (uint16_t);
	} else if (*(xdr->stream) == XDR_UINT8) {
		len = sizeof (uint8_t);
	} else if (*(xdr->stream) == XDR_IPv6) {
		len = 16;
	} else if (*(xdr->stream) == XDR_STRING) {
		if ((err = xdr_recv (xdr->fd, (xdr->stream + 1), 2)) < 0)
			return err;
		if (err == 0)
			return -EPROTO;
		len = be16_to_cpu (*((uint16_t *) (xdr->stream + xdr->curloc)));
		xdr->curloc += 2;
	} else if (*(xdr->stream) == XDR_RAW) {
		if ((err = xdr_recv (xdr->fd, (xdr->stream + 1), 2)) < 0)
			return err;
		if (err == 0)
			return -EPROTO;
		len = be16_to_cpu (*((uint16_t *) (xdr->stream + xdr->curloc)));
		xdr->curloc += 2;
	} else if (*(xdr->stream) == XDR_LIST_START) {
		xdr->curloc = 0;
		return 0;
	} else if (*(xdr->stream) == XDR_LIST_STOP) {
		xdr->curloc = 0;
		return 0;
	} else {
		return -1;
	}

	/* grow buffer if need be. */
	if (xdr->curloc + len > xdr->length) {
		uint8_t *c;
		c = xdr_realloc (xdr->stream, xdr->curloc + len, xdr->length);
		if (c == NULL)
			return -ENOMEM;
		xdr->stream = c;
		xdr->length = xdr->curloc + len;
	}

	if (len > 0) {
		if ((err =
		     xdr_recv (xdr->fd, (xdr->stream + xdr->curloc), len)) < 0)
			return err;
		if (err == 0)
			return -EPROTO;
	}
	xdr->curloc = 0;
	return 0;
}

int
xdr_dec_uint64 (xdr_dec_t * xdr, uint64_t * i)
{
	int err;
	if (xdr == NULL || i == NULL)
		return -EINVAL;
	if (*(xdr->stream) == XDR_NULL) {
		if ((err = get_next (xdr)) != 0)
			return err;
	}
	if (*(xdr->stream) != XDR_UINT64)
		return -ENOMSG;
	*i = be64_to_cpu (*((uint64_t *) (xdr->stream + 1)));
	/* read the item out, mark that */
	*(xdr->stream) = XDR_NULL;
	return 0;
}

int
xdr_dec_uint32 (xdr_dec_t * xdr, uint32_t * i)
{
	int err;
	if (xdr == NULL || i == NULL)
		return -EINVAL;
	if (*(xdr->stream) == XDR_NULL) {
		if ((err = get_next (xdr)) != 0)
			return err;
	}
	if (*(xdr->stream) != XDR_UINT32)
		return -ENOMSG;
	*i = be32_to_cpu (*((uint32_t *) (xdr->stream + 1)));
	/* read the item out, mark that */
	*(xdr->stream) = XDR_NULL;
	return 0;
}

int
xdr_dec_uint16 (xdr_dec_t * xdr, uint16_t * i)
{
	int err;
	if (xdr == NULL || i == NULL)
		return -EINVAL;
	if (*(xdr->stream) == XDR_NULL) {
		if ((err = get_next (xdr)) != 0)
			return err;
	}
	if (*(xdr->stream) != XDR_UINT16)
		return -ENOMSG;
	*i = be16_to_cpu (*((uint16_t *) (xdr->stream + 1)));
	/* read the item out, mark that */
	*(xdr->stream) = XDR_NULL;
	return 0;
}

int
xdr_dec_uint8 (xdr_dec_t * xdr, uint8_t * i)
{
	int err;
	if (xdr == NULL || i == NULL)
		return -EINVAL;

	if (*(xdr->stream) == XDR_NULL) {
		if ((err = get_next (xdr)) != 0)
			return err;
	}
	if (*(xdr->stream) != XDR_UINT8)
		return -ENOMSG;
	*i = *((uint8_t *) (xdr->stream + 1));
	/* read the item out, mark that */
	*(xdr->stream) = XDR_NULL;
	return 0;
}

int
xdr_dec_ipv6 (xdr_dec_t * xdr, struct in6_addr *ip)
{
	int err;
	if (xdr == NULL || ip == NULL)
		return -EINVAL;
	if (*(xdr->stream) == XDR_NULL) {
		if ((err = get_next (xdr)) != 0)
			return err;
	}
	if (*(xdr->stream) != XDR_IPv6)
		return -ENOMSG;
	memcpy (ip, xdr->stream + 1, 16);
	/* read the item out, mark that */
	*(xdr->stream) = XDR_NULL;
	return 0;
}

/* mallocing version */
int
xdr_dec_raw_m (xdr_dec_t * xdr, void **p, uint16_t * l)
{
	int len;
	void *str;
	int err;

	if (xdr == NULL || p == NULL || l == NULL)
		return -EINVAL;
	if (*(xdr->stream) == XDR_NULL) {
		if ((err = get_next (xdr)) != 0)
			return err;
	}
	if (*(xdr->stream) != XDR_RAW)
		return -ENOMSG;
	xdr->curloc = 1;

	len = be16_to_cpu (*((uint16_t *) (xdr->stream + xdr->curloc)));
	xdr->curloc += 2;

	str = kmalloc (len, GFP_KERNEL);
	if (str == NULL)
		return -ENOMEM;
	memcpy (str, (xdr->stream + xdr->curloc), len);
	xdr->curloc += len;

	*p = str;
	*l = len;
	/* read the item out, mark that */
	*(xdr->stream) = XDR_NULL;
	return 0;
}

/* non-mallocing version */
int
xdr_dec_raw (xdr_dec_t * xdr, void *p, uint16_t * l)
{
	int len;
	int err;

	if (xdr == NULL || p == NULL || l == NULL)
		return -EINVAL;
	if (*(xdr->stream) == XDR_NULL) {
		if ((err = get_next (xdr)) != 0)
			return err;
	}
	if (*(xdr->stream) != XDR_RAW)
		return -ENOMSG;
	xdr->curloc = 1;

	len = be16_to_cpu (*((uint16_t *) (xdr->stream + xdr->curloc)));
	xdr->curloc += 2;

	if (len > *l)
		return -1;

	memcpy (p, (xdr->stream + xdr->curloc), len);
	xdr->curloc += len;

	*l = len;

	/* read the item out, mark that */
	*(xdr->stream) = XDR_NULL;
	return 0;
}

/**
 * xdr_dec_raw_ag - auto-growing version
 * @xdr: 
 * @p: <> pointer to buffer
 * @bl: <> size of the buffer
 * @rl: > size of data read from stream
 * 
 * This form of xdr_dec_raw will increase the size of a pre-malloced buffer
 * to fit the data it is reading.  It is kind of a merger of the
 * non-mallocing and mallocing versions.
 * 
 * Returns: int
 */
int
xdr_dec_raw_ag (xdr_dec_t * xdr, void **p, uint16_t * bl, uint16_t * rl)
{
	int len;
	int err;

	if (xdr == NULL || p == NULL || bl == NULL || rl == NULL)
		return -EINVAL;
	if (*(xdr->stream) == XDR_NULL) {
		if ((err = get_next (xdr)) != 0)
			return err;
	}
	if (*(xdr->stream) != XDR_RAW)
		return -ENOMSG;
	xdr->curloc = 1;

	len = be16_to_cpu (*((uint16_t *) (xdr->stream + xdr->curloc)));
	xdr->curloc += 2;

	if (len > *bl) {	/* grow p */
		void *temp;
		temp = xdr_realloc (*p, len, *bl);
		if (temp == NULL)
			return -ENOMEM;
		*bl = len;
		*p = temp;
	}

	memcpy (*p, (xdr->stream + xdr->curloc), len);
	xdr->curloc += len;

	*rl = len;

	*(xdr->stream) = XDR_NULL;
	return 0;
}

/* mallocing version */
int
xdr_dec_string (xdr_dec_t * xdr, uint8_t ** strp)
{
	int len;
	char *str;
	int err;
	if (xdr == NULL || strp == NULL)
		return -EINVAL;
	if (*(xdr->stream) == XDR_NULL) {
		if ((err = get_next (xdr)) != 0)
			return err;
	}
	if (*(xdr->stream) != XDR_STRING)
		return -ENOMSG;
	xdr->curloc = 1;

	len = be16_to_cpu (*((uint16_t *) (xdr->stream + xdr->curloc)));
	xdr->curloc += 2;

	if (len > 0) {
		str = kmalloc (len + 1, GFP_KERNEL);
		if (str == NULL)
			return -ENOMEM;
		str[len] = '\0';
		memcpy (str, (xdr->stream + xdr->curloc), len);
		xdr->curloc += len;

		*strp = str;
	} else {
		*strp = NULL;
	}

	/* read the item out, mark that */
	*(xdr->stream) = XDR_NULL;
	return 0;
}

/* non-mallocing version */
int
xdr_dec_string_nm (xdr_dec_t * xdr, uint8_t * string, size_t l)
{
	int len;
	int err;
	if (xdr == NULL || string == NULL)
		return -EINVAL;
	if (*(xdr->stream) == XDR_NULL) {
		if ((err = get_next (xdr)) != 0)
			return err;
	}
	if (*(xdr->stream) != XDR_STRING)
		return -ENOMSG;
	xdr->curloc = 1;

	len = be16_to_cpu (*((uint16_t *) (xdr->stream + xdr->curloc)));
	xdr->curloc += 2;

	if (len > 0) {
		memcpy (string, (xdr->stream + xdr->curloc), MIN (len, l));
		if (l > len) {
			string[len] = '\0';
		}
		string[l - 1] = '\0';
	} else {
		string[0] = '\0';
	}

	/* read the item out, mark that */
	*(xdr->stream) = XDR_NULL;
	return 0;
}

int
xdr_dec_string_ag (xdr_dec_t * xdr, uint8_t ** s, uint16_t * bl)
{
	int len;
	int err;
	if (xdr == NULL || s == NULL || bl == NULL)
		return -EINVAL;
	if (*(xdr->stream) == XDR_NULL) {
		if ((err = get_next (xdr)) != 0)
			return err;
	}
	if (*(xdr->stream) != XDR_STRING)
		return -ENOMSG;
	xdr->curloc = 1;

	len = be16_to_cpu (*((uint16_t *) (xdr->stream + xdr->curloc)));
	xdr->curloc += 2;

	if (len == 0) {		/* empty string */
		**s = '\0';
		*(xdr->stream) = XDR_NULL;
		return 0;
	}

	if (len >= *bl) {	/* grow s */
		void *temp;
		temp = xdr_realloc (*s, len + 1, *bl);
		if (temp == NULL)
			return -ENOMEM;
		*bl = len + 1;
		*s = temp;
	}

	memcpy (*s, (xdr->stream + xdr->curloc), len);
	(*s)[len] = '\0';

	*(xdr->stream) = XDR_NULL;
	return 0;
}

int
xdr_dec_list_start (xdr_dec_t * xdr)
{
	int err;
	if (xdr == NULL)
		return -EINVAL;
	if (*(xdr->stream) == XDR_NULL) {
		if ((err = get_next (xdr)) != 0)
			return err;
	}
	if (*(xdr->stream) != XDR_LIST_START)
		return -ENOMSG;
	/* read the item out, mark that */
	*(xdr->stream) = XDR_NULL;
	return 0;
}

int
xdr_dec_list_stop (xdr_dec_t * xdr)
{
	int err;
	if (xdr == NULL)
		return -EINVAL;
	if (*(xdr->stream) == XDR_NULL) {
		if ((err = get_next (xdr)) != 0)
			return err;
	}
	if (*(xdr->stream) != XDR_LIST_STOP)
		return -ENOMSG;
	/* read the item out, mark that */
	*(xdr->stream) = XDR_NULL;
	return 0;
}
