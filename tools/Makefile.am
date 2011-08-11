#
# heartbeat: Linux-HA heartbeat code
#
# Copyright (C) 2001 Michael Moerz
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
#
MAINTAINERCLEANFILES    = Makefile.in

SUBDIRS			= ocft

INCLUDES                = -I$(top_builddir)/include -I$(top_srcdir)/include

halibdir		= $(libdir)/heartbeat

EXTRA_DIST		= ocf-tester.8 sfex_init.8

sbin_PROGRAMS		= 
sbin_SCRIPTS		= ocf-tester
halib_PROGRAMS		= findif

man8_MANS		= ocf-tester.8

if BUILD_SFEX
halib_PROGRAMS		+= sfex_daemon
sbin_PROGRAMS		+= sfex_init sfex_stat
man8_MANS		+= sfex_init.8
endif

if USE_LIBNET
halib_PROGRAMS		+= send_arp
send_arp_SOURCES	= send_arp.libnet.c
send_arp_CFLAGS		= @LIBNETDEFINES@
send_arp_LDADD		= $(GLIBLIB) -lplumb @LIBNETLIBS@
else

if SENDARP_LINUX
halib_PROGRAMS		+= send_arp
send_arp_SOURCES	= send_arp.linux.c
endif

endif

sfex_daemon_SOURCES	= sfex_daemon.c sfex.h sfex_lib.c sfex_lib.h
sfex_daemon_CFLAGS	= -D_GNU_SOURCE
sfex_daemon_LDADD	= $(GLIBLIB) -lplumb -lplumbgpl

sfex_init_SOURCES	= sfex_init.c sfex.h sfex_lib.c sfex_lib.h
sfex_init_CFLAGS	= -D_GNU_SOURCE
sfex_init_LDADD		= $(GLIBLIB) -lplumb -lplumbgpl

sfex_stat_SOURCES	= sfex_stat.c sfex.h sfex_lib.c sfex_lib.h
sfex_stat_CFLAGS	= -D_GNU_SOURCE
sfex_stat_LDADD		= $(GLIBLIB) -lplumb -lplumbgpl

findif_SOURCES		= findif.c

if BUILD_TICKLE
halib_PROGRAMS		+= tickle_tcp
tickle_tcp_SOURCES	= tickle_tcp.c
endif

.PHONY: install-exec-hook
