#
# Copyright (c) 2016 Red Hat, Inc, Oyvind Albrigtsen
#                    All Rights Reserved.
#
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
# 

import sys, os, logging, syslog

argv=sys.argv
env=os.environ

#
# 	Common variables for the OCF Resource Agents supplied by
# 	heartbeat.
#

OCF_SUCCESS=0
OCF_ERR_GENERIC=1
OCF_ERR_ARGS=2
OCF_ERR_UNIMPLEMENTED=3
OCF_ERR_PERM=4
OCF_ERR_INSTALLED=5
OCF_ERR_CONFIGURED=6
OCF_NOT_RUNNING=7

# Non-standard values.
#
# OCF does not include the concept of master/slave resources so we
#   need to extend it so we can discover a resource's complete state.
#
# OCF_RUNNING_MASTER:  
#    The resource is in "master" mode and fully operational
# OCF_FAILED_MASTER:
#    The resource is in "master" mode but in a failed state
# 
# The extra two values should only be used during a probe.
#
# Probes are used to discover resources that were started outside of
#    the CRM and/or left behind if the LRM fails.
# 
# They can be identified in RA scripts by checking for:
#   [ "${__OCF_ACTION}" = "monitor" -a "${OCF_RESKEY_CRM_meta_interval}" = "0" ]
# 
# Failed "slaves" should continue to use: OCF_ERR_GENERIC
# Fully operational "slaves" should continue to use: OCF_SUCCESS
#
OCF_RUNNING_MASTER=8
OCF_FAILED_MASTER=9


## Own logger handler that uses old-style syslog handler as otherwise
## everything is sourced from /dev/syslog
class SyslogLibHandler(logging.StreamHandler):
	"""
	A handler class that correctly push messages into syslog
	"""
	def emit(self, record):
		syslog_level = {
			logging.CRITICAL:syslog.LOG_CRIT,
			logging.ERROR:syslog.LOG_ERR,
			logging.WARNING:syslog.LOG_WARNING,
			logging.INFO:syslog.LOG_INFO,
			logging.DEBUG:syslog.LOG_DEBUG,
			logging.NOTSET:syslog.LOG_DEBUG,
		}[record.levelno]

		msg = self.format(record)

		# syslog.syslog can not have 0x00 character inside or exception
		# is thrown
		syslog.syslog(syslog_level, msg.replace("\x00","\n"))
		return


OCF_RESOURCE_INSTANCE = env.get("OCF_RESOURCE_INSTANCE")

HA_DEBUG = env.get("HA_debug", 0)
HA_DATEFMT = env.get("HA_DATEFMT", "%b %d %T ")
HA_LOGFACILITY = env.get("HA_LOGFACILITY")
HA_LOGFILE = env.get("HA_LOGFILE")
HA_DEBUGLOG = env.get("HA_DEBUGLOG")

log = logging.getLogger(os.path.basename(argv[0]))
log.setLevel(logging.DEBUG)

## add logging to stderr
if sys.stdout.isatty():
	seh = logging.StreamHandler(stream=sys.stderr)
	if HA_DEBUG == 0:
		seh.setLevel(logging.WARNING)
	sehformatter = logging.Formatter('%(filename)s(%(OCF_RESOURCE_INSTANCE)s)[%(process)s]:\t%(asctime)s%(levelname)s: %(message)s', datefmt=HA_DATEFMT)
	seh.setFormatter(sehformatter)
	log.addHandler(seh)

## add logging to syslog
if HA_LOGFACILITY:
	slh = SyslogLibHandler()
	if HA_DEBUG == 0:
		slh.setLevel(logging.WARNING)
	slhformatter = logging.Formatter('%(levelname)s: %(message)s')
	slh.setFormatter(slhformatter)
	log.addHandler(slh)

## add logging to file
if HA_LOGFILE:
	lfh = logging.FileHandler(HA_LOGFILE)
	if HA_DEBUG == 0:
		lfh.setLevel(logging.WARNING)
	lfhformatter = logging.Formatter('%(filename)s(%(OCF_RESOURCE_INSTANCE)s)[%(process)s]:\t%(asctime)s%(levelname)s: %(message)s', datefmt=HA_DATEFMT)
	lfh.setFormatter(lfhformatter)
	log.addHandler(lfh)

## add debug logging to file
if HA_DEBUGLOG and HA_LOGFILE != HA_DEBUGLOG:
	dfh = logging.FileHandler(HA_DEBUGLOG)
	if HA_DEBUG == 0:
		dfh.setLevel(logging.WARNING)
	dfhformatter = logging.Formatter('%(filename)s(%(OCF_RESOURCE_INSTANCE)s)[%(process)s]:\t%(asctime)s%(levelname)s: %(message)s', datefmt=HA_DATEFMT)
	dfh.setFormatter(dfhformatter)
	log.addHandler(dfh)

logger = logging.LoggerAdapter(log, {'OCF_RESOURCE_INSTANCE': OCF_RESOURCE_INSTANCE})
