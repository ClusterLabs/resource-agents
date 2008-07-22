#!/usr/bin/python

#############################################################################
## This APC Fence script uses snmp to control the APC power
## switch. This script requires that net-snmp-utils be installed
## on all nodes in the cluster, and that the powernet369.mib file be
## located in @MIBDIR@
#############################################################################

import getopt, sys
import os
import datetime
import select
import signal
from glob import glob

#BEGIN_VERSION_GENERATION
RELEASE_VERSION=""
REDHAT_COPYRIGHT=""
BUILD_DATE=""
#END_VERSION_GENERATION

POWER_ON="outletOn"
POWER_OFF="outletOff"
POWER_REBOOT="outletReboot"


# oid defining fence device 
oid_sysObjectID = '.1.3.6.1.2.1.1.2.0'



class SNMP:
	def __init__(self, params):
		self.hostname  = params['ipaddr']
		self.udpport   = params['udpport']
		self.community = params['community']
	
	def get(self, oid):
		args = ['@SNMPBIN@/snmpget']
		args.append('-Oqn')
		args.append('-v')
		args.append('1')
		args.append('-c')
		args.append(self.community)
		args.append('-m')
		args.append('ALL')
		args.append(self.hostname + ':' + self.udpport)
		args.append(oid)
		strr, code = execWithCaptureStatus("@SNMPBIN@/snmpget", args)
		if code:
			raise Exception, 'snmpget failed'
		l = strr.strip().split()
		return l[0], ' '.join(l[1:])
	
	def set_int(self, oid, value):
		args = ['@SNMPBIN@/snmpset']
		args.append('-Oqn')
		args.append('-v')
		args.append('1')
		args.append('-c')
		args.append(self.community)
		args.append('-m')
		args.append('ALL')
		args.append(self.hostname + ':' + self.udpport)
		args.append(oid)
		args.append('i')
		args.append(str(value))
		strr,code = execWithCaptureStatus("@SNMPBIN@/snmpset", args)
		if code:
			raise Exception, 'snmpset failed'
		
	def walk(self, oid):
		args = ['@SNMPBIN@/snmpwalk']
		args.append('-Oqn')
		args.append('-v')
		args.append('1')
		args.append('-c')
		args.append(self.community)
		args.append('-m')
		args.append('ALL')
		args.append(self.hostname + ':' + self.udpport)
		args.append(oid)
		strr,code = execWithCaptureStatus("@SNMPBIN@/snmpwalk", args)
		if code:
			raise Exception, 'snmpwalk failed'
		lines = strr.strip().splitlines()
		ret = []
		for line in lines:
			l = line.strip().split()
			ret.append((l[0], ' '.join(l[1:]).strip('"')))
		return ret
	


class FenceAgent:
	
	def __init__(self, params):
	   self.snmp = SNMP(params)
	
	def resolve_outlet(self):
		raise Exception, 'resolve_outlet() not implemented'
	
	def status(self):
		oid = self.status_oid % self.resolve_outlet()
		dummy, stat = self.snmp.get(oid)
		if stat == self.state_on or stat == "outletStatusOn":
			return 'on'
		elif stat == self.state_off or stat == "outletStatusOff":
			return 'off'
		else:
			raise Exception, 'invalid status ' + stat
	
	def power_off(self):
		oid = self.control_oid % self.resolve_outlet()
		self.snmp.set_int(oid, self.turn_off)
	
	def power_on(self):
		oid = self.control_oid % self.resolve_outlet()
		self.snmp.set_int(oid, self.turn_on)
	



		

		

class MasterSwitch(FenceAgent):
	
	def __init__(self, params):
	   FenceAgent.__init__(self, params)
	   
	   self.status_oid       = '.1.3.6.1.4.1.318.1.1.12.3.5.1.1.4.%s'
	   self.control_oid      = '.1.3.6.1.4.1.318.1.1.12.3.3.1.1.4.%s'
	   self.outlet_table_oid = '.1.3.6.1.4.1.318.1.1.12.3.5.1.1.2'
	   
	   self.state_on  = '1'
	   self.state_off = '2'
	   
	   self.turn_on   = '1'
	   self.turn_off  = '2'
	   
	   self.port = params['port']
	
	def resolve_outlet(self):
		outlet = None
		try:
			outlet = str(int(self.port))
		except:
			table = self.snmp.walk(self.outlet_table_oid)
			for row in table:
				if row[1] == self.port:
					t = row[0].strip().split('.')
					outlet = t[len(t)-1]
		if outlet == None:
			raise Exception, 'unable to resolve ' + self.port
		else:
			self.port = outlet
		return outlet
	
	
