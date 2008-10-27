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
	try:
		start_communication(conn,options)
		
		conn.sendline("ldm ls")
		    
		conn.log_expect(options,COMMAND_PROMPT_REG,SHELL_TIMEOUT)

		result={}

		#This is status of mini finite automata. 0 = we didn't found NAME and STATE, 1 = we did
		fa_status=0
		
		for line in conn.before.splitlines():
			domain=re.search("^(\S+)\s+(\S+)\s+.*$",line)

			if (domain!=None):
				if ((fa_status==0) and (domain.group(1)=="NAME") and (domain.group(2)=="STATE")):
					fa_status=1
				elif (fa_status==1):
					result[domain.group(1)]=("",(domain.group(2).lower()=="bound" and "off" or "on"))

	except pexpect.EOF:
		fail(EC_CONNECTION_LOST)
	except pexpect.TIMEOUT:
		fail(EC_TIMED_OUT)

	if (options["-o"] == "status"):
		if (not (options["-n"] in result)):
			fail_usage("Failed: You have to enter existing logical domain!")
		else:
			return result[options["-n"]][1]
	else:
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
			"secure",  "identity_file", "test" , "port", "cmd_prompt",
			"separator" ]

    	
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
	fence_action(conn, options, set_power_status, get_power_status,get_power_status)

	##
	## Logout from system
	######
	conn.sendline("logout")
	conn.close()

if __name__ == "__main__":
	main()
