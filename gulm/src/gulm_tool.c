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
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/ioctl.h>

#include "gio_wiretypes.h"
#include "gulm_defines.h"
#include "xdr.h"
#include "utils_ip.h"
#include "utils_tostr.h"
#include "utils_verb_flags.h"

char *prog_name;

const char *usage[] =
{
"Print tool version information\n",
"  gulm_tool version\n",
"\n",
"Commands that can be sent to any server:\n",
" Change verbose flags\n",
"   gulm_tool setverb <server:port> <flags>\n",
"\n",
" Get server statistics\n",
"   gulm_tool getstats <server:port>\n",
"\n",
" Unparsed server statistics (useful if you know the code)\n",
"   gulm_tool rawstats <server:port>\n",
"\n",
"Commands that can only be sent to gulm_core:\n",
" Stop the server and its resources:\n",
"   gulm_tool shutdown <server>:core\n",
"\n",
" Get the current nodelist\n",
"   gulm_tool nodelist <server>:core\n",
"\n",
" Get a single node\n",
"   gulm_tool nodeinfo <server>:core <node name>\n",
"\n",
" Get list of services connected to this node\n",
"   gulm_tool servicelist <server>:core\n",
"\n",
" Force a node into the Expired state, causing it to be fenced:\n",
"   gulm_tool forceexpire <server>:core <node name>\n",
"\n",
" Switch the core on a node into the pending state.\n",
"   gulm_tool switchPending <server>:core\n",
"\n",
#if DEBUG
"Commands for debugging things:\n",
" Rerun wait queues for all locks.\n",
"   gulm_tool rerunqueues <server>:lt000\n",
"\n",
" List the slaves connected to a JID or LT Master.\n",
"   gulm_tool slavelist <server>:lt000\n",
"\n",
#endif
""
};
void print_usage(void)
{
   int i;
   for(i=0; usage[i][0]; i++)
      printf(usage[i]);
   exit(0);
}

#define cpl_core (0x1)
#define cpl_lt   (0x4)
#define cpl_ltpx (0x8)
#define cpl_all  (cpl_core|cpl_lt|cpl_ltpx)
/**
 * check_ports - 
 * @req: 
 * @allows: 
 * 
 * 
 * Returns: int
 */
int check_ports(char *req, int allows)
{
   int port;
   if( sscanf(req, "%u", &port) == 1 ) return TRUE; /* numbers are valid */
   if( (allows & cpl_core) && strncasecmp(req, "core", 4) == 0 ) return TRUE;
   if( (allows & cpl_ltpx) && strncasecmp(req, "ltpx", 4) == 0 ) return TRUE;
   if( (allows & cpl_lt) && strncasecmp(req, "lt", 2) == 0 ) return TRUE;
   return FALSE;
}

/**
 * connect_to_server - 
 * @where: 
 * @sport: 
 * @enc: 
 * @dec: 
 * 
 * Returns: int
 */
