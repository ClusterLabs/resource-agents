#!/usr/bin/python

##
## Copyright (C) 2008 Red Hat, Inc. All Rights Reserved.
##
## The Following Agent Has Been Tested On:
##
##  Model       Firmware
## +---------------------------------------------+
##  AP7951	AOS v2.7.0, PDU APP v2.7.3
##  AP7941      AOS v3.5.7, PDU APP v3.5.6
##
## @note: ssh is very slow on AP7951 device
#####

import sys, re, pexpect
sys.path.append("@FENCEAGENTSLIBDIR@")
from fencing import *

#BEGIN_VERSION_GENERATION
RELEASE_VERSION="New APC Agent - test release on steroids"
REDHAT_COPYRIGHT=""
BUILD_DATE="March, 2008"
#END_VERSION_GENERATION

def get_power_status(conn, options):
	result = ""
	try:
		conn.send("1\r\n")
		conn.log_expect(options, options["-c"], SHELL_TIMEOUT)

		version = 0
		if (None == re.compile('.*Outlet Management.*', re.IGNORECASE | re.S).match(conn.before)):
			version = 2
		else:
			version = 3

		if version == 2:
			conn.send("2\r\n")
		else:
			conn.send("2\r\n")
			conn.log_expect(options, options["-c"], SHELL_TIMEOUT)
			conn.send("1\r\n")

		while 1 == conn.log_expect(options, [ options["-c"],  "Press <ENTER>" ], SHELL_TIMEOUT):
			result += conn.before
			conn.send("\r\n")
		result += conn.before
		conn.send(chr(03))		
		conn.log_expect(options, "- Logout", SHELL_TIMEOUT)
		conn.log_expect(options, options["-c"], SHELL_TIMEOUT)
	except pexpect.EOF:
		fail(EC_CONNECTION_LOST)
	except pexpect.TIMEOUT:
		fail(EC_TIMED_OUT)

	status = re.compile("\s*"+options["-n"]+"-.*(ON|OFF)", re.IGNORECASE).search(result).group(1)
	return status.lower().strip()

def set_power_status(conn, options):
	action = {
		'on' : "1",
		'off': "2"
	}[options["-o"]]

	try:
		conn.send("1\r\n")
		conn.log_expect(options, options["-c"], SHELL_TIMEOUT)

		version = 0
		if (None == re.compile('.*Outlet Management.*', re.IGNORECASE | re.S).match(conn.before)):
			version = 2
		else:
			version = 3

		if version == 2:
			conn.send("2\r\n")
		else:
			conn.send("2\r\n")
			conn.log_expect(options, options["-c"], SHELL_TIMEOUT)
			conn.send("1\r\n")

		while 1 == conn.log_expect(options, [ options["-c"],  "Press <ENTER>" ], SHELL_TIMEOUT):
			conn.send("\r\n")
		conn.send(options["-n"]+"\r\n")
		conn.log_expect(options, options["-c"], SHELL_TIMEOUT)
		if version == 3:
			conn.send("1\r\n")
			conn.log_expect(options, options["-c"], SHELL_TIMEOUT)
		conn.send(action+"\r\n")
		conn.log_expect(options, "Enter 'YES' to continue or <ENTER> to cancel :", SHELL_TIMEOUT)
		conn.send("YES\r\n")
		conn.log_expect(options, "Press <ENTER> to continue...", SHELL_TIMEOUT)
		conn.send("\r\n")
		conn.log_expect(options, options["-c"], SHELL_TIMEOUT)
		conn.send(chr(03))
		conn.log_expect(options, "- Logout", SHELL_TIMEOUT)
		conn.log_expect(options, options["-c"], SHELL_TIMEOUT)
	except pexcept.EOF:
		fail(EC_CONNECTION_LOST)
	except pexcept.TIMEOUT:
		fail(EC_TIMED_OUT)

def main():
	device_opt = [  "help", "version", "agent", "quiet", "verbose", "debug",
			"action", "ipaddr", "login", "passwd", "passwd_script",
			"secure", "port", "test" ]

	options = check_input(device_opt, process_input(device_opt))

	## 
	## Fence agent specific defaults
	#####
	if 0 == options.has_key("-c"):
		options["-c"] = "\n>"

	##
	## Operate the fencing device
	####
	conn = fence_login(options)
	fence_action(conn, options, set_power_status, get_power_status)

	##
	## Logout from system
	######
	conn.sendline("4")
	conn.close()

if __name__ == "__main__":
	main()
