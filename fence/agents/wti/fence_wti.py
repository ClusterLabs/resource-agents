#!/usr/bin/python

#####
##
## The Following Agent Has Been Tested On:
##
##  Version            Firmware
## +-----------------+---------------------------+
##  WTI RSM-8R4         ?? unable to find out ??
##  WTI MPC-??? 	?? unable to find out ??
##  WTI IPS-800-CE     v1.40h		(no username) ('list' tested)
#####

import sys, re, pexpect
sys.path.append("@FENCEAGENTSLIBDIR@")
from fencing import *

#BEGIN_VERSION_GENERATION
RELEASE_VERSION="New WTI Agent - test release on steroids"
REDHAT_COPYRIGHT=""
BUILD_DATE="March, 2008"
#END_VERSION_GENERATION

def get_power_status(conn, options):
	try:
		conn.send("/S"+"\r\n")
		conn.log_expect(options, options["-c"], SHELL_TIMEOUT)
	except pexpect.EOF:
		fail(EC_CONNECTION_LOST)
	except pexpect.TIMEOUT:
		fail(EC_TIMED_OUT)
	
	plug_section = 0
	outlets = {}
	for line in conn.before.splitlines():
		if (plug_section == 2) and line.find("|") >= 0:
			plug_line = [x.strip().lower() for x in line.split("|")]
			if len(plug_line) < len(plug_header):
				plug_section = -1
				pass
			if ["list", "monitor"].count(options["-o"]) == 0 and options["-n"].lower() == plug_line[plug_index]:
				return plug_line[status_index]
			else:
				## We already believe that first column contains plug number
				outlets[plug_line[0]] = (plug_line[name_index], plug_line[status_index])
		elif (plug_section == 1):
			plug_section = 2
			pass
		elif (line.upper().startswith("PLUG")):
			plug_section = 1
			plug_header = [x.strip().lower() for x in line.split("|")]
			plug_index = plug_header.index("plug")
			name_index = plug_header.index("name")
			status_index = plug_header.index("status")

	if ["list", "monitor"].count(options["-o"]) == 1:
		return outlets
	else:
		return "PROBLEM"

def set_power_status(conn, options):
	action = {
		'on' : "/on",
		'off': "/off"
	}[options["-o"]]

	try:
		conn.send(action + " " + options["-n"] + ",y\r\n")
		conn.log_expect(options, options["-c"], POWER_TIMEOUT)
	except pexpect.EOF:
		fail(EC_CONNECTION_LOST)
	except pexpect.TIMEOUT:
		fail(EC_TIMED_OUT)

def main():
	device_opt = [  "help", "version", "agent", "quiet", "verbose", "debug",
			"action", "ipaddr", "login", "passwd", "passwd_script",
			"cmd_prompt", "secure", "port", "no_login", "test" ]

	options = check_input(device_opt, process_input(device_opt))

	## 
	## Fence agent specific defaults
	#####
	if 0 == options.has_key("-c"):
		options["-c"] = [ "RSM>", "MPC>", "IPS>", "TPS>", "NBB>", "NPS>" ]

	##
	## Operate the fencing device
	##
	## @note: if there is not a login name then we assume that it is WTI-IPS
	##        where no login name is used
	#####	
	if (0 == options.has_key("-l")):
		try:
			conn = fspawn ('telnet ' + options["-a"])
			conn.log_expect(options, "Password: ", SHELL_TIMEOUT)
			conn.send(options["-p"]+"\r\n")
			conn.log_expect(options, options["-c"], SHELL_TIMEOUT)
		except pexpect.EOF:
			fail(EC_LOGIN_DENIED) 
		except pexpect.TIMEOUT:
			fail(EC_LOGIN_DENIED)		
	else:
		conn = fence_login(options)

	fence_action(conn, options, set_power_status, get_power_status, get_power_status)

	##
	## Logout from system
	######
	conn.send("/X"+"\r\n")
	conn.close()

if __name__ == "__main__":
	main()
