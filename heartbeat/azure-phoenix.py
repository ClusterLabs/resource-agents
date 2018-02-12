#!/usr/bin/python

import sys
import os
import logging
import subprocess

OCF_SUCCESS = 0
OCF_ERR_GENERIC = 1
OCF_ERR_UNIMPLEMENTED = 3
OCF_NOT_RUNNING = 7

OCF_RESOURCE_INSTANCE = None
PROCESS_EXEC_NAME = None
PROCESS_EXEC_ARG = None
PID_FILE = "azure-phoenix-{}.pid"

# https://stackoverflow.com/questions/32295395/how-to-get-the-process-name-by-pid-in-linux-using-python
def get_pname(id):
    p = subprocess.Popen(["ps -o cmd= {}".format(id)], stdout=subprocess.PIPE, shell=True)
    return str(p.communicate()[0]).strip()

# https://stackoverflow.com/questions/568271/how-to-check-if-there-exists-a-process-with-a-given-pid-in-python
def check_pid(pid):
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    else:
        return True

def get_pid_file():
    return PID_FILE.format(OCF_RESOURCE_INSTANCE)

def print_help():
    print("help")

def print_metadata():
    print "<?xml version=\"1.0\"?>"
    print "<!DOCTYPE resource-agent SYSTEM \"ra-api-1.dtd\">"
    print "<resource-agent name=\"foobar\">"
    print "  <version>0.1</version>"
    print "  <longdesc lang=\"en\">"
    print "This is a fictitious example resource agent written for the"
    print "OCF Resource Agent Developers Guide."
    print "  </longdesc>"
    print "  <shortdesc lang=\"en\">Example resource agent"
    print "  for budding OCF RA developers</shortdesc>"
    print "  <parameters>"
    print "    <parameter name=\"eggs\" unique=\"0\" required=\"1\">"
    print "      <longdesc lang=\"en\">"
    print "      Number of eggs, an example numeric parameter"
    print "      </longdesc>"
    print "      <shortdesc lang=\"en\">Number of eggs</shortdesc>"
    print "      <content type=\"integer\"/>"
    print "    </parameter>"
    print "    <parameter name=\"superfrobnicate\" unique=\"0\" required=\"0\">"
    print "      <longdesc lang=\"en\">"
    print "      Enable superfrobnication, an example boolean parameter"
    print "      </longdesc>"
    print "      <shortdesc lang=\"en\">Enable superfrobnication</shortdesc>"
    print "      <content type=\"boolean\" default=\"false\"/>"
    print "    </parameter>"
    print "    <parameter name=\"datadir\" unique=\"0\" required=\"1\">"
    print "      <longdesc lang=\"en\">"
    print "      Data directory, an example string parameter"
    print "      </longdesc>"
    print "      <shortdesc lang=\"en\">Data directory</shortdesc>"
    print "      <content type=\"string\"/>"
    print "    </parameter>"
    print "  </parameters>"
    print "  <actions>"
    print "    <action name=\"start\"        timeout=\"20\" />"
    print "    <action name=\"stop\"         timeout=\"20\" />"
    print "    <action name=\"monitor\"      timeout=\"20\""
    print "                                interval=\"10\" depth=\"0\" />"
    print "    <action name=\"meta-data\"    timeout=\"5\" />"
    print "  </actions>"
    print "</resource-agent>"

    return OCF_SUCCESS

def action_start():

    if (action_monitor() == OCF_SUCCESS):
        logging.info("Resource is already running")
        return OCF_SUCCESS
    
    if os.path.exists(get_pid_file()):
        os.remove(get_pid_file())

    file = open(get_pid_file(), "w")
    file.close()

    print("Starting new process. Using pid file %s" % get_pid_file())
    #p = subprocess.Popen(arg, executable="/bin/sh")
    #p = subprocess.Popen(['/bin/sh', '-c', '"sleep infinity;test"'], stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=False)
    #p = subprocess.Popen(['tailf', get_pid_file()], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    p = subprocess.Popen(PROCESS_EXEC_ARG, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    
    logging.info("process started. pid %s" % p.pid)   
    file = open(get_pid_file(), "w") 
    file.write(str(p.pid))
    file.close()

    return OCF_SUCCESS