int connect_to_server(char *where, char *sport, int allows, 
      xdr_enc_t **enc, xdr_dec_t **dec)
{
   char *n=NULL, *p=NULL;
   struct sockaddr_in6 adr;
   ip_name_t *in;
   int sk;

   /* pull apart server:port */
   adr.sin6_family = AF_INET6;
   adr.sin6_port = htons(40040);
   n = strdup(where);
   if( n == NULL ) die(ExitGulm_NoMemory, "Out Of Memory.\n");
   p = strstr(n, ":");
   if( p != NULL ) {
      *p++ = '\0';
   }else if( p == NULL ) {
      p = sport;
   }
   if( p != NULL ) {
      if( !check_ports(p, allows) ) {
         return -2;
      }
      if( strncasecmp(p, "core", 5) == 0) {
         adr.sin6_port = htons(40040);
      }else
      if( strncasecmp(p, "ltpx", 5) == 0) {
         adr.sin6_port = htons(40042);
      }else
      if( strncasecmp(p, "LT", 2) == 0) {
         int offset = 0;
         if( strlen(p) > 2 ) offset = atoi( p + 2 );
         adr.sin6_port = htons( 41040 + offset );
      }else
      {
         adr.sin6_port = htons(atoi(p));
      }
   }
   if( strlen(n) <= 0 ) {
      die(ExitGulm_BadOption, "No server name given in \"%s\"\n", where);
   }
   if( (in=get_ipname(n)) == NULL ) {
      die(ExitGulm_ParseFail, "Failed to lookup ip_name for \"%s\"\n", n);
   }
   if( in->name == NULL ){
      die(ExitGulm_ParseFail, "Failed to lookup name for \"%s\"\n", n);
   }
   if(IN6_IS_ADDR_UNSPECIFIED(in->ip.s6_addr32)) {
      die(ExitGulm_ParseFail, "Failed to lookup ip for \"%s\"\n", n);
   }
   free(n);
   memcpy(&adr.sin6_addr, &in->ip, sizeof(struct in6_addr));
   free(in);

   if((sk = socket(AF_INET6, SOCK_STREAM, 0)) <0) {
      fprintf(stderr, "Failed to create socket: %s\n", strerror(errno));
      return -1;
   }
   if( connect(sk, (struct sockaddr*)&adr, sizeof(struct sockaddr_in6))<0) {
      fprintf(stderr, "Failed to connect to %s (%s %d) %s\n",
            where, ip6tostr(&adr.sin6_addr), ntohs(adr.sin6_port),
            strerror(errno));
      return -1;
   }

   *enc = xdr_enc_init(sk,0);
   if( *enc == NULL ) {
      die(ExitGulm_NoMemory, "Couldn't set up xdr encoder\n");
   }
   *dec = xdr_dec_init(sk,0);
   if( *dec == NULL ) {
      die(ExitGulm_NoMemory, "Couldn't set up xdr decoder\n");
   }

   return sk;
}

/**
 * do_set_verb - 
 * @argc: 
 * @argv: 
 * 
 * 
 * Returns: int
 */
int do_set_verb(int argc, char **argv)
{
   int sk;
   xdr_enc_t *enc=NULL;
   xdr_dec_t *dec=NULL;

   if( argc != 4 ) die(ExitGulm_BadOption, "Wrong number of arguments.\n");

   if((sk = connect_to_server(argv[2], NULL, cpl_all, &enc, &dec)) < 0 ) {
      die(ExitGulm_InitFailed, "Failed to connect to server\n");
   }

   xdr_enc_uint32(enc, gulm_info_set_verbosity);
   xdr_enc_string(enc, argv[3]);
   xdr_enc_flush(enc);

   xdr_enc_release(enc);
   xdr_dec_release(dec);
   close(sk);
   return 0;
}

/**
 * dump_mbr_list - 
 * @dec: 
 * 
 * 
 * Returns: int
 */
int dump_mbr_list(xdr_dec_t *dec)
{
   int err;
   uint32_t x_code, x_int;
   uint8_t *x_name, x_st;
   uint64_t x_time;
   struct in6_addr x_ip;

   if((err=xdr_dec_uint32(dec, &x_code)) != 0 ) return err;
   if( x_code != gulm_core_mbr_lstrpl ) return -EINVAL;
   if((err=xdr_dec_list_start(dec)) != 0 ) return err;
   while( xdr_dec_list_stop(dec) != 0 ) {
      if((err=xdr_dec_string(dec, &x_name)) != 0 ) return err;
      printf(" Name: %s\n", x_name);
      free(x_name);

      if((err=xdr_dec_ipv6(dec, &x_ip)) != 0 ) return err;
      printf("  ip    = %s\n", ip6tostr(&x_ip));
      if((err=xdr_dec_uint8(dec, &x_st)) != 0 ) return err;
      printf("  state = %s\n", gio_mbrupdate_to_str(x_st));
      if((err=xdr_dec_uint8(dec, &x_st)) != 0 ) return err;
      printf("  last state = %s\n", gio_mbrupdate_to_str(x_st));
      if((err=xdr_dec_uint8(dec, &x_st)) != 0 ) return err;
      printf("  mode = %s\n", gio_I_am_to_str(x_st));
      if((err=xdr_dec_uint32(dec, &x_int)) != 0 ) return err;
      printf("  missed beats = %d\n", x_int);
      if((err=xdr_dec_uint64(dec, &x_time)) != 0 ) return err;
      printf("  last beat = %"PRIu64"\n", x_time);
      if((err=xdr_dec_uint64(dec, &x_time)) != 0 ) return err;
      printf("  delay avg = %"PRIu64"\n", x_time);
      if((err=xdr_dec_uint64(dec, &x_time)) != 0 ) return err;
      printf("  max delay = %"PRIu64"\n", x_time);

      printf("\n");
   }
   return 0;
}

