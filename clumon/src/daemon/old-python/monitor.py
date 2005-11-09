
import threading
import time
import os
import copy

import xml.dom
from xml.dom import minidom


from monitor_objects import *

from CommandHandler import CommandHandler

from Communicator import Communicator, getAddresses

from executil import execWithCaptureErrorStatus


CLUSTER_CONF='/etc/cluster/cluster.conf'


CLUSTER_PTR_STR="cluster"
CLUSTERNODES_PTR_STR="clusternodes"
CLUSTERNODE_PTR_STR="clusternode"
FAILDOMS_PTR_STR="failoverdomains"
FENCEDEVICES_PTR_STR="fencedevices"
RESOURCEMANAGER_PTR_STR="rm"
RESOURCES_PTR_STR="resources"
FENCEDAEMON_PTR_STR="fence_daemon"
SERVICE="service"
GULM_TAG_STR="gulm"
MCAST_STR="multicast"
CMAN_PTR_STR="cman"



class Monitor(threading.Thread):
    def __init__(self, comm_port):
        threading.Thread.__init__(self)
        
        self.mutex = threading.RLock()
        self.c_h = CommandHandler()
        
        self.cluster = None
        
        self.mynode = self.myNodeName()
        
        self.comm = Communicator(self.dataArrived, [], self.mynode, comm_port)
        
        self.cache = {}
        
        self.__stop = False
        
    
    ### reporting part ###
    
    def request(self, req):
        print req
        
        comm = ''
        
        if req == 'GET':
            self.mutex.acquire()
            cluster = copy.deepcopy(self.cluster)
            self.mutex.release()
            
            try:
                if cluster != None:
                    comm += cluster.getData() + '\n'
                    for node in cluster.nodes:
                        comm += node.getData() + '\n'
                    for service in cluster.services:
                        comm += service.getData() + '\n'
                comm += '\n'
            except:
                pass
            
        return comm
    
    
    ### monitoring part ###
    
    def dataArrived(self, msg):
        msg = msg.strip()
        doc = None
        try:
            doc = minidom.parseString(msg)
        except:
            return
        first = doc.firstChild
        if first.nodeName != 'msg':
            return
        attrs_dir = {}
        attrs = first.attributes
        for attrName in attrs.keys():
            attrNode = attrs.get(attrName)
            attrValue = attrNode.nodeValue
            attrs_dir[attrName.strip()] = attrValue.strip()
        if 'type' not in attrs_dir:
            return
        
        if attrs_dir['type'] == 'clusterupdate':
            if 'node' not in attrs_dir:
                return
            self.mutex.acquire()
            self.cache[attrs_dir['node']] = (time.time(), msg)
            self.mutex.release()
        else:
            return
        #print self.cache
        
    
    def run(self):
        self.comm.start()
        
        stop = False
        while stop != True:
            # get local info
            info, clustername, nodes = self.getLocalInfo()
            
            # publish it
            self.comm.updateNodes(nodes)
            self.comm.send(info)
            
            # merge data from all nodes (removing stale entries)
            cluster = self.merge()
            
            # update self.cluster
            self.mutex.acquire()
            stop = self.__stop
            self.cluster = cluster
            self.mutex.release()
            
            # wait some time
            time.sleep(5)
        
        self.comm.stop()
        
    def stop(self):
        self.mutex.acquire()
        self.__stop = True
        self.mutex.release()
    
    def getLocalInfo(self):
        cluster = self.parseClusterConf()
        nodenames = []
        for node in cluster.nodes:
            nodenames.append(node.name)
        
        clustered = self.c_h.isClusterMember()
        
        ### build info tree ###
        
        # cluster
        c = Object('cluster')
        c.set('name', cluster.name)
        c.set('version', cluster.version)
        if cluster.minQuorum != None:
            c.set('minQuorum', str(cluster.minQuorum))
        
        # nodes
        ns = Object('objects')
        ns.set('name', 'nodes')
        c.addChild(ns)
        for node in cluster.nodes:
            n = Object('node')
            n.set('name', node.name)
            ns.addChild(n)
            n.set('votes', str(node.votes))
            if node.name == self.mynode:
                n.set('running', 'true')
                if clustered:
                    n.set('clustered', 'true')
                else:
                    n.set('clustered', 'false')
        
        # services
        ss = Object('objects')
        ss.set('name', 'services')
        c.addChild(ss)
        for service in cluster.services:
            s = Object('service')
            s.set('name', service.name)
            ss.addChild(s)
            if service.autostart:
                s.set('autostart', 'true')
            else:
                s.set('autostart', 'false')
        
        # if part of cluster, insert current data
        if clustered:
            nodesinfo = self.c_h.getNodesInfo(cluster.locking)
            for n in ns.getChildren().values():
                for ni in nodesinfo:
                    if n.get('name') == ni.name:
                        n.set('running', 'true')
                        if ni.status.upper() == 'MEMBER' or ni.status.upper() == 'JOIN':
                            n.set('clustered', 'true')
            
            if self.c_h.isClusterQuorate():
                servicesinfo = self.c_h.getServicesInfo()
                for s in ss.getChildren().values():
                    for si in servicesinfo:
                        if s.get('name') == si.name:
                            if 'START' in si.state.upper():
                                s.set('running', 'true')
                                s.set('nodename', si.owner)
                            else:
                                s.set('running', 'false')
                                if 'FAIL' in si.state.upper():
                                    s.set('failed', 'true')
                                else:
                                    s.set('failed', 'false')
        
        
        ### construct message ###
        
        msg = Object('msg')
        msg.set('type', 'clusterupdate')
        msg.set('node', self.mynode)
        msg.addChild(c)
        
        doc = minidom.Document()
        msg.generateXML(doc)
        info = doc.toprettyxml() + '\n\n'
        
        return info, cluster.name, nodenames
    
    def merge(self):
        ### remove stale entries ###
        self.mutex.acquire()
        rems = []
        for node in self.cache:
            if self.cache[node][0] < time.time() - 8:
                rems.append(node)
        for node in rems:
            self.cache.pop(node)
        cache = copy.deepcopy(self.cache)
        self.mutex.release()
        
        ### build cluster views from cache ###
        c_views = self.buildCachedInfo(cache)
        if len(c_views.keys()) == 0:
            return None
        
        ### merge cluster views ###
        view = None
        
        # get newest cluster version
        version = -1
        for c in c_views.values():
            v = int(c.get('version'))
            if v > version:
                version = v
        
        # merge
        for c in c_views.values():
            # use info only if current version
            if int(c.get('version')) == version:
                if view == None:
                    view = c
                view.merge(c)
        
        # every node we got info from is running, so update
        if view == None:
            return None
        nodes = view.getChild('nodes').getChildren().values()
        for node in nodes:
            if node.get('name') in cache:
                node.set('running', 'true')
        
        #doc = minidom.Document()
        #view.generateXML(doc)
        #print doc.toprettyxml()
        
        # return cluster (composed of real objects)
        return self.buildClusterFromView(view)
    
    def buildClusterFromView(self, view):
        if view == None:
            return None
        
        cluster_v = view
        nodes_v = view.getChild('nodes').getChildren().values()
        services_v = view.getChild('services').getChildren().values()
        
        # cluster
        cluster = None
        name = view.get('name')
        version = view.get('version')
        minQuorum = view.get('minQuorum')
        if name == None or version == None:
            return None
        if minQuorum == None:
            cluster = Cluster(name, int(version))
        else:
            cluster = Cluster(name, int(version), int(minQuorum))
        
        # nodes
        for node_v in nodes_v:
            name = node_v.get('name')
            votes = int(node_v.get('votes'))
            running = (node_v.get('running') == 'true')
            in_cluster = (node_v.get('clustered') == 'true')
            cluster.nodes.append(Node(name, votes, running, in_cluster))
        
        # services
        for serv_v in services_v:
            name = serv_v.get('name')
            autostart = (serv_v.get('autostart') == 'true')
            running = (serv_v.get('running') == 'true')
            failed = (serv_v.get('failed') == 'true')
            nodename = serv_v.get('nodename')
            if nodename == None:
                nodename == ''
            cluster.services.append(Service(name, autostart, running, failed, nodename))
            
        # link nodes to services
        for serv in cluster.services:
            for node in cluster.nodes:
                if serv.nodename == node.name:
                    node.services.append(serv)
        
        return cluster
    
    def buildCachedInfo(self, cache):
        ret = {}
        for node in cache:
            try:
                root = minidom.parseString(cache[node][1])
            except:
                continue
            obj = self.__parse_cached(root.firstChild)
            if obj != None:
                kids = obj.getChildren()
                if len(kids.keys()) == 1:
                    if kids.values()[0].elemName == 'cluster':
                        cluster = kids.values()[0]
                        if cluster.get('version') != None:
                            ret[node] = cluster
        return ret
    def __parse_cached(self, node):
        if node == None:
            return None
        ret = None
        if node.nodeType == xml.dom.Node.ELEMENT_NODE:
            attrs_dir = {}
            attrs = node.attributes
            for attrName in attrs.keys():
                attrNode = attrs.get(attrName)
                attrValue = attrNode.nodeValue
                attrs_dir[attrName.strip()] = attrValue.strip()

            if not (node.nodeName == 'msg' or
                    node.nodeName == 'cluster' or
                    node.nodeName == 'objects' or
                    node.nodeName == 'node' or
                    node.nodeName == 'service'):
                return None
            
            ret = Object(node.nodeName)
            for attr in attrs_dir:
                ret.set(attr, attrs_dir[attr])
            
            for item in node.childNodes:
                o = self.__parse_cached(item)
                if o != None:
                    ret.addChild(o)
        else:
            return None
        
        return ret
    
    
    ### probing part ###
    
    def myNodeName(self):
        ifconfig, e, s = execWithCaptureErrorStatus('/sbin/ifconfig', ['/sbin/ifconfig'])
        
        cluster = self.parseClusterConf()
        if cluster == None:
            raise 'Unable to get local node name'
        nodes = []
        for node in cluster.nodes:
            nodes.append(node.name)
        mynode = None
        for node in nodes:
            for address in getAddresses(node):
                if address in ifconfig:
                    mynode = node
        if mynode == None:
            print nodes
            raise 'Unable to get local node name'
        return mynode
    
    
    def parseClusterConf(self):
        clusters = []
        if os.access(CLUSTER_CONF, os.F_OK) != True:
            return None
        xml_str = open(CLUSTER_CONF, 'r').read()
        root = minidom.parseString(xml_str)
        self.__parseClusterConf(root.firstChild, clusters)
        clusters.append(None)
        return clusters[0]
    def __parseClusterConf(self, node, clusters):
        if node == None:
            return
        if node.nodeType == xml.dom.Node.ELEMENT_NODE:
            attrs_dir = {}
            attrs = node.attributes
            for attrName in attrs.keys():
                attrNode = attrs.get(attrName)
                attrValue = attrNode.nodeValue
                attrs_dir[attrName.strip()] = attrValue.strip()
                
            if node.nodeName == CLUSTER_PTR_STR:
                clusters.append(Cluster(attrs_dir['name'], attrs_dir['config_version']))
            elif node.nodeName == CLUSTERNODES_PTR_STR:
                pass
            elif node.nodeName == CLUSTERNODE_PTR_STR:
                clusternode = Node(attrs_dir['name'], int(attrs_dir['votes']))
                clusters[0].nodes.append(clusternode)
                return
            elif node.nodeName == RESOURCEMANAGER_PTR_STR:
                pass
            elif node.nodeName == SERVICE:
                if 'autostart' in attrs_dir:
                    autostart = attrs_dir['autostart']
                else:
                    autostart = 'no'
                if autostart == '1' or autostart.upper() == 'YES':
                    autostart = True
                else:
                    autostart = False
                service = Service(attrs_dir['name'], autostart)
                clusters[0].services.append(service)
                return
            elif node.nodeName == CMAN_PTR_STR:
                clusters[0].setLocking('dlm')
                if 'expected_votes' in attrs_dir:
                    clusters[0].minQuorum = int(attrs_dir['expected_votes'])
                return
            else:
                return
        else:
            return
        
        for item in node.childNodes:
            self.__parseClusterConf(item, clusters)
