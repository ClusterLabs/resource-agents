#!/usr/bin/python

#
# The Following agent has been tested on:
# VI Perl API 1.6 against:
# 	VMware ESX 3.5
#	VMware ESXi 3.5 update 2
# 	VMware Virtual Center 2.5
#

import sys, re, pexpect
sys.path.append("@FENCEAGENTSLIBDIR@")
from fencing import *

#BEGIN_VERSION_GENERATION
RELEASE_VERSION="VMware Agent using VI Perl API"
REDHAT_COPYRIGHT=""
BUILD_DATE=""
#END_VERSION_GENERATION

# Path to vmrun command
VMHELPER_COMMAND="fence_vmware_vi_helper"

# Return string with command and additional parameters (something like vmrun -h 'host'
def vmware_vi_prepare_command(options,add_login_params,additional_params):
	res=VMHELPER_COMMAND

	if (add_login_params):
		res+=" --server '%s' --username '%s' --password '%s' "%(options["-a"],options["-l"],options["-p"])

	if (options.has_key("-d")):
		res+="--datacenter '%s' "%(options["-d"])

	if (additional_params!=""):
		res+=additional_params

	return res

# Log message if user set verbose option
def vmware_vi_log(options, message):
	if options["log"] >= LOG_MODE_VERBOSE:
		options["debug_fh"].write(message+"\n")

# Run vmrun command with timeout and parameters. Internaly uses vmware_vix_prepare_command. Returns string
# with output from vmrun command. If something fails (command not found, exit code is not 0), fail_usage
# function is called (and never return).
def vmware_vi_run_command(options,add_login_params,additional_params):
	command=vmware_vi_prepare_command(options,add_login_params,additional_params)

	try:
		vmware_vi_log(options,command)

		(res_output,res_code)=pexpect.run(command,POWER_TIMEOUT+SHELL_TIMEOUT+LOGIN_TIMEOUT,True)

		if (res_code==None):
			fail(EC_TIMED_OUT)
		if ((res_code!=0) and (add_login_params)):
			vmware_vi_log(options,res_output)
			fail_usage("vmware_helper returned %s"%(res_output))
		else:
			vmware_vi_log(options,res_output)

	except pexpect.ExceptionPexpect:
		fail_usage("Cannot run vmware_helper command %s"%(VMHELPER_COMMAND))

	return res_output

def get_outlets_status(conn, options):
	outlets={}

	all_machines=vmware_vi_run_command(options,True,"--operation list")

	all_machines_array=all_machines.splitlines()

	for machine in all_machines_array:
		machine_array=machine.split("\t",1)
		if (len(machine_array)==2):
			outlets[machine_array[0]]=("",((machine_array[1].lower() in ["poweredon"]) and "on" or "off"))

	return outlets

def get_power_status(conn,options):
	outlets=get_outlets_status(conn,options)

	if (not (options["-n"] in outlets)):
		fail_usage("Failed: You have to enter existing name of virtual machine!")
	else:
		return outlets[options["-n"]][1]

def set_power_status(conn, options):
	additional_params="--operation %s --vmname '%s'"%((options["-o"]=="on" and "on" or "off"),options["-n"])

	vmware_vi_run_command(options,True,additional_params)

# Define new options
def vmware_vi_define_new_opts():
	all_opt["datacenter"]={
		"getopt":"d:",
		"help":"-d <type>      Datacenter",
		"order": 2}

def main():
	device_opt = [  "help", "version", "agent", "quiet", "verbose", "debug",
			"action", "ipaddr", "login", "passwd", "passwd_script",
			"test", "port", "separator", "datacenter" ]

	vmware_vi_define_new_opts()

	options = check_input(device_opt, process_input(device_opt))

	# Operate the fencing device
	fence_action(None, options, set_power_status, get_power_status, get_outlets_status)

if __name__ == "__main__":
	main()