/**
 * do_get_one_mbr - 
 * @name: 
 * @enc: 
 * @dec: 
 * 
 * 
 * Returns: int
 */
int do_get_one_mbr(int argc, char **argv)
{
   int sk, err;
   xdr_enc_t *enc=NULL;
   xdr_dec_t *dec=NULL;

   if( argc != 4 ) die(ExitGulm_BadOption, "Wrong number of arguments.\n");
   if((sk = connect_to_server(argv[2], "core", cpl_core, &enc, &dec)) < 0 ) {
      if( sk == -2 ) fprintf(stderr, "nodeinfo only works to core\n");
      die(ExitGulm_InitFailed, "Failed to connect to server\n");
   }

   xdr_enc_uint32(enc, gulm_core_mbr_req);
   xdr_enc_string(enc, argv[3]);
   xdr_enc_flush(enc);

   if( (err = dump_mbr_list(dec)) != 0 ) 
      die(ExitGulm_BadLogic, "Error %d while reading reply.\n", err);

   xdr_enc_uint32(enc, gulm_socket_close);
   xdr_enc_flush(enc);

   xdr_enc_release(enc);
   xdr_dec_release(dec);
   close(sk);
   return err;
}
/**
 * do_get_mbr_list - 
 * @enc: 
 * @dec: 
 * 
 * 
 * Returns: int
 */
int do_get_mbr_list(int argc, char **argv)
{
   int sk, err;
   xdr_enc_t *enc=NULL;
   xdr_dec_t *dec=NULL;

   if( argc != 3 ) die(ExitGulm_BadOption, "Wrong number of arguments.\n");
   if((sk = connect_to_server(argv[2], "core", cpl_core, &enc, &dec)) < 0 ) {
      if( sk == -2 ) fprintf(stderr, "nodelist only works to core.\n");
      die(ExitGulm_InitFailed, "Failed to connect to server\n");
   }

   xdr_enc_uint32(enc, gulm_core_mbr_lstreq);
   xdr_enc_flush(enc);

   if( (err = dump_mbr_list(dec)) != 0 ) 
      die(ExitGulm_BadLogic, "Error %d while reading reply.\n", err);

   xdr_enc_uint32(enc, gulm_socket_close);
   xdr_enc_flush(enc);

   xdr_enc_release(enc);
   xdr_dec_release(dec);
   close(sk);
   return err;
}

/**
 * do_shutdown_core - 
 * @argc: 
 * @argv: 
 * 
 * 
 * Returns: int
 */
int do_shutdown_core(int argc, char **argv)
{
   int sk;
   xdr_enc_t *enc=NULL;
   xdr_dec_t *dec=NULL;
   uint32_t code, error;

   if( argc != 3 ) die(ExitGulm_BadOption, "Wrong number of arguments.\n");
   if((sk = connect_to_server(argv[2], "core", cpl_core, &enc, &dec)) < 0 ) {
      if( sk == -2 ) fprintf(stderr, "shutdown only works to core\n");
      die(ExitGulm_InitFailed, "Failed to connect to server\n");
   }

   xdr_enc_uint32(enc, gulm_core_shutdown);
   xdr_enc_flush(enc);

   /* get reply */
   xdr_dec_uint32(dec, &code);
   xdr_dec_uint32(dec, &code);
   xdr_dec_uint32(dec, &error);

   /* tell server we're done. */
   xdr_enc_uint32(enc, gulm_socket_close);
   xdr_enc_flush(enc);

   if( error != 0 ) {
      printf("Cannot shutdown %s. Maybe try unmounting gfs?\n", argv[2]);
      exit(1);
   }

   xdr_enc_release(enc);
   xdr_dec_release(dec);
   close(sk);
   return 0;
}

