#!/usr/bin/python

# The Following Agent Has Been Tested On:
#
# Sun(tm) Advanced Lights Out Manager CMT v1.6.1
# as found on SUN T2000 Niagara

import sys, re, pexpect, time
sys.path.append("@FENCEAGENTSLIBDIR@")
from fencing import *

#BEGIN_VERSION_GENERATION
RELEASE_VERSION="Sun Advanced Lights Out Manager (ALOM)"
REDHAT_COPYRIGHT=""
BUILD_DATE=""
#END_VERSION_GENERATION

def get_power_status(conn, options):
	result = ""
	try:
		conn.sendline("showplatform")
                conn.log_expect(options, options["-c"], SHELL_TIMEOUT)
		status = re.search("standby",conn.before.lower())
		result=(status!=None and "off" or "on")
	except pexpect.EOF:
		fail(EC_CONNECTION_LOST)
	except pexpect.TIMEOUT:
		fail(EC_TIMED_OUT)

	return result

def set_power_status(conn, options):
	try:
		cmd_line=(options["-o"]=="on" and "poweron" or "poweroff -f -y")
		conn.sendline(cmd_line)
		conn.log_expect(options, options["-c"],POWER_TIMEOUT)
		#Get the machine some time between poweron and poweroff
		time.sleep(POWER_TIMEOUT)
		
	except pexpect.EOF:
		fail(EC_CONNECTION_LOST)
	except pexpect.TIMEOUT:
		fail(EC_TIMED_OUT)
		
def main():
	device_opt = [  "help", "version", "agent", "quiet", "verbose", "debug",
			"action", "ipaddr", "login", "passwd", "passwd_script",
			"secure",  "test" ]

	options = check_input(device_opt, process_input(device_opt))

	# Default command is sc>
	if (not options.has_key("-c")):
            options["-c"] = "sc\>\ "

	# Default to ssh
	options["-x"] = 1
	options["telnet_over_ssh"] = 1
		
	# Operate the fencing device
	conn = fence_login(options)
	fence_action(conn, options, set_power_status, get_power_status,None)

	# Logout from system
	conn.sendline("logout")
	conn.close()

if __name__ == "__main__":
	main()
