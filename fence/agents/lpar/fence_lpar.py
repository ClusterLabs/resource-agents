#!/usr/bin/python

#####
##
## The Following Agent Has Been Tested On:
##
##  Version       
## +---------------------------------------------+
##  Tested on HMC
##
#####

import sys, re, pexpect
sys.path.append("@FENCEAGENTSLIBDIR@")
from fencing import *

#BEGIN_VERSION_GENERATION
RELEASE_VERSION=""
REDHAT_COPYRIGHT=""
BUILD_DATE=""
#END_VERSION_GENERATION

def get_power_status(conn, options):
	try:
		conn.send("lssyscfg -r lpar -m "+ options["-s"] +" --filter 'lpar_names=" + options["-n"] + "'\n")
		conn.log_expect(options, options["-c"], SHELL_TIMEOUT)
	except pexpect.EOF:
		fail(EC_CONNECTION_LOST)
	except pexpect.TIMEOUT:
		fail(EC_TIMED_OUT)
				
	status = re.compile(",state=(.*?),", re.IGNORECASE).search(conn.before).group(1)

	##
	## Transformation to standard ON/OFF status if possible
	if status == "Running":
		status = "on"
	else:
		status = "off"

	return status

def set_power_status(conn, options):
	try:
		if options["-o"] == "on":
			conn.send("chsysstate -o on -r lpar -m " + options["-s"] + 
				" -n " + options["-n"] + 
				" -f `lssyscfg -r lpar -F curr_profile " +
				" -m " + options["-s"] +
				" --filter \"lpar_names="+ options["-n"] +"\"`\n" )
		else:
			conn.send("chsysstate -o shutdown -r lpar --immed" +
				" -m " + options["-s"] + " -n " + options["-n"] + "\n")		
		conn.log_expect(options, options["-c"], POWER_TIMEOUT)
	except pexpect.EOF:
		fail(EC_CONNECTION_LOST)
	except pexpect.TIMEOUT:
		fail(EC_TIMED_OUT)

def get_lpar_list(conn, options):
	outlets = { }
	try:
		conn.send("lssyscfg -r lpar -m " + options["-s"] + 
			" -F name:state\n")
		conn.log_expect(options, options["-c"], POWER_TIMEOUT)

		## We have to remove first line (command) and last line (part of new prompt)
		####
		res = re.search("^.+?\n(.*)\n.*$", conn.before, re.S)

		if res == None:
			fail_usage("Unable to parse output of list command")
		
		lines = res.group(1).split("\n")
		for x in lines:
			s = x.split(":")
			outlets[s[0]] = ("", s[1])
	except pexpect.EOF:
		fail(EC_CONNECTION_LOST)
	except pexpect.TIMEOUT:
		fail(EC_TIMED_OUT)

	return outlets

def main():
	device_opt = [  "help", "version", "agent", "quiet", "verbose", "debug",
			"action", "ipaddr", "login", "passwd", "passwd_script",
			"secure", "partition", "managed" ]

	options = check_input(device_opt, process_input(device_opt))

	## 
	## Fence agent specific defaults
	#####
	if 0 == options.has_key("-c"):
		options["-c"] = ":~>"

	if 0 == options.has_key("-x"):
		fail_usage("Failed: You have to use ssh connection (-x) to fence device")

	if 0 == options.has_key("-s"):
		fail_usage("Failed: You have to enter name of managed system")

        if (0 == ["list", "monitor"].count(options["-o"].lower())) and (0 == options.has_key("-n")):
                fail_usage("Failed: You have to enter name of the partition")

	##
	## Operate the fencing device
	####
	conn = fence_login(options)
	fence_action(conn, options, set_power_status, get_power_status, get_lpar_list)

	##
	## Logout from system
	######
	conn.send("quit\r\n")
	conn.close()

if __name__ == "__main__":
	main()
