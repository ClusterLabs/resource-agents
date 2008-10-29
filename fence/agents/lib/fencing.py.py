#!/usr/bin/python

import sys, getopt, time, os
import pexpect, re
import telnetlib

## do not add code here.
#BEGIN_VERSION_GENERATION
RELEASE_VERSION = "New fence lib agent - test release on steroids"
REDHAT_COPYRIGHT = ""
BUILD_DATE = "March, 2008"
#END_VERSION_GENERATION

POWER_TIMEOUT = 20
SHELL_TIMEOUT = 3
LOGIN_TIMEOUT = 5

LOG_MODE_VERBOSE = 100
LOG_MODE_QUIET = 0

EC_BAD_ARGS        = 2
EC_LOGIN_DENIED    = 3
EC_CONNECTION_LOST = 4
EC_TIMED_OUT       = 5
EC_WAITING_ON      = 6
EC_WAITING_OFF     = 7
EC_STATUS          = 8

TELNET_PATH = "/usr/bin/telnet"
SSH_PATH    = "/usr/bin/ssh"
SSL_PATH    = "@FENCEAGENTSLIBDIR@/telnet_ssl"

all_opt = {
	"help"    : {
		"getopt" : "h",
		"help" : "-h             Display this help and exit",
		"order" : 54 },
	"version" : { 
		"getopt" : "V",
		"help" : "-V             Output version information and exit",
		"order" : 53 },
	"quiet"   : {
		"getopt" : "q",
		"help" : "-q             Quiet mode",
		"order" : 50 },
	"verbose" : {
		"getopt" : "v",
		"help" : "-v             Verbose mode",
		"order" : 51 },
	"debug" : {
		"getopt" : "D:",
		"help" : "-D <debugfile> Debugging to output file",
		"order" : 52 },
	"agent"   : {
		"getopt" : "",
		"help" : "",
		"order" : 1 },
	"action" : {
		"getopt" : "o:",
		"help" : "-o <action>    Action: status, reboot (default), off or on",
		"order" : 1 },
	"ipaddr" : {
		"getopt" : "a:",
		"help" : "-a <ip>        IP address or hostname of fencing device",
		"order" : 1 },
	"login" : {
		"getopt" : "l:",
		"help" : "-l <name>      Login name",
		"order" : 1 },
	"no_login" : {
		"getopt" : "",
		"help" : "",
		"order" : 1 },
	"no_password" : {
		"getopt" : "",
		"help" : "",
		"order" : 1 },
	"passwd" : {
		"getopt" : "p:",
		"help" : "-p <password>  Login password or passphrase",
		"order" : 1 },
	"passwd_script" : {
		"getopt" : "S:",
		"help" : "-S <script>    Script to run to retrieve password",
		"order" : 1 },
	"identity_file" : {
		"getopt" : "k:",
		"help" : "-k <filename>  Identity file (private key) for ssh ",
		"order" : 1 },
	"module_name" : {
		"getopt" : "m:",
		"help" : "-m <module>    DRAC/MC module name",
		"order" : 1 },
	"drac_version" : {
		"getopt" : "d:",
		"help" : "-D <version>   Force DRAC version to use",
		"order" : 1 },
	"ribcl" : {
		"getopt" : "r:",
		"help" : "-r <version>   Force ribcl version to use",
		"order" : 1 },
	"cmd_prompt" : {
		"getopt" : "c:",
		"help" : "-c <prompt>    Force command prompt",
		"order" : 1 },
	"secure" : {
		"getopt" : "x",
		"help" : "-x             Use ssh connection",
		"order" : 1 },
	"ssl" : {
		"getopt" : "z",
		"help" : "-z             Use ssl connection",
		"order" : 1 },
	"port" : {
		"getopt" : "n:",
		"help" : "-n <id>        Physical plug number on device or name of virtual machine",
		"order" : 1 },
	"switch" : {
		"getopt" : "s:",
		"help" : "-s <id>        Physical switch number on device",
		"order" : 1 },
	"partition" : {
		"getopt" : "n:",
		"help" : "-n <id>        Name of the partition",
		"order" : 1 },
	"managed" : {
		"getopt" : "s:",
		"help" : "-s <id>        Name of the managed system",
		"order" : 1 },
	"test" : {
		"getopt" : "T",
		"help" : "",
		"order" : 1,
		"obsolete" : "use -o status instead" },
	"vmipaddr" : {
		"getopt" : "A:",
		"help" : "-A <ip>        IP address or hostname of managed VMware ESX (default localhost)",
		"order" : 2 },
	"vmlogin" : {
		"getopt" : "L:",
		"help" : "-L <name>      VMware ESX management login name",
		"order" : 2 },
	"vmpasswd" : {
		"getopt" : "P:",
		"help" : "-P <password>  VMware ESX management login password",
		"order" : 2 },
	"vmpasswd_script" : {
		"getopt" : "B:",
		"help" : "-B <script>    Script to run to retrieve VMware ESX management password",
		"order" : 2 },
	"separator" : {
		"getopt" : "C:",
		"help" : "-C <char>      Separator for CSV created by 'list' operation",
		"order" : 100 }
}

