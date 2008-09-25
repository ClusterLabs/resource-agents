#!/usr/bin/python

##
## The Following Agent Has Been Tested On - LDOM 1.0.3
## The interface is backward compatible so it will work 
## with 1.0, 1.0.1 and .2 too.
## 
#####

import sys, re, pexpect
sys.path.append("@FENCEAGENTSLIBDIR@")
from fencing import *

#BEGIN_VERSION_GENERATION
RELEASE_VERSION="Logical Domains (LDoms) fence Agent"
REDHAT_COPYRIGHT=""
BUILD_DATE=""
#END_VERSION_GENERATION

COMMAND_PROMPT_REG="\[PEXPECT\]$"
COMMAND_PROMPT_NEW="[PEXPECT]"

# Start comunicating after login. Prepare good environment.
def start_communication(conn, options):
	conn.sendline ("PS1='"+COMMAND_PROMPT_NEW+"'")
	res=conn.expect([pexpect.TIMEOUT, COMMAND_PROMPT_REG],SHELL_TIMEOUT)
	if res==0:
		#CSH stuff
		conn.sendline("set prompt='"+COMMAND_PROMPT_NEW+"'")
		conn.log_expect(options, COMMAND_PROMPT_REG,SHELL_TIMEOUT)
	

def get_power_status(conn, options):
	result = ""
	try:
		start_communication(conn,options)
		
		conn.sendline("ldm ls")
		    
		conn.log_expect(options,COMMAND_PROMPT_REG,SHELL_TIMEOUT)
		#Status of logical domain. This can be None => LM not found or something else	
		ldom_exists = re.search(re.escape(options["-n"])+"\s+(\w+)",conn.before)
		if (ldom_exists==None):
			fail_usage("Failed: You have to enter existing logical domain!")
		#Test status
		status=re.search(".*bound",ldom_exists.group(1).lower())

		result=(status!=None and "off" or "on")
		
	except pexpect.EOF:
		fail(EC_CONNECTION_LOST)
	except pexpect.TIMEOUT:
		fail(EC_TIMED_OUT)

	return result

def set_power_status(conn, options):
	try:
		start_communication(conn,options)
         	
		cmd_line="ldm "+(options["-o"]=="on" and "start" or "stop -f")+" \""+options["-n"]+"\""
            	
		conn.sendline(cmd_line)
		    
		conn.log_expect(options,COMMAND_PROMPT_REG,POWER_TIMEOUT)
		
	except pexpect.EOF:
		fail(EC_CONNECTION_LOST)
	except pexpect.TIMEOUT:
		fail(EC_TIMED_OUT)
		
def main():
	device_opt = [  "help", "version", "agent", "quiet", "verbose", "debug",
			"action", "ipaddr", "login", "passwd", "passwd_script",
			"secure",  "identity_file", "test" , "port", "cmd_prompt" ]

    	
	options = check_input(device_opt, process_input(device_opt))

	## 
	## Fence agent specific defaults
	#####
	if (not options.has_key("-c")):
		options["-c"] = "\ $"
	

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
