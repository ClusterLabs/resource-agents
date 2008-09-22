#!/usr/bin/python

##
## Copyright (C) 2008 Red Hat, Inc. All Rights Reserved.
##
## The Following Agent Has Been Tested On VMware ESX 3.5 and VMware Server 1.0.7
## 
#####

import sys, re, pexpect
sys.path.append("@FENCEAGENTSLIBDIR@")
from fencing import *

#BEGIN_VERSION_GENERATION
RELEASE_VERSION="New VMware Agent - test release on steroids"
REDHAT_COPYRIGHT=""
BUILD_DATE=""
#END_VERSION_GENERATION

VMWARE_COMMAND="/usr/bin/vmware-cmd"
COMMAND_PROMPT_REG="\[PEXPECT\]\$ "
COMMAND_PROMPT_NEW="[PEXPECT]\$ "

# Start comunicating after login. Prepare good environment.
def start_communication(conn, options):
	conn.sendline ("PS1='"+COMMAND_PROMPT_NEW+"'");
	conn.log_expect(options,COMMAND_PROMPT_REG,SHELL_TIMEOUT)

# Prepare command line for vmware-cmd with parameters.
def prepare_cmdline(conn,options):
	cmd_line=VMWARE_COMMAND+" -H "+options["-A"]+" -U "+options["-L"]+" -P "+options["-P"]+" '"+options["-n"]+"'"
        if options.has_key("-A"):
    		cmd_line+=" -v"
    	return cmd_line
    	
def get_power_status(conn, options):
	result = ""
	try:
		start_communication(conn,options)
		
		cmd_line=prepare_cmdline(conn,options)
            	
            	cmd_line+=" getstate"
            	
		conn.sendline(cmd_line)
		    
		conn.log_expect(options,COMMAND_PROMPT_REG,SHELL_TIMEOUT)
		status_err = re.search("vmcontrol\ error\ ([-+]?\d+)\:(.*)",conn.before.lower())
		if (status_err!=None):
			fail_usage("VMware error "+status_err.group(1)+": "+status_err.group(2))
			
		status = re.search("getstate\(\)\ =\ on",conn.before.lower())
		
		result=(status==None and "off" or "on")
		
	except pexpect.EOF:
		fail(EC_CONNECTION_LOST)
	except pexpect.TIMEOUT:
		fail(EC_TIMED_OUT)

	return result

def set_power_status(conn, options):
	try:
		start_communication(conn,options)

		cmd_line=prepare_cmdline(conn,options)
            	
            	cmd_line+=" "+(options["-o"]=="on" and "start" or "stop hard")
            	
		conn.sendline(cmd_line)
		    
		conn.log_expect(options,COMMAND_PROMPT_REG,POWER_TIMEOUT)
		
	except pexpect.EOF:
		fail(EC_CONNECTION_LOST)
	except pexpect.TIMEOUT:
		fail(EC_TIMED_OUT)
		
def main():
	device_opt = [  "help", "version", "agent", "quiet", "verbose", "debug",
			"action", "ipaddr", "login", "passwd", "passwd_script",
			"secure",  "identity_file", "test" , "vmipaddr", "vmlogin", 
			"vmpasswd", "port", "vmpasswd_script" ]

	options = check_input(device_opt, process_input(device_opt))

	## 
	## Fence agent specific defaults
	#####
        if 0 == options.has_key("-c"):
            options["-c"] = "\$ "

        if 0 == options.has_key("-A"):
            options["-A"] = "localhost"
    
    	options["-x"] = 1
	##
	## Operate the fencing device
	####
	conn = fence_login(options)
	fence_action(conn, options, set_power_status, get_power_status)

	##
	## Logout from system
	######
	conn.sendline("logout")
	conn.close()

if __name__ == "__main__":
	main()