def action_stop():

    logging.info("action_stop: Testing if resource is running")
    if (action_monitor() == OCF_NOT_RUNNING):
        logging.info("action_stop: Resource is not running")
        return OCF_SUCCESS
    
    logging.info("action_stop: Resource is running. Removing pid file")
    if os.path.exists(get_pid_file()):
        os.remove(get_pid_file())
    else:
        logging.error("action_stop: pid file does not exist")
        return OCF_ERR_GENERIC

    logging.info("action_stop: pid file removed. Testing status")
    if (action_monitor() == OCF_NOT_RUNNING):
        logging.error("action_stop: Resource is not running")
        return OCF_SUCCESS
    else:
        logging.error("action_stop: stop failed. Resource still running")
        return OCF_ERR_GENERIC

def action_monitor():

    if os.path.exists(get_pid_file()):
        logging.info("action_monitor: pid file exist")
        file = open(get_pid_file(), "r") 
        strpid = file.read()
        file.close()

        if strpid and check_pid(int(strpid)):
            logging.info("action_monitor: process with pid %s is running" % strpid)
            name = get_pname(strpid)
            logging.info("action_monitor: process with pid %s has name '%s'" % (strpid, name))

            if (name == PROCESS_EXEC_NAME):
                return OCF_SUCCESS
            else:
                logging.info("action_monitor: no process with name '%s'" % PROCESS_EXEC_NAME)
                return OCF_NOT_RUNNING
        else:
            logging.info("action_monitor: no process for pid %s" % strpid)
            return OCF_NOT_RUNNING
    else:
        logging.info("action_monitor: pid file does not exist")
        return OCF_NOT_RUNNING

def main():
    logging.basicConfig(filename='example.log',level=logging.DEBUG)
    
#     # logging.info("Env before")
#     # for name, value in os.environ.items():
#     #     logging.info("%s\t= %s" % (name, value))
# #: ${OCF_FUNCTIONS_DIR=${OCF_ROOT}/lib/heartbeat}
# #. ${OCF_FUNCTIONS_DIR}/ocf-shellfuncs
# #subprocess.call(['./test.sh'])
#     OCF_ROOT = os.environ.get("OCF_ROOT")
#     OCF_FUNCTIONS_DIR = OCF_ROOT + "/lib/heartbeat"
#     SOURCE = OCF_FUNCTIONS_DIR + "/ocf-shellfuncs"
#     logging.info("source is %s" % SOURCE)
#     proc = subprocess.Popen(['bash', '-c', 'source {} && env'.format(SOURCE)], stdout=subprocess.PIPE)
#     source_env = {tup[0].strip(): tup[1].strip() for tup in map(lambda s: s.strip().split('=', 1), proc.stdout)}

    
#     logging.info("Env after")
#     for name, value in source_env.items():
#         logging.info("%s\t= %s" % (name, value))

    global OCF_RESOURCE_INSTANCE
    global PROCESS_EXEC_NAME
    global PROCESS_EXEC_ARG

    OCF_RESOURCE_INSTANCE = os.environ.get("OCF_RESOURCE_INSTANCE")
    if not OCF_RESOURCE_INSTANCE:
        OCF_RESOURCE_INSTANCE = "unknown"
    
    PROCESS_EXEC_NAME = 'tailf {}'.format(get_pid_file())
    PROCESS_EXEC_ARG = ['tailf', get_pid_file()]

    action = None
    if len(sys.argv) > 1:
            action = sys.argv[1]
    else:
        print_help()

    logging.info("action is %s" % action)

    result = -1
    if action == "meta-data":
        result = print_metadata()
    elif action == "monitor":
        result = action_monitor()
    elif action == "stop":
        result = action_stop()
    elif action == "start":
        result = action_start()
    elif action:
        result = OCF_ERR_UNIMPLEMENTED 

    logging.info("Done %s" % result)
    sys.exit(result)

if __name__ == "__main__":
    main()
