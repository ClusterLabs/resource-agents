#!/usr/bin/python

#
# The Following agent has been tested on:
# vmrun 2.0.0 build-116503 (from VMware Server 2.0) against:
# 	VMware ESX 3.5 (works correctly)
# 	VMware Server 2.0.0 (works correctly)
#	VMware ESXi 3.5 update 2 (works correctly)
# 	VMware Server 1.0.7 (doesn't work)
# Any older version of vmrun doesn't have support for ESX/ESXi
#

import sys, re, pexpect
sys.path.append("@FENCEAGENTSLIBDIR@")
from fencing import *

#BEGIN_VERSION_GENERATION
RELEASE_VERSION="VMware Agent using VIX API"
REDHAT_COPYRIGHT=""
BUILD_DATE=""
#END_VERSION_GENERATION

# Path to vmrun command
VMRUN_COMMAND="/usr/bin/vmrun"
# Default type for -T parameter of vmrun command (default is esx, can be changed with -c)
VMWARE_DEFAULT_TYPE="esx"
# Minimum required version of vmrun command
VMRUN_MINIMUM_REQUIRED_VERSION=2

# Return string with command and additional parameters (something like vmrun -h 'host'
def vmware_vix_prepare_command(options,add_login_params,additional_params):
	res=options["-c"]

	if (add_login_params):
		res+=" -h '%s' -u '%s' -p '%s' -T '%s' "%(options["-a"],options["-l"],options["-p"],options["-d"])

	if (additional_params!=""):
		res+=additional_params

	return res

# Log message if user set verbose option
def vmware_vix_log(options, message):
	if options["log"] >= LOG_MODE_VERBOSE:
		options["debug_fh"].write(message+"\n")

# Run vmrun command with timeout and parameters. Internaly uses vmware_vix_prepare_command. Returns string
# with output from vmrun command. If something fails (command not found, exit code is not 0), fail_usage
# function is called (and never return).
def vmware_vix_run_command(options,add_login_params,additional_params):
	command=vmware_vix_prepare_command(options,add_login_params,additional_params)

	try:
		vmware_vix_log(options,command)

		(res_output,res_code)=pexpect.run(command,POWER_TIMEOUT+SHELL_TIMEOUT+LOGIN_TIMEOUT,True)

		if (res_code==None):
			fail(EC_TIMED_OUT)
		if ((res_code!=0) and (add_login_params)):
			vmware_vix_log(options,res_output)
			fail_usage("vmrun returned %s"%(res_output))
		else:
			vmware_vix_log(options,res_output)

	except pexpect.ExceptionPexpect:
		fail_usage(("Bad command name %s. Make sure, that you installed\nvmrun command (VIX Api). "+\
		"If you have nonstandard installation location,\ntry use -c switch.")%(options["-c"]))

	return res_output

# Returns True, if user uses supported vmrun version (currently >=2.0.0) otherwise False.
def vmware_vix_is_supported_vmrun_version(options):
	vmware_help_str=vmware_vix_run_command(options,False,"")
	version_re=re.search("vmrun version (\d\.(\d[\.]*)*)",vmware_help_str.lower())
	if (version_re==None):
		    return False   # Looks like this "vmrun" is not real vmrun

	version_array=version_re.group(1).split(".")

	try:
		if (int(version_array[0])<VMRUN_MINIMUM_REQUIRED_VERSION):
			return False
	except Exception:
		return False

	return True

def get_outlets_status(conn, options):
	outlets={}

	running_machines=vmware_vix_run_command(options,True,"list")
	all_machines=vmware_vix_run_command(options,True,"listRegisteredVM")

	all_machines_array=all_machines.splitlines()[1:]
	running_machines_array=running_machines.splitlines()[1:]

	for machine in all_machines_array:
		if (machine!=""):
			outlets[machine]=("",((machine in running_machines_array) and "on" or "off"))

	return outlets

def get_power_status(conn,options):
	outlets=get_outlets_status(conn,options)

	if (not (options["-n"] in outlets)):
		fail_usage("Failed: You have to enter existing name of virtual machine!")
	else:
		return outlets[options["-n"]][1]

def set_power_status(conn, options):
	additional_params="%s '%s'"%((options["-o"]=="on" and "start" or "stop"),options["-n"])
	if (options["-o"]=="off"):
		additional_params+=" hard"

	vmware_vix_run_command(options,True,additional_params)

# Define new options
def vmware_vix_define_new_opts():
	all_opt["vmrun_cmd"]={
		"getopt":"c:",
		"help":"-c <command>   Name of vmrun command (default "+VMRUN_COMMAND+")",
		"order": 2}
	all_opt["host_type"]={
		"getopt":"d:",
		"help":"-d <type>      Type of VMware to connect (default "+VMWARE_DEFAULT_TYPE+")",
		"order": 2}

def main():
	device_opt = [  "help", "version", "agent", "quiet", "verbose", "debug",
			"action", "ipaddr", "login", "passwd", "passwd_script",
			"test", "port", "separator", "vmrun_cmd", "host_type" ]

	vmware_vix_define_new_opts()

	options = check_input(device_opt, process_input(device_opt))

	# Fence agent specific defaults
        if (not options.has_key("-c")):
		options["-c"]=VMRUN_COMMAND

	if (not options.has_key("-d")):
		options["-d"]=VMWARE_DEFAULT_TYPE

	# Test user vmrun command version
	if (not (vmware_vix_is_supported_vmrun_version(options))):
		fail_usage("Unsupported version of vmrun command! You must use at least version %d!"%(VMRUN_MINIMUM_REQUIRED_VERSION))

	# Operate the fencing device
	fence_action(None, options, set_power_status, get_power_status, get_outlets_status)

if __name__ == "__main__":
	main()