class MasterSwitchPlus(FenceAgent):
	def __init__(self, params):
	   FenceAgent.__init__(self, params)
	   
	   self.status_oid       = '.1.3.6.1.4.1.318.1.1.6.7.1.1.5.%s.1.%s'
	   self.control_oid      = '.1.3.6.1.4.1.318.1.1.6.5.1.1.5.%s.1.%s'
	   self.outlet_table_oid = '.1.3.6.1.4.1.318.1.1.6.7.1.1.4'
	   
	   self.state_on  = '1'
	   self.state_off = '2'
	   
	   self.turn_on   = '1'
	   self.turn_off  = '3'
	   
	   try:
		   self.switch = params['switch']
	   except:
		   self.switch = ''
	   self.port   = params['port']
   
	def resolve_outlet(self):
		switch = None
		outlet = None
		try:
			switch = str(int(self.switch))
			outlet = str(int(self.port))
		except:
			table = self.snmp.walk(self.outlet_table_oid)
			for row in table:
				if row[1] == self.port:
					t = row[0].strip().split('.')
					outlet = t[len(t)-1]
					switch = t[len(t)-3]
		if outlet == None:
			raise Exception, 'unable to resolve ' + self.port
		else:
			self.switch = switch
			self.port   = outlet
		return (switch, outlet)
	



	

def usage():
        print "Usage:"
        print ""
        print "Options:"
        print "  -h               Usage"
        print "  -a <ip>          IP address or hostname of fence device"
        print "  -u <udpport>     UDP port to use (default 161)"
        print "  -c <community>   SNMP community (default 'private')"
        print "  -n <num>         Outlet name/number to act on"
        print "  -o <string>      Action: Reboot (default), On, Off and Status"
        print "  -v               Verbose mode - write to /tmp/apclog"
        print "  -V               Version"
	
        sys.exit(0)



file_log = None
def set_logging(verbose):
	global file_log
	if verbose:
		file_log = open('/tmp/apclog', 'a')
		file_log.write('\n-----------  ')
		file_log.write(datetime.datetime.today().ctime())
		file_log.write('  -----------\n')
def log(msg, error=False):
	global file_log
	if msg.rfind('\n') != len(msg)-1:
		msg += '\n'
	if file_log != None:
		file_log.write(msg)
	if error:
		o = sys.stderr
	else:
		o = sys.stdout
	o.write(msg)



def main():
	try:
		main2()
		return 0
	except Exception, e:
		log(str(e), True)
		sys.exit(1)
