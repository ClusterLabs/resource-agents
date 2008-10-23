#!/usr/bin/python

#####
##
## The Following Agent Has Been Tested On:
##
##  DRAC Version       Firmware
## +-----------------+---------------------------+
##  DRAC 5             1.0  (Build 06.05.12)
##  DRAC 5             1.21 (Build 07.05.04)
##
## @note: drac_version, modulename were removed
#####

import sys, re, pexpect
sys.path.append("@FENCEAGENTSLIBDIR@")
from fencing import *

#BEGIN_VERSION_GENERATION
RELEASE_VERSION="New Drac5 Agent - test release on steroids"
REDHAT_COPYRIGHT=""
BUILD_DATE="March, 2008"
#END_VERSION_GENERATION

def get_power_status(conn, options):
	try:
		conn.sendline("racadm serveraction powerstatus")
		conn.log_expect(options, options["-c"], SHELL_TIMEOUT)
	except pexpect.EOF:
		fail(EC_CONNECTION_LOST)
	except pexpect.TIMEOUT:
		fail(EC_TIMED_OUT)
				
	status = re.compile("Server power status: (.*)", re.IGNORECASE).search(conn.before).group(1)
	return status.lower().strip()

def set_power_status(conn, options):
	action = {
		'on' : "powerup",
		'off': "powerdown"
	}[options["-o"]]

	try:
		conn.sendline("racadm serveraction " + action)
		conn.log_expect(options, options["-c"], POWER_TIMEOUT)
	except pexpect.EOF:
		fail(EC_CONNECTION_LOST)
	except pexpect.TIMEOUT:
		fail(EC_TIMED_OUT)

def main():
	device_opt = [  "help", "version", "agent", "quiet", "verbose", "debug",
			"action", "ipaddr", "login", "passwd", "passwd_script",
			"cmd_prompt", "secure",
			"drac_version", "module_name" ]

	options = check_input(device_opt, process_input(device_opt))

	## 
	## Fence agent specific defaults
	#####
	if 0 == options.has_key("-c"):
		options["-c"] = "\$"

	##
	## Operate the fencing device
	######
	conn = fence_login(options)
	fence_action(conn, options, set_power_status, get_power_status, None)

	##
	## Logout from system
	######
	conn.sendline("exit")
	conn.close()

if __name__ == "__main__":
	main()
