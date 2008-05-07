#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>


#ifdef STANDALONE
#define dbg_printf(x, fmt, args...) printf("<%d> " fmt, x, ##args)
#else
#include "debug.h"
#endif

xmlNodePtr
get_os_node(xmlDocPtr doc)
{
	xmlNodePtr node;

	/* Flip the property of the graphics port if it exists */
	node = xmlDocGetRootElement(doc);
	node = node->children;

	while (node) {
		if (!xmlStrcmp(node->name, (xmlChar *)"os"))
			break;
		node = node->next;
	}

	return node;
}


int
flip_graphics_port(xmlDocPtr doc)
{
	xmlNodePtr node, curr;

	/* Flip the property of the graphics port if it exists */
	node = xmlDocGetRootElement(doc);
	node = node->children;

	while (node) {
		if (!xmlStrcmp(node->name, (xmlChar *)"devices"))
			break;
		node = node->next;
	}

	node = node->children;
	curr = node;
	while (curr) {
		if (!xmlStrcmp(curr->name, (xmlChar *)"graphics"))
			break;
		curr = curr->next;
	}

	if (xmlGetProp(curr, (xmlChar *)"port")) {
		dbg_printf(5,"Zapping the graphics port\n");
		xmlSetProp(curr, (xmlChar *)"port", (xmlChar *)"-1");
	}

	return 0;
}


int
cleanup_xml_doc(xmlDocPtr doc)
{
	xmlNodePtr os_node, curr;
	int type = 0;
	char *val;

	curr = xmlDocGetRootElement(doc);
	if (xmlStrcmp(curr->name, (xmlChar *)"domain")) {
		dbg_printf(1, "Invalid XML\n");
		return -1;
	}

	flip_graphics_port(doc);

	os_node = get_os_node(doc);

	curr = os_node->children;
	while (curr) {
		if (!xmlStrcmp(curr->name, (xmlChar *)"type"))
			break;
		curr = curr->next;
	}
	if (!curr) {
		dbg_printf(1, "Unable to determine the domain type\n");
		return -1;
	}

	val = (char *)xmlNodeGetContent(curr);
	while (isspace(*val)) val++;

	if (!strcasecmp(val, "hvm")) {
		type = 1;
		dbg_printf(2, "Virtual machine is HVM\n");
	} else if (!strcasecmp(val, "linux")) {
		type = 2;
		dbg_printf(2, "Virtual machine is Linux\n");
	}

	/* Node is still pointing to the <os> block */
	if (type == 2) {
		dbg_printf(3, "Unlinkiking %s block\n", (char *)os_node->name);
		xmlUnlinkNode(os_node);
		xmlFreeNode(os_node);
	}

	return 0;
}


int
xtree_readfile(const char *filename, xmlDocPtr *xtreep)
{
	xmlNodePtr cur;

	xmlKeepBlanksDefault(0);
	xmlIndentTreeOutput = 1;

	*xtreep = xmlParseFile(filename);

	if (!*xtreep)
		return -1;

	if (!((cur = xmlDocGetRootElement(*xtreep)))) {
		xmlFreeDoc(*xtreep);
		*xtreep = NULL;
		return -1;
	}

	return 0;
}


int
xtree_readbuffer(const char *buffer, size_t size, xmlDocPtr *xtreep)
{
	xmlNodePtr cur;

	xmlKeepBlanksDefault(0);
	xmlIndentTreeOutput = 1;

	*xtreep = xmlParseMemory(buffer, size);

	if (!*xtreep) {
		dbg_printf(1, "parse failure %p %d\n", buffer, (int)size);
		return -1;
	}

	if (!((cur = xmlDocGetRootElement(*xtreep)))) {
		dbg_printf(1, "root element failure\n");
		xmlFreeDoc(*xtreep);
		*xtreep = NULL;
		return -1;
	}

	return 0;
}


int
xtree_writefile(const char *filename, xmlDocPtr xtree)
{
	char tmpfn[1024];
	int fd, tmpfd;
	xmlChar *buffer;
	struct flock flock;
	int n, remain, written, size = 0;

	snprintf(tmpfn, sizeof(tmpfn), "%s.XXXXXX", filename);
	tmpfd = mkstemp(tmpfn);
	if (tmpfd == -1)
		return -1;

	memset(&flock, 0, sizeof(flock));
	flock.l_type = F_WRLCK;

	fd = open(filename, O_WRONLY | O_CREAT | O_SYNC, S_IRUSR | S_IWUSR );
	if (fd == -1) {
		n = errno;
		close(tmpfd);
		unlink(tmpfn);
		errno = n;
		return -1;
	}

	while (fcntl(fd, F_SETLKW, &flock) == -1) {
		if (errno == EINTR)
			continue;
		n = errno;
		close(fd);
		close(tmpfd);
		unlink(tmpfn);
		errno = n;
		return -1;
	}

	xmlDocDumpFormatMemory(xtree, (xmlChar **)&buffer, (int *)&size, 1);

	written = 0;
	remain = size;
	while (remain) {
		n = write(tmpfd, buffer + written, remain);

		if (n == -1) {
			if (errno == EINTR)
				continue;
				
			free(buffer);
			n = errno;
			close(fd);
			close(tmpfd);
			unlink(tmpfn);
			errno = n;
			return -1;
		}
			
		written += n;
		remain -= n;
	}

	xmlFree(buffer);
	if (rename(tmpfn, filename) == -1) {
		n = errno;
		close(fd);
		close(tmpfd);
		unlink(tmpfn);
		errno = n;
		return -1;
	}

	close(fd);
	fsync(tmpfd);
	close(tmpfd);
	return 0;
}


int
xtree_writebuffer(xmlDocPtr xtree, char **buffer, size_t *size)
{
	*size = 0;
	xmlDocDumpFormatMemory(xtree, (xmlChar **)buffer, (int *)size, 1);
	return 0;
}


int
cleanup_xml(char *desc, char **ret, size_t *retsz)
{
	xmlDocPtr xtree;
	int rv;

	*ret = NULL;
	if (xtree_readbuffer(desc, strlen(desc), &xtree) < 0) {
		xmlCleanupParser();
		return -1;
	}

	rv = cleanup_xml_doc(xtree);	
	if (xtree_writebuffer(xtree, ret, retsz) < 0)
		rv = -1;

	if (*ret && rv < 0)
		free(*ret);
	xmlFreeDoc(xtree);
	xmlCleanupParser();
	return rv;
}


#ifdef STANDALONE
int
main(int argc, char **argv)
{
	char *file = NULL;
	char *buf;
	size_t sz;
	int opt;
	xmlDocPtr xtree;

	while ((opt = getopt(argc, argv, "f:")) != EOF) {
		switch(opt) {
		case 'f':
			file = optarg;
			break;
		}
	}

	if (!file) {
		printf("No file specified\n");
		return 1;
	}

	if (xtree_readfile(file, &xtree) < 0)
		return -1;

	cleanup_xml_doc(xtree);

	xtree_writebuffer(xtree, &buf, &sz);
	write(1, buf, sz);

	return 0;
}
#endif
