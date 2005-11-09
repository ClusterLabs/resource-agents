import os
import string
from CommandError import CommandError
from NodeData import NodeData
from ServiceData import ServiceData
from clui_constants import *
import executil

import gettext
_ = gettext.gettext

PATH_TO_RELAXNG_FILE = "./misc/cluster.ng"

ALT_PATH_TO_RELAXNG_FILE = "/usr/share/system-config-cluster/misc/cluster.ng"

PATH_TO_CLUSTAT = "/sbin/clustat"

ALT_PATH_TO_CLUSTAT = "/usr/sbin/clustat"

PROPAGATE_ERROR=_("Propagation of configuration file version #%s failed with the following error:\n %s")

PROPAGATE_ERROR2=_("Propagation of configuration file failed with the following error:\n %s")

NODES_INFO_ERROR=_("A problem was encountered when attempting to get information about the nodes in the cluster: %s")

GULM_NODES_INFO_ERROR=_("A problem was encountered when attempting to get information about the nodes in the cluster: %s")

MODE_OFFSET = 4

STATE_OFFSET = 2

NAME_STR = "Name"

DEAD_STR=_("Dead")

MEMBER_STR=_("Member")

NAMESTR="name=\""
STATESTR="state_str=\""
OWNERSTR="owner=\""
LOWNERSTR="last_owner=\""
RESTARTSSTR="restarts=\""

