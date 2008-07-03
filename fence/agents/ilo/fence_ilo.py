#!/usr/bin/python

#####
##
## The Following Agent Has Been Tested On:
##
##  iLO Version
## +---------------------------------------------+
##  iLO  / firmware 1.91 / RIBCL 2.22
##  iLO2 / firmware 1.22 / RIBCL 2.22 
##  iLO2 / firmware 1.50 / RIBCL 2.22
#####

import sys, re, pexpect, socket
sys.path.append("@FENCEAGENTSLIBDIR@")
from fencing import *
from OpenSSL import SSL

#BEGIN_VERSION_GENERATION
RELEASE_VERSION="New ILO Agent - test release on steroids"
REDHAT_COPYRIGHT=""
BUILD_DATE="March, 2008"
#END_VERSION_GENERATION

def get_power_status(conn, options):
	conn.send("<LOGIN USER_LOGIN = \"" + options["-l"] + "\"" + \
		" PASSWORD = \"" + options["-p"] + "\">\r\n")
	conn.send("<SERVER_INFO MODE = \"read\"><GET_HOST_POWER_STATUS/>\r\n")
	conn.send("</SERVER_INFO></LOGIN>\r\n")
	conn.log_expect(options, "HOST_POWER=\"(.*?)\"", POWER_TIMEOUT)

	status = conn.match.group(1)
	return status.lower().strip()

def set_power_status(conn, options):
	conn.send("<LOGIN USER_LOGIN = \"" + options["-l"] + "\"" + \
		" PASSWORD = \"" + options["-p"] + "\">\r\n")
	conn.send("<SERVER_INFO MODE = \"write\">")

	if options.has_key("fw_processor") and options["fw_processor"] == "iLO2":
		if options["fw_version"] > 1.29:
			conn.send("<HOLD_PWR_BTN TOGGLE=\"yes\" />\r\n")
		else:
			conn.send("<HOLD_PWR_BTN />\r\n")
	elif options["-r"] < 2.21:
		conn.send("<SET_HOST_POWER HOST_POWER = \"" + options["-o"] + "\" />\r\n")
	else:
		if options["-o"] == "off":
			conn.send("<HOLD_PWR_BTN/>\r\n")
		else:
			conn.send("<PRESS_PWR_BTN/>\r\n")
	conn.send("</SERVER_INFO></LOGIN>\r\n")

	return

def main():
	device_opt = [  "help", "version", "agent", "quiet", "verbose", "debug",
			"action", "ipaddr", "login", "passwd", "passwd_script",
			"ssl", "ribcl" ]

	options = check_input(device_opt, process_input(device_opt))

	options["-z"] = 1
	LOGIN_TIMEOUT = 10

	##
	## Login and get version number
	####
	conn = fence_login(options)
	try:
		conn.send("<?xml version=\"1.0\"?>\r\n")
		conn.log_expect(options, [ "</RIBCL>", "<END_RIBCL/>" ], LOGIN_TIMEOUT)
		version = re.compile("<RIBCL VERSION=\"(.*?)\"", re.IGNORECASE).search(conn.before).group(1)
		if options.has_key("-r") == 0:
			options["-r"] = float(version)

		if options["-r"] >= 2:
			conn.send("<RIBCL VERSION=\"2.0\">\r\n")
		else:
			conn.send("<RIBCL VERSION=\"1.2\">\r\n")

		conn.send("<LOGIN USER_LOGIN = \"" + options["-l"] + "\"" + \
			" PASSWORD = \"" + options["-p"] + "\">\r\n")
		if options["-r"] >= 2:
			conn.send("<RIB_INFO MODE=\"read\"><GET_FW_VERSION />\r\n")
			conn.send("</RIB_INFO>\r\n")
			conn.log_expect(options, "<GET_FW_VERSION\s*\n", SHELL_TIMEOUT)
			conn.log_expect(options, "/>", SHELL_TIMEOUT)
			options["fw_version"] = float(re.compile("FIRMWARE_VERSION\s*=\s*\"(.*?)\"", re.IGNORECASE).search(conn.before).group(1))
			options["fw_processor"] = re.compile("MANAGEMENT_PROCESSOR\s*=\s*\"(.*?)\"", re.IGNORECASE).search(conn.before).group(1)
		conn.send("</LOGIN>\r\n")
	except pexpect.TIMEOUT:
		fail(EC_LOGIN_DENIED)

	##
	## Fence operations
	####
	fence_action(conn, options, set_power_status, get_power_status)

if __name__ == "__main__":
	main()
