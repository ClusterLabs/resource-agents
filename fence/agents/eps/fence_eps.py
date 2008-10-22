#!/usr/bin/python

# The Following Agent Has Been Tested On:
# ePowerSwitch 8M+ version 1.0.0.4

import sys, re, time
import httplib, base64, string,socket
sys.path.append("@FENCEAGENTSLIBDIR@")
from fencing import *

#BEGIN_VERSION_GENERATION
RELEASE_VERSION="ePowerSwitch 8M+ (eps)"
REDHAT_COPYRIGHT=""
BUILD_DATE=""
#END_VERSION_GENERATION

# Log actions and results from EPS device
def eps_log(options,str):
	if options["log"]>=LOG_MODE_VERBOSE:
		options["debug_fh"].write(str)

# Run command on EPS device.
# @param options Device options
# @param params HTTP GET parameters (without ?)
def eps_run_command(options, params):
	try:
		# New http connection
		conn = httplib.HTTPConnection(options["-a"])

		request_str="/"+options["-c"]

		if (params!=""):
			request_str+="?"+params

		eps_log(options,"GET "+request_str+"\n")
		conn.putrequest('GET', request_str)

		if (options.has_key("-l")):
			if (not options.has_key("-p")):
				options["-p"]="" # Default is empty password
				
			# String for Authorization header
			auth_str = 'Basic ' + string.strip(base64.encodestring(options["-l"]+':'+options["-p"]))
			eps_log(options,"Authorization:"+auth_str+"\n")
			conn.putheader('Authorization',auth_str)

		conn.endheaders()

		response = conn.getresponse()

		eps_log(options,"%d %s\n"%(response.status,response.reason))

		#Response != OK -> couldn't login
		if (response.status!=200):
			fail(EC_LOGIN_DENIED)

		result=response.read()
		eps_log(options,result+"\n")
		conn.close()

	except socket.timeout:
		fail(EC_TIMED_OUT)
	except socket.error:
		fail(EC_LOGIN_DENIED)

	return result

def get_power_status(conn, options):
	result = ""

	ret_val=eps_run_command(options,"")

	status=re.search("p"+options["-n"].lower()+"=(0|1)\s*\<br\>",ret_val.lower())
	if status==None:
		fail_usage("Failed: You have to enter existing physical plug!")

	result=(status.group(1)=="1" and "on" or "off")

	return result

def set_power_status(conn, options):
	ret_val=eps_run_command(options,"P%s=%s"%(options["-n"],(options["-o"]=="on" and "1" or "0")))

# Define new option
def eps_define_new_opts():
	all_opt["hidden_page"]={
		"getopt":"c:",
		"help":"-c <page>      Name of hidden page (default hidden.htm)",
		"order": 1}

# Starting point of fence agent
def main():
	device_opt = [  "help", "version", "agent", "quiet", "verbose", "debug",
			"action", "ipaddr", "login", "passwd", "passwd_script",
			"test", "port", "hidden_page", "no_login", "no_password" ]

	eps_define_new_opts()

	options = check_input(device_opt,process_input(device_opt))

	if (not options.has_key("-c")):
		options["-c"]="hidden.htm"

	#Run fence action. Conn is None, beacause we always need run new curl command
	fence_action(None, options, set_power_status, get_power_status)

if __name__ == "__main__":
	main()