class CommandHandler:

  def __init__(self):
    path_exists = os.path.exists(PATH_TO_RELAXNG_FILE)
    if path_exists == True:
      self.relaxng_file = PATH_TO_RELAXNG_FILE
    else:
      self.relaxng_file = ALT_PATH_TO_RELAXNG_FILE
    pass

  def isClusterMember(self):
    args = list()
    args.append("/sbin/magma_tool")
    args.append("quorum")
    cmdstr = ' '.join(args)
    try:
      out, err, res = executil.execWithCaptureErrorStatus("/sbin/magma_tool",args)
    except RuntimeError, e:
      return False

    if res != 0:
      return False

    #look for 'Connect Failure' substring
    lines = out.splitlines()
    for line in lines:
      val = line.find("Connect Failure")
      if val != (-1):
        return False

    return True

  def getClusterName(self):
    #Use  [root@link-08 ~]# ccs_test connect
    # Connect successful.
    #Connection descriptor = 0
    # [root@link-08 ~]# ccs_test get 0 /cluster/\@name
    #Get successful.
    # Value = <link-cluster>

    # 
    # get descriptor
    try:
      out, err, res =  executil.execWithCaptureErrorStatus('/sbin/ccs_test', ['/sbin/ccs_test', 'connect'])
    except RuntimeError, e:
      return ""
    if res != 0:
      return ""
    lines = out.splitlines()
    descr = ''
    for line in lines:
      if line.find('Connection descriptor = ') != -1:
        words = line.split('=')
        if len(words) != 2:
          return ''
        descr = words[1].strip()
        break
    if descr == '':
      return ''
    
    
    # get name
    try:
      args = ['/sbin/ccs_test', 'get', descr, '/cluster/@name']
      out, err, res =  executil.execWithCaptureErrorStatus('/sbin/ccs_test', args)
    except RuntimeError, e:
      try:
        # make sure descriptor gets disconnected
        executil.execWithCapture('/sbin/ccs_test', ['/sbin/ccs_test', 'disconnect', descr])
      except RuntimeError, e:
        pass
      return ""
    try:
      # make sure descriptor gets disconnected
      executil.execWithCapture('/sbin/ccs_test', ['/sbin/ccs_test', 'disconnect', descr])
    except RuntimeError, e:
      pass
    
    
    if res != 0:
      return ""
    lines = out.splitlines()
    for line in lines:
      if line.find('Value = ') != -1:
        words = line.split('=')
        if len(words) != 2:
          return ''
        else:
         str_with_brackets =  words[1].strip()
         str_len = len(str_with_brackets)
         sbuffer = ""
         if str_with_brackets.find('<') == 0: #the string is wrapped in brackets
           dex = 0
           for item in str_with_brackets:
             if item == '<' and dex == 0:
               dex = dex + 1
               continue
             elif item == '>' and dex == (str_len - 1):
               break
             else:
               sbuffer = sbuffer + item
               dex = dex + 1
           return sbuffer
         else:
           return str_with_brackets

    return ''


    #     args = list()
    #     args.append("/sbin/cman_tool")
    #     args.append("status")
    #     cmdstr = ' '.join(args)
    #     try:
    #       out,err,res =  rhpl.executil.execWithCaptureErrorStatus("/sbin/cman_tool",args)
    #     except RuntimeError, e:
    #       return ""
    # 
    #     if res != 0:
    #       return ""
    # 
    #     #look for Cluster Name string
    #     lines = out.splitlines()
    #     for line in lines:
    #       val = line.find("Cluster name:")
    #       if val != (-1):  #Found it
    #         v = line.find(":")
    #         return line[(v+1):].strip()
    # 
    #     return ""
  

  def getNodeName(self):
    args = list()
    args.append("/sbin/cman_tool")
    args.append("status")
    cmdstr = ' '.join(args)
    try:
      out,err,res =  executil.execWithCaptureErrorStatus("/sbin/cman_tool",args)
    except RuntimeError, e:
      return ""

    if res != 0:
      return ""

    #look for Node Name string
    lines = out.splitlines()
    for line in lines:
      val = line.find("Node name:")
      if val != (-1):  #Found it
        v = line.find(":")
        return line[(v+1):].strip()

    return ""

  def getClusterStatus(self):
    args = list()
    args.append("/sbin/cman_tool")
    args.append("status")
    cmdstr = ' '.join(args)
    try:
      out,err,res =  executil.execWithCaptureErrorStatus("/sbin/cman_tool",args)
    except RuntimeError, e:
      return False

    if res != 0:
      return False

    #look for Node Name string
    lines = out.splitlines()
    for line in lines:
      val = line.find("Membership state:")
      if val != (-1):  #Found it
        v = line.find(":")
        return line[(v+1):].strip()

    return ""

  def isClusterQuorate(self):
    args = list()
    args.append("/sbin/magma_tool")
    args.append("quorum")
    cmdstr = ' '.join(args)
    try:
      out,err,res =  executil.execWithCaptureErrorStatus("/sbin/magma_tool",args)
    except RuntimeError, e:
      return False

    if res != 0:
      return False

    #look for Quorate string
    lines = out.splitlines()
    for line in lines:
      val = line.find("Quorate")
      if val != (-1):  #Found it
        return True

    return False

  def getNodesInfo(self, locking_type ):
    dataobjs = list()
    if locking_type == DLM_TYPE:
      args = list()
      args.append("/sbin/cman_tool")
      args.append("nodes")
      cmdstr = ' '.join(args)
      try:
        out,err,res =  executil.execWithCaptureErrorStatus("/sbin/cman_tool",args)
      except RuntimeError, e:
        return dataobjs  #empty list with no nodes

      if res != 0:
        raise CommandError("FATAL", NODES_INFO_ERROR % err)

      lines = out.splitlines()
      y = (-1)
      for line in lines:
        y = y + 1
        if y == 0:
          continue
        words = line.split()
        nd = NodeData(words[1],words[3],words[4])
        dataobjs.append(nd)

      return dataobjs

    else:
      args = list()
      args.append("/sbin/gulm_tool")
      args.append("nodelist")
      args.append("localhost:core")
      cmdstr = ' '.join(args)
      try:
        out,err,res =  executil.execWithCaptureErrorStatus("/sbin/gulm_tool",args)
      except RuntimeError, e:
        return dataobjs  #empty list with no nodes

      if res != 0:
        raise CommandError("FATAL", GULM_NODES_INFO_ERROR % err)

      lines = out.splitlines()
      line_counter = (-1)
      for line in lines:
        line_counter = line_counter + 1
        dex = line.find(NAME_STR)
        if dex == (-1):
          continue
        #We now have a name line
        words = line.split() #second word is name...
        name = words[1]
        stateline = lines[line_counter + STATE_OFFSET] #The state is 2 lines down..
        modeline = lines[line_counter + MODE_OFFSET] #The mode is 4 lines down..
        sdex = stateline.find("state =")
        sdex = sdex + 8  #move past locator string
        ste = stateline[sdex:]
        if ste.strip() == "Logged in":
          statestr = MEMBER_STR
        else:
          statestr = DEAD_STR
        mwords = modeline.split()
        mode = mwords[2]
        modestate = mode + " - " + statestr
        nd = NodeData(None,modestate,name)
        dataobjs.append(nd)

      return dataobjs

  def getServicesInfo(self):

    clustat_path = ""

    if os.path.exists(PATH_TO_CLUSTAT):
      clustat_path = PATH_TO_CLUSTAT
    else:
      clustat_path = ALT_PATH_TO_CLUSTAT

    dataobjs = list()
    args = list()
    args.append(clustat_path)
    args.append("-x")
    cmdstr = ' '.join(args)
    try:
      out,err,res =  executil.execWithCaptureErrorStatus(clustat_path,args)
    except RuntimeError, e:
      return dataobjs  #empty list with no services

    if res != 0:
      raise CommandError("FATAL", NODES_INFO_ERROR % err)

    servicelist = list()
    lines = out.splitlines()
   
    y = 0 
    found_groups_tag = False
    #First, run through lines and look for "<groups>" tag
    for line in lines:
      if line.find("<groups>") != (-1):
        found_groups_tag = True
        break
      y = y + 1

    if found_groups_tag == False:
      return dataobjs  #no services visible


    #y now holds index into line list for <groups> tag
    #We need to add one more to y before beginning to start with serices
    y = y + 1

    while lines[y].find("</groups>") == (-1):
      servicelist.append(lines[y])
      y = y + 1

    #servicelist now holds all services

    for service in servicelist:
      words = service.split()

      namedex = service.find(NAMESTR)
      start = namedex + len(NAMESTR)
      name = self.extract_attr(start, service)
      
      statedex = service.find(STATESTR)
      start = statedex + len(STATESTR)
      state = self.extract_attr(start, service)
      
      ownerdex = service.find(OWNERSTR)
      start = ownerdex + len(OWNERSTR)
      owner = self.extract_attr(start, service)
      
      lownerdex = service.find(LOWNERSTR)
      start = lownerdex + len(LOWNERSTR)
      lastowner = self.extract_attr(start, service)
      
      resdex = service.find(RESTARTSSTR)
      start = resdex + len(RESTARTSSTR)
      restarts = self.extract_attr(start, service)
      
      dataobjs.append(ServiceData(name,state,owner,lastowner,restarts))

    return dataobjs

  def extract_attr(self, index, line):
    end = line.find("\"",index)
    return line[index:end]

  def propagateConfig(self, file):
    args = list()
    args.append("/sbin/ccs_tool")
    args.append("update")
    args.append(file)
    cmdstr = ' '.join(args)
    try:
      out,err,res = executil.execWithCaptureErrorStatus("/sbin/ccs_tool",args)
    except RuntimeError, e:
      raise CommandError("FATAL",PROPAGATE_ERROR2 % (err))

    if res != 0:
      raise CommandError("FATAL",PROPAGATE_ERROR2 % (err))

    return res


  def propagateCmanConfig(self, version):
    args = list()
    args.append("/sbin/cman_tool")
    args.append("version")
    args.append("-r")
    args.append(version)
    cmdstr = ' '.join(args)
    try:
      out,err,res = executil.execWithCaptureErrorStatus("/sbin/cman_tool",args)
    except RuntimeError, e:
      raise CommandError("FATAL",PROPAGATE_ERROR % (version, err))

    if res != 0:
      raise CommandError("FATAL",PROPAGATE_ERROR2 % (err))

    return res


  def check_xml(self, file):
    err = ""
    args = list()
    args.append("/usr/bin/xmllint")
    args.append("--relaxng")
    args.append(self.relaxng_file)
    args.append(file)
    try:
      out,err,res = executil.execWithCaptureErrorStatus("/usr/bin/xmllint",args)
    except RuntimeError, e:
      raise CommandError("FATAL", str(e))

    if res != 0:
      raise CommandError("FATAL", err)