/**
 * do_switchtopending - 
 * @argc: 
 * @argv: 
 * 
 * 
 * Returns: int
 */
int do_switchtopending(int argc, char **argv)
{
   int sk;
   xdr_enc_t *enc=NULL;
   xdr_dec_t *dec=NULL;

   if( argc != 3 ) die(ExitGulm_BadOption, "Wrong number of arguments.\n");
   if((sk = connect_to_server(argv[2], "core", cpl_core, &enc, &dec)) < 0 ) {
      if( sk == -2 ) fprintf(stderr, "switchPending only works to core\n");
      die(ExitGulm_InitFailed, "Failed to connect to server\n");
   }

   xdr_enc_uint32(enc, gulm_core_forcepend);
   xdr_enc_flush(enc);

   /* tell server we're done. */
   xdr_enc_uint32(enc, gulm_socket_close);
   xdr_enc_flush(enc);

   xdr_enc_release(enc);
   xdr_dec_release(dec);
   close(sk);
   return 0;
}

/**
 * do_force_node_state - 
 * @argc: 
 * @argv: 
 *  
 *  gulm_tool forceExpire <server> <node>
 * 
 * Returns: int
 */
int do_force_node_state(int argc, char **argv)
{
   int sk;
   xdr_enc_t *enc=NULL;
   xdr_dec_t *dec=NULL;
   uint32_t code, error;

   if( argc != 4 ) die(ExitGulm_BadOption, "Wrong number of arguments.\n");
   if((sk = connect_to_server(argv[2], "core", cpl_core, &enc, &dec)) < 0 ) {
      if( sk == -2 ) fprintf(stderr, "forceExpire only works to core\n");
      die(ExitGulm_InitFailed, "Failed to connect to server\n");
   }

   /* send force state req */
   xdr_enc_uint32(enc, gulm_core_mbr_force);
   xdr_enc_string(enc, argv[3] );
   xdr_enc_flush(enc);

   /* get reply */
   xdr_dec_uint32(dec, &code);
   xdr_dec_uint32(dec, &code);
   xdr_dec_uint32(dec, &error);

   /* tell server we're done. */
   xdr_enc_uint32(enc, gulm_socket_close);
   xdr_enc_flush(enc);

   if( error != gio_Err_Ok )
      die(ExitGulm_BadLogic, "Got Error: %d:%s\n", error, gio_Err_to_str(error));

   xdr_enc_release(enc);
   xdr_dec_release(dec);
   close(sk);
   return 0;
}

/**
 * do_raw_stats - 
 * 
 * 
 * Returns: int
 */
int do_raw_stats(int argc, char **argv)
{
   int sk;
   xdr_enc_t *enc=NULL;
   xdr_dec_t *dec=NULL;
   uint32_t x_code = -1;
   uint8_t *x_key = NULL, *x_value = NULL;

   if( argc != 3 ) die(ExitGulm_BadOption, "Wrong number of arguments.\n");
   if((sk=connect_to_server(argv[2], NULL, cpl_all, &enc, &dec)) < 0 )
      die(ExitGulm_InitFailed, "Failed to connect to server\n");

   if(xdr_enc_uint32(enc, gulm_info_stats_req) != 0) return -1;
   if(xdr_enc_flush(enc) != 0) return -1;

   if(xdr_dec_uint32(dec, &x_code) != 0) return -1;
   if(xdr_dec_list_start(dec) != 0) return -1;

   while( xdr_dec_list_stop(dec) != 0 ) {
      if(xdr_dec_string(dec, &x_key) != 0) return -1;
      if(xdr_dec_string(dec, &x_value) != 0) return -1;

      printf("%s = %s\n", x_key, x_value);

      free(x_key); x_key = NULL;
      free(x_value); x_value = NULL;
   }

   xdr_enc_uint32(enc, gulm_socket_close);
   xdr_enc_flush(enc);

   xdr_enc_release(enc);
   xdr_dec_release(dec);
   close(sk);
   return 0;
}