def main2():
  
  agents_dir = {'.1.3.6.1.4.1.318.1.3.4.5' : MasterSwitch,
		'.1.3.6.1.4.1.318.1.3.4.4' : MasterSwitchPlus}
  
  verbose = False
  params = {}
  
  if len(sys.argv) > 1:
    try:
      opts, args = getopt.getopt(sys.argv[1:], "ha:u:c:n:o:vV", ["help", "output="])
    except getopt.GetoptError:
      usage()
      sys.exit(2)

    for o, a in opts:
      o = o.strip()
      a = a.strip()
      if o == "-v":
        verbose = True
      if o == "-V":
        print "%s\n" % RELEASE_VERSION
        print "%s\n" % REDHAT_COPYRIGHT
        print "%s\n" % BUILD_DATE
        sys.exit(0)
      if o in ("-h", "--help"):
        usage()
	sys.exit(0)
      if o == "-a":
        params['ipaddr'] = a
      if o == "-u":
        params['udpport'] = a
      if o == "-c":
        params['community'] = a
      if o == "-n":
        switch = ''
	port   = a
	if ':' in port:
	   idx = port.find(':')
	   switch = port[:idx]
	   port = port[idx+1:]
	params['switch'] = switch
	params['port']   = port
      if o == "-o":
        params['option'] = a.lower()

  else: #Get opts from stdin 
    for line in sys.stdin:
      val = line.strip().split("=")
      if len(val) == 2:
         o = val[0].strip().lower()
	 a = val[1].strip()
	 if o == 'verbose':
	    if a.lower() == 'on' or a.lower() == 'true' or a == '1':
	       verbose = True
	 else:
	    params[o] = a 
	
    
  set_logging(verbose)
  
  
  ### validation ###
  
  try:
	  if params['ipaddr'] == '':
		  raise Exception, 'missing ipadddr'
  except:
	  log("FENCE: Missing ipaddr param for fence_apc_snmp...exiting", True)
	  sys.exit(1)
  if 'udpport' not in params:
	  params['udpport'] = '161'
  try:
	  t = int(params['udpport'])
	  if t >= 65536 or t < 0:
		  raise Exception, 'invalid udpport'
  except:
	  log("FENCE: Invalid udpport for fence_apc_snmp...exiting", True)
	  sys.exit(1)
  if 'community' not in params:
	  params['community'] = 'private'
  try:
	  port = params['port']
	  if len(port) == 0:
		  raise Exception, 'missing port'
  except:
	  log("FENCE: Missing port param for fence_apc_snmp...exiting", True)
	  sys.exit(1)
  if 'switch' not in params:
	  params['switch'] = ''
  try:
	  act = params['option'].lower()
	  if act in ['on', 'off', 'reboot', 'status']:
		  params['option'] = act
	  else:
		  usage()
		  sys.exit(3)
  except:
	  params['option'] = 'reboot'
	  
  ### End of validation ###

  if verbose:
     log('called with ' + str(params))
  
  agent = None
  dummy, sys_id = SNMP(params).get(oid_sysObjectID)
  if sys_id not in agents_dir:
     log('Fence device with \'oid_sysObjectID=' + sys_id + '\' is not supported', True)
     sys.exit(1)
  agent = agents_dir[sys_id](params)
  
  if params['option'] == 'status':
	  log('Outlet "%s" - %s is %s' % (params['port'], 
					  str(agent.resolve_outlet()),
					  agent.status()))
  elif params['option'] == 'on':
	  agent.power_on()
	  if agent.status() != 'on':
		  raise Exception, 'Error turning outlet on'
  elif params['option'] == 'off':
	  agent.power_off()
	  if agent.status() != 'off':
		  raise Exception, 'Error turning outlet off'
  elif params['option'] == 'reboot':
	  agent.power_off()
	  if agent.status() != 'off':
		  raise Exception, 'Error turning outlet off'
	  agent.power_on()
	  if agent.status() != 'on':
		  raise Exception, 'Error turning outlet on'
  else:
	  print 'nothing to do'
	  sys.exit(1)
	  pass
  

  
def execWithCaptureStatus(command, argv, searchPath = 0, root = '/', stdin = 0,
			  catchfd = 1, closefd = -1):
	
    if not os.access (root + command, os.X_OK):
        raise Exception, command + " cannot be run"
    
    (read, write) = os.pipe()
    
    childpid = os.fork()
    if (not childpid):
        if (root and root != '/'): os.chroot (root)
        if isinstance(catchfd, tuple):
            for fd in catchfd:
                os.dup2(write, fd)
        else:
            os.dup2(write, catchfd)
        os.close(write)
        os.close(read)
	
        if closefd != -1:
            os.close(closefd)
	
        if stdin:
            os.dup2(stdin, 0)
            os.close(stdin)
	
        if (searchPath):
            os.execvp(command, argv)
        else:
            os.execv(command, argv)
	
        sys.exit(1)
    
    os.close(write)
    
    rc = ""
    s = "1"
    while (s):
        select.select([read], [], [])
        s = os.read(read, 1000)
        rc = rc + s
    
    os.close(read)
    
    try:
        (pid, status) = os.waitpid(childpid, 0)
    except OSError, (errno, msg):
        print __name__, "waitpid:", msg
    
    if os.WIFEXITED(status) and (os.WEXITSTATUS(status) == 0):
        status = os.WEXITSTATUS(status)
    else:
        status = -1
    
    return (rc, status)

if __name__ == "__main__":
	ret = main()
	sys.exit(ret)