class fspawn(pexpect.spawn):
	def log_expect(self, options, pattern, timeout):
		result = self.expect(pattern, timeout)
		if options["log"] >= LOG_MODE_VERBOSE:
			options["debug_fh"].write(self.before + self.after)
		return result

def version(command, release, build_date, copyright_notice):
	print command, " ", release, " ", build_date
	if len(copyright_notice) > 0:
		print copyright_notice

def fail_usage(message = ""):
	if len(message) > 0:
		sys.stderr.write(message+"\n")
	sys.stderr.write("Please use '-h' for usage\n")
	sys.exit(EC_BAD_ARGS)

def fail(error_code):
	message = {
		EC_LOGIN_DENIED : "Unable to connect/login to fencing device",
		EC_CONNECTION_LOST : "Connection lost",
		EC_TIMED_OUT : "Connection timed out",
		EC_WAITING_ON : "Failed: Timed out waiting to power ON",
		EC_WAITING_OFF : "Failed: Timed out waiting to power OFF",
		EC_STATUS : "Failed: Unable to obtain correct plug status"
	}[error_code] + "\n"
	sys.stderr.write(message)
	sys.exit(error_code)

def usage(avail_opt):
	global all_opt

	print "Usage:"
	print "\t" + os.path.basename(sys.argv[0]) + " [options]"
	print "Options:"

	sorted_list = [ (key, all_opt[key]) for key in avail_opt ]
	sorted_list.sort(lambda x, y: cmp(x[1]["order"], y[1]["order"]))

	for key, value in sorted_list:
		if len(value["help"]) != 0:
			print "   " + value["help"]

def process_input(avail_opt):
	global all_opt

	##
	## Set standard environment
	#####
	os.putenv("LANG", "C")
	os.putenv("LC_ALL", "C")

	##
	## Prepare list of options for getopt
	#####
	getopt_string = ""
	for k in avail_opt:
		if all_opt.has_key(k):
			getopt_string += all_opt[k]["getopt"]
		else:
			fail_usage("Parse error: unknown option '"+k+"'")

	##
	## Read options from command line or standard input
	#####
	if len(sys.argv) > 1:
		try:
			opt, args = getopt.gnu_getopt(sys.argv[1:], getopt_string)
		except getopt.GetoptError, error:
			fail_usage("Parse error: " + error.msg)

		## Compatibility Layer
		#####
		z = dict(opt)
		if z.has_key("-T") == 1:
			z["-o"] = "status"

		opt = z
		##
		#####
	else:
		opt = { }
		name = ""
		for line in sys.stdin.readlines():
			line = line.strip()
			if ((line.startswith("#")) or (len(line) == 0)):
				continue

			(name, value) = (line + "=").split("=", 1)
			value = value[:-1]

			## Compatibility Layer
			######
			if name == "blade":
				name = "port"
			elif name == "option":
				name = "action"
			elif name == "fm":
				name = "port"
			elif name == "hostname":
				name = "ipaddr"

			##
			######
			if avail_opt.count(name) == 0:
				fail_usage("Parse error: Unknown option '"+line+"'")

			if all_opt[name]["getopt"].endswith(":"):
				opt["-"+all_opt[name]["getopt"].rstrip(":")] = value
			elif ((value == "1") or (value.lower() == "yes")):
				opt["-"+all_opt[name]["getopt"]] = "1"
	return opt