/**
 * statsfilter_client - 
 * @len: 
 * @keys: 
 * @values: 
 *
 *  If I_am == Slave and rank == -1; change I_am to Client/
 * 
 * Returns: void
 */
void statsfilter_client(int *len, uint8_t **keys, uint8_t **values)
{
   int i, iamat=-1, fiddle=FALSE;
   int mat=-1, qut=FALSE;

   for(i=0; i < *len; i++ ) {
      if( strcmp("I_am", keys[i]) == 0 ) {
         iamat = i;
      }else
      if( strcmp("rank", keys[i]) == 0 ) {
         if( strcmp("-1", values[i]) == 0 ) fiddle = TRUE;
      }else
      if( strcmp("Master", keys[i]) == 0 ) {
         mat = i;
      }else
      if( strcmp("quorate", keys[i]) == 0 ) {
         if( strcmp("false", values[i]) == 0) qut = TRUE;
      }
   }

   if( fiddle && iamat > -1 ) {
      free(values[iamat]);
      values[iamat] = strdup("Client");
   }
   if( qut && mat > -1 ) {
      free(keys[mat]);
      keys[mat] = strdup("Arbitrator");
   }
}

/**
 * plain_print - 
 * @len: 
 * @keys: 
 * @values: 
 * 
 */
void plain_print(int len, uint8_t **keys, uint8_t **values)
{
   int i;
   for(i=0; i < len; i++ ) {
      printf("%s = %s\n", keys[i], values[i]);
   }
}

/**
 * xml_print - 
 * @len: 
 * @keys: 
 * @values: 
 * 
 * 
 * Returns: void
 */
void xml_print(int len, uint8_t **keys, uint8_t **values)
{
   int i;
   printf("<resource>\n");
   for(i=0; i < len; i++ ) {
      printf(" <property name=\"%s\">%s</property>\n", keys[i], values[i]);
   }
   printf("</resource>\n");
}

/**
 * do_get_stats - 
 * 
 * Returns: int
 */
int do_get_keys(int argc, char **argv, uint32_t request,
      void (*filter)(int *, uint8_t **, uint8_t **),
      void (*print)(int, uint8_t **, uint8_t **))
{
   int sk;
   xdr_enc_t *enc=NULL;
   xdr_dec_t *dec=NULL;
   uint32_t x_code = -1;
   uint8_t **keys=NULL, **values=NULL;
   int length=0, size=0, i;

   if( argc != 3 ) die(ExitGulm_BadOption, "Wrong number of arguments.\n");
   if((sk=connect_to_server(argv[2], NULL, cpl_all, &enc, &dec)) < 0 )
      die(ExitGulm_InitFailed, "Failed to connect to server\n");

   if(xdr_enc_uint32(enc, request) != 0) return -1;
   if(xdr_enc_flush(enc) != 0) return -1;

   if(xdr_dec_uint32(dec, &x_code) != 0) return -1;
   if(xdr_dec_list_start(dec) != 0) return -1;

   /* load status key/values */
   while( xdr_dec_list_stop(dec) != 0 ) {

      if( length >= size ) {
         /* grow */
         uint8_t **A, **B;
         A = realloc(keys, (size + 10) * sizeof(uint8_t*) );
         if( A == NULL ) die(ExitGulm_NoMemory, "Out Of Memory.\n");
         B = realloc(values, (size + 10) * sizeof(uint8_t*) );
         if( B == NULL ) die(ExitGulm_NoMemory, "Out Of Memory.\n");

         keys = A;
         values = B;
         size += 10;
      }

      if(xdr_dec_string(dec, &keys[length]) != 0) return -1;
      if(xdr_dec_string(dec, &values[length]) != 0) return -1;

      length++;

   }

   xdr_enc_uint32(enc, gulm_socket_close);
   xdr_enc_flush(enc);

   xdr_enc_release(enc);
   xdr_dec_release(dec);
   close(sk);

   /* process them */
   if( filter != NULL ) filter(&length, keys, values);

   /* print */
   print(length, keys, values);

   /* free */
   for(i=0; i < length; i++ ) {
      free(keys[i]);
      free(values[i]);
   }
   free(keys);
   free(values);

   return 0;
}

