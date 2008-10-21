#!/usr/bin/python

#####
##
## The Following Agent Has Been Tested On:
##
##  Model                 Firmware
## +--------------------+---------------------------+
## (1) Main application	  BRET85K, rev 16  
##     Boot ROM           BRBR67D, rev 16
##     Remote Control     BRRG67D, rev 16
##
#####

import sys, re, pexpect
sys.path.append("@FENCEAGENTSLIBDIR@")
from fencing import *

#BEGIN_VERSION_GENERATION
RELEASE_VERSION="New Bladecenter Agent - test release on steroids"
REDHAT_COPYRIGHT=""
BUILD_DATE="March, 2008"
#END_VERSION_GENERATION

def get_power_status(conn, options):
	try:
		node_cmd = "system:blade\[" + options["-n"] + "\]>"

		conn.send("env -T system:blade[" + options["-n"] + "]\r\n")
		conn.log_expect(options, node_cmd, SHELL_TIMEOUT)
		conn.send("power -state\r\n")
		conn.log_expect(options, node_cmd, SHELL_TIMEOUT)
		status = conn.before.splitlines()[-1]
		conn.send("env -T system\r\n")
		conn.log_expect(options, options["-c"], SHELL_TIMEOUT)
	except pexpect.EOF:
		fail(EC_CONNECTION_LOST)
	except pexpect.TIMEOUT:
		fail(EC_TIMED_OUT)

	return status.lower().strip()

def set_power_status(conn, options):
	action = {
		'on' : "powerup",
		'off': "powerdown"
	}[options["-o"]]

	try:
		node_cmd = "system:blade\[" + options["-n"] + "\]>"

		conn.send("env -T system:blade[" + options["-n"] + "]\r\n")
		conn.log_expect(options, node_cmd, SHELL_TIMEOUT)
		conn.send("power -"+options["-o"]+"\r\n")
		conn.log_expect(options, node_cmd, SHELL_TIMEOUT)
		conn.send("env -T system\r\n")
		conn.log_expect(options, options["-c"], SHELL_TIMEOUT)
	except pexpect.EOF:
		fail(EC_CONNECTION_LOST)
	except pexpect.TIMEOUT:
		fail(EC_TIMED_OUT)

def main():
	device_opt = [  "help", "version", "agent", "quiet", "verbose", "debug",
			"action", "ipaddr", "login", "passwd", "passwd_script",
			"cmd_prompt", "secure", "port", "identity_file" ]

	options = check_input(device_opt, process_input(device_opt))

	## 
	## Fence agent specific defaults
	#####
	if 0 == options.has_key("-c"):
		options["-c"] = "system>"

	##
	## Operate the fencing device
	######
	conn = fence_login(options)
	fence_action(conn, options, set_power_status, get_power_status)

	##
	## Logout from system
	######
	conn.send("exit\r\n")
	conn.close()

if __name__ == "__main__":
	main()