##
## This function checks input and answers if we want to have same answers 
## in each of the fencing agents. It looks for possible errors and run
## password script to set a correct password
######
def check_input(device_opt, opt):
	options = dict(opt)
	options["device_opt"] = device_opt

	if options.has_key("-h"): 
		usage(device_opt)
		sys.exit(0)

	if options.has_key("-V"):
		print RELEASE_VERSION, BUILD_DATE
		print REDHAT_COPYRIGHT
		sys.exit(0)

	if options.has_key("-v"):
		options["log"] = LOG_MODE_VERBOSE
	else:
		options["log"] = LOG_MODE_QUIET

	if 0 == options.has_key("-o"):
		options["-o"] = "reboot"

	# Convert action to lowercase
	options["-o"]=options["-o"].lower()

	if 0 == ["on", "off", "reboot", "status", "list", "monitor"].count(options["-o"].lower()):
		fail_usage("Failed: Unrecognised action '" + options["-o"] + "'")

	if (0 == options.has_key("-l")) and device_opt.count("login") and (device_opt.count("no_login") == 0):
		fail_usage("Failed: You have to set login name")

	if 0 == options.has_key("-a"):
		fail_usage("Failed: You have to enter fence address")

	if (device_opt.count("no_password") == 0):
		if 0 == device_opt.count("identity_file"):
			if 0 == (options.has_key("-p") or options.has_key("-S")):
				fail_usage("Failed: You have to enter password or password script")
			else: 
				if 0 == (options.has_key("-p") or options.has_key("-S") or options.has_key("-k")):
					fail_usage("Failed: You have to enter password, password script or identity file")

	if 0 == options.has_key("-x") and 1 == options.has_key("-k"):
		fail_usage("Failed: You have to use identity file together with ssh connection (-x)")

	if 1 == options.has_key("-k"):
		if 0 == os.path.isfile(options["-k"]):
			fail_usage("Failed: Identity file " + options["-k"] + " does not exist")

	if (0 == ["list", "monitor"].count(options["-o"].lower())) and (0 == options.has_key("-n")) and (device_opt.count("port")):
		fail_usage("Failed: You have to enter plug number")

	if options.has_key("-S"):
		options["-p"] = os.popen(options["-S"]).read().rstrip()

	if options.has_key("-D"):
		try:
			options["debug_fh"] = file (options["-D"], "w")
		except IOError:
			fail_usage("Failed: Unable to create file "+options["-D"])

	if options.has_key("-v") and options.has_key("debug_fh") == 0:
		options["debug_fh"] = sys.stderr

	if 0 == options.has_key("-C"):
		options["-C"] = ","

	## VMware
	#######
	if options.has_key("-B"):
		options["-P"] = os.popen(options["-B"]).read().rstrip()

	if (device_opt.count("vmlogin") and (not options.has_key("-L"))):
		fail_usage("Failed: You have to set login name for VMware ESX management console")

	if (options.has_key("-L") and (not (options.has_key("-P") or options.has_key("-B")))):
		fail_usage("Failed: You have to enter password or password script for VMware ESX management console")

	if (["list", "monitor"].count(options["-o"])==0 and (options.has_key("-L") and (not (options.has_key("-n"))))):
		fail_usage("Failed: You have to enter virtual machine name")

	return options
	
def wait_power_status(tn, options, get_power_fn):
	for dummy in xrange(POWER_TIMEOUT):
		if get_power_fn(tn, options) != options["-o"]:
			time.sleep(1)
		else:
			return 1
	return 0