/**
 * do_service_list - 
 * @argc: 
 * @argv: 
 * 
 * 
 * Returns: int
 */
int do_service_list(int argc, char **argv)
{
   int sk;
   uint32_t x_int;
   uint8_t x_name[1024]="";
   xdr_enc_t *enc=NULL;
   xdr_dec_t *dec=NULL;

   if( argc != 3 ) die(ExitGulm_BadOption, "Wrong number of arguments.\n");
   if((sk=connect_to_server(argv[2], "core", cpl_core, &enc, &dec)) < 0 ) {
      if( sk == -2 ) fprintf(stderr, "servicelist only works to core\n");
      die(ExitGulm_InitFailed, "Failed to connect to server\n");
   }

   if(xdr_enc_uint32(enc, gulm_core_res_req)!=0) return -1;
   if(xdr_enc_flush(enc)!=0) return -1;

   if(xdr_dec_uint32(dec, &x_int)!=0) return -1;
   if( x_int !=  gulm_core_res_list)
      die(ExitGulm_BadLogic, "Got strange replay %#x\n", x_int);

   if(xdr_dec_list_start(dec)!=0) return -1;
   while( xdr_dec_list_stop(dec) != 0 ) {
      if(xdr_dec_string_nm(dec, x_name, 1024)!=0) return -1;

      printf("%s\n", x_name);
   }

   xdr_enc_uint32(enc, gulm_socket_close);
   xdr_enc_flush(enc);

   xdr_enc_release(enc);
   xdr_dec_release(dec);
   close(sk);
   return 0;
}

/**
 * do_slave_list - 
 * @argc: 
 * @argv: 
 * 
 * 
 * Returns: int
 */
int do_slave_list(int argc, char **argv)
{
   int sk;
   uint32_t x_int;
   uint8_t x_name[1024]="";
   xdr_enc_t *enc=NULL;
   xdr_dec_t *dec=NULL;

   if( argc != 3 ) die(ExitGulm_BadOption, "Wrong number of arguments.\n");
   if((sk=connect_to_server(argv[2], "lt", cpl_lt, &enc, &dec)) < 0 ) {
      if( sk == -2 ) fprintf(stderr, "slavelist only works to LTs\n");
      die(ExitGulm_InitFailed, "Failed to connect to server\n");
   }

   if(xdr_enc_uint32(enc, gulm_info_slave_list_req)!=0) return -1;
   if(xdr_enc_flush(enc)!=0) return -1;

   if(xdr_dec_uint32(dec, &x_int)!=0) return -1;
   if( x_int != gulm_info_slave_list_rpl )
      die(ExitGulm_BadLogic, "Got strange replay %#x\n", x_int);

   if(xdr_dec_list_start(dec)!=0) return -1;
   while( xdr_dec_list_stop(dec) != 0 ) {
      if(xdr_dec_string_nm(dec, x_name, 1024)!=0) return -1;
      if(xdr_dec_uint32(dec, &x_int)!=0) return -1;

      printf("poller:%d name:%s\n", x_int, x_name);
   }
   xdr_enc_uint32(enc, gulm_socket_close);
   xdr_enc_flush(enc);

   xdr_enc_release(enc);
   xdr_dec_release(dec);
   close(sk);
   return 0;
}

/**
 * do_rerun_queues - 
 * @argc: 
 * @argv: 
 * 
 * 
 * Returns: int
 */
int do_rerun_queues(int argc, char **argv)
{
   int sk;
   xdr_enc_t *enc=NULL;
   xdr_dec_t *dec=NULL;

   if( argc != 3 ) die(ExitGulm_BadOption, "Wrong number of arguments.\n");
   if((sk=connect_to_server(argv[2], "lt000", cpl_lt, &enc, &dec)) < 0 ) {
      if( sk == -2 ) fprintf(stderr, "rerunqueues onlyworks to LTs\n");
      die(ExitGulm_InitFailed, "Failed to connect to server\n");
   }

   if(xdr_enc_uint32(enc, gulm_lock_rerunqueues)!=0) return -1;
   if(xdr_enc_flush(enc)!=0) return -1;

   xdr_enc_uint32(enc, gulm_socket_close);
   xdr_enc_flush(enc);

   xdr_enc_release(enc);
   xdr_dec_release(dec);
   close(sk);
   return 0;
}

/**
 * sigalrm - 
 * @sig: 
 */
void sigalrm(int sig) {
   fprintf(stderr, "Command timed out.\n");
   exit(142);
}

/**
 * main - 
 * @argc: 
 * @argv: 
 * 
 * 
 * Returns: int
 */
int main(int argc, char **argv)
{
   prog_name = argv[0];

   if( argc < 2) die(ExitGulm_ParseFail, "Wrong number of arguments.\n");

   signal(SIGALRM, sigalrm);
   alarm(10);

   if( strncasecmp(argv[1], "getstats", 9) == 0 ) {
      do_get_keys(argc, argv, gulm_info_stats_req,
            statsfilter_client, plain_print);
   }else
   if( strncasecmp(argv[1], "xmlstats", 9) == 0 ) {
      do_get_keys(argc, argv, gulm_info_stats_req,
            statsfilter_client, xml_print);
   }else
   if( strncasecmp(argv[1], "rawstats", 9) == 0 ) {
      do_raw_stats(argc, argv);
   }else
   if( strncasecmp(argv[1], "config", 7) == 0 ) {
      do_get_keys(argc, argv, gulm_core_configreq, NULL, plain_print);
   }else
   if( strncasecmp(argv[1], "slavelist", 10) == 0 ) {
      do_slave_list(argc, argv);
   }else
   if( strncasecmp(argv[1], "servicelist", 10) == 0 ) {
      do_service_list(argc, argv);
   }else
   if( strncasecmp(argv[1], "rerunqueues", 12) == 0 ) {
      do_rerun_queues(argc, argv);
   }else
   if( strncasecmp(argv[1], "shutdown", 9) == 0 ) {
      do_shutdown_core(argc, argv);
   }else
   if( strncasecmp(argv[1], "switchPending", 14) == 0 ) {
      do_switchtopending(argc, argv);
   }else
   if( strncasecmp(argv[1], "nodelist", 9) == 0 ) {
      do_get_mbr_list(argc, argv);
   }else
   if( strncasecmp(argv[1], "nodeinfo", 9) == 0 ) {
      do_get_one_mbr(argc, argv);
   }else
   if( strncasecmp(argv[1], "forceExpire", 12) == 0 ) {
      do_force_node_state(argc, argv);
   }else
   if( strncasecmp(argv[1], "setverb", 8) == 0 ) {
      do_set_verb(argc, argv);
   }else
   if( strncasecmp(argv[1], "version", 8) == 0 ||
       strncmp(argv[1], "-V", 3) == 0 ) {
      printf("%s %s (built " __DATE__ " " __TIME__ ")\n"
            "Copyright (C) 2004 Red Hat, Inc.  All rights reserved.\n",
              argv[0], RELEASE);
   }else
   if( strncmp(argv[1], "-h", 3) == 0 ||
       strncmp(argv[1], "--help", 7) == 0 ||
       strncmp(argv[1], "help", 5) == 0 ) {
      print_usage();
   }else
   {
      fprintf(stderr, "Unknown command \"%s\"\nPlease use '-h' for usage.\n",
            argv[1]);
   }

   return 0;
}
/* vim: set ai cin et sw=3 ts=3 : */