def fence_action(tn, options, set_power_fn, get_power_fn, get_outlet_list = None):
	if (options["-o"] == "list" and 0 == options["device_opt"].count("port")):
		print "N/A"
		return
	elif (options["-o"] == "list" and get_outlet_list == None):
		## @todo: exception?
		## This is just temporal solution, we will remove default value
		## None as soon as all existing agent will support this operation 
		print "NOTICE: List option is not working on this device yet"
		return
	elif (options["-o"] == "list") or ((options["-o"] == "monitor") and 1 == options["device_opt"].count("port")):
		outlets = get_outlet_list(tn, options)
		## keys can be numbers (port numbers) or strings (names of VM)
		for o in outlets.keys():
			(alias, status) = outlets[o]
			if options["-o"] != "monitor":
				print o + options["-C"] + alias	
		return

	status = get_power_fn(tn, options)

	if status != "on" and status != "off":  
		fail(EC_STATUS)

	if options["-o"] == "on":
		if status == "on":
			print "Success: Already ON"
		else:
			set_power_fn(tn, options)
			if wait_power_status(tn, options, get_power_fn):
				print "Success: Powered ON"
			else:
				fail(EC_WAITING_ON)
	elif options["-o"] == "off":
		if status == "off":
			print "Success: Already OFF"
		else:
			set_power_fn(tn, options)
			if wait_power_status(tn, options, get_power_fn):
				print "Success: Powered OFF"
			else:
				fail(EC_WAITING_OFF)
	elif options["-o"] == "reboot":
		if status != "off":
			options["-o"] = "off"
			set_power_fn(tn, options)
			if wait_power_status(tn, options, get_power_fn) == 0:
				fail(EC_WAITING_OFF)
		options["-o"] = "on"
		set_power_fn(tn, options)
		if wait_power_status(tn, options, get_power_fn) == 0:
			sys.stderr.write('Timed out waiting to power ON\n')
		print "Success: Rebooted"
	elif options["-o"] == "status":
		print "Status: " + status.upper()
	elif options["-o"] == "monitor":
		1

def fence_login(options):
	try:
		re_login = re.compile("(login: )|(Login Name:  )|(username: )|(User Name :)", re.IGNORECASE)
		re_pass  = re.compile("password", re.IGNORECASE)

		if options.has_key("-z"):
			command = '%s %s %s' % (SSL_PATH, options["-a"], "443")
			conn = fspawn(command)
		elif options.has_key("-x") and 0 == options.has_key("-k"):
			command = '%s %s@%s' % (SSH_PATH, options["-l"], options["-a"])
			if options.has_key("ssh_options"):
				command += ' ' + options["ssh_options"]
			conn = fspawn(command)

			if options.has_key("telnet_over_ssh"):
				#This is for stupid ssh servers (like ALOM) which behave more like telnet (ignore name and display login prompt)
				result = conn.log_expect(options, [ re_login, "Are you sure you want to continue connecting (yes/no)?" ], LOGIN_TIMEOUT)
				if result == 1:
					conn.sendline("yes") # Host identity confirm
					conn.log_expect(options, re_login, LOGIN_TIMEOUT)

				conn.sendline(options["-l"])
				conn.log_expect(options, re_pass, LOGIN_TIMEOUT)
			else:
				result = conn.log_expect(options, [ "ssword:", "Are you sure you want to continue connecting (yes/no)?" ], LOGIN_TIMEOUT)
				if result == 1:
					conn.sendline("yes")
					conn.log_expect(options, "ssword:", LOGIN_TIMEOUT)

			conn.sendline(options["-p"])
			conn.log_expect(options, options["-c"], LOGIN_TIMEOUT)
		elif options.has_key("-x") and 1 == options.has_key("-k"):
			conn = fspawn('%s %s@%s -i %s' % (SSH_PATH, options["-l"], options["-a"], options["-k"]))
			result = conn.log_expect(options, [ options["-c"], "Are you sure you want to continue connecting (yes/no)?", "Enter passphrase for key '"+options["-k"]+"':" ], LOGIN_TIMEOUT)
			if result == 1:
				conn.sendline("yes")
				conn.log_expect(options, [ options["-c"], "Enter passphrase for key '"+options["-k"]+"':"] , LOGIN_TIMEOUT)
			if result != 0:
				if options.has_key("-p"):
					conn.sendline(options["-p"])
					conn.log_expect(options, options["-c"], LOGIN_TIMEOUT)
				else:
					fail_usage("Failed: You have to enter passphrase (-p) for identity file")
		else:
			conn = fspawn('%s %s' % (TELNET_PATH, options["-a"]))
			conn.log_expect(options, re_login, LOGIN_TIMEOUT)
			conn.send(options["-l"]+"\r\n")
			conn.log_expect(options, re_pass, SHELL_TIMEOUT)
			conn.send(options["-p"]+"\r\n")
			conn.log_expect(options, options["-c"], SHELL_TIMEOUT)
	except pexpect.EOF:
		fail(EC_LOGIN_DENIED) 
	except pexpect.TIMEOUT:
		fail(EC_LOGIN_DENIED)
	return conn
