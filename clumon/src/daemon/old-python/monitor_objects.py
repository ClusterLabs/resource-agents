
import copy


class Object:
    
    def __init__(self, elem_name):
        self.__attrs = {}
        self.__children = {}
        self.elemName = elem_name
    
    def set(self, key, value):
        self.__attrs[key] = value
        
    def get(self, key):
        if key in self.__attrs:
            return self.__attrs[key]
        else:
            return None
    
    def getAttrs(self):
        return copy.deepcopy(self.__attrs)
    
    def getChildren(self):
        return self.__children
    
    def getChild(self, name):
        if name in self.__children:
            return self.__children[name]
        else:
            return None
    
    def addChild(self, child):
        if child.get('name') == None:
            return
        self.__children[child.get('name')] = child
    
    def generateXML(self, doc, parent=None):
        elem = doc.createElement(self.elemName)
        if parent == None:
            doc.appendChild(elem)
        else:
            parent.appendChild(elem)
        for attr in self.__attrs:
            elem.setAttribute(attr, self.__attrs[attr])
        for child in self.__children.values():
            child.generateXML(doc, elem)
        
    def merge(self, obj):
        attrs = obj.getAttrs()
        for attr in attrs:
            self.set(attr, attrs[attr])
        for kid_o in obj.getChildren().values():
            found = False
            for kid_s in self.getChildren().values():
                if kid_o.get('name') == kid_s.get('name'):
                    found = True
                    kid_s.merge(kid_o)
            if not found:
                self.addChild(copy.deepcopy(kid_o))
        
    

class Cluster:
    
    def __init__(self, name, version, minQuorum=None):
        self.name = name
        self.version = version
        self.minQuorum = minQuorum
        self.locking = 0
        self.nodes = []
        self.services = []
        
    def setLocking(self, type):
        if type.upper() == 'DLM':
            self.locking = 0
        else:
            self.locking = 1
        
    def getData(self):
        data = 'object=cluster'
        data += ' name=' + self.name
        data += ' version=' + str(self.version)
        if self.minQuorum == None:
            t = 0
            for node in self.nodes:
                t += node.votes
            self.minQuorum = int(t/2+1)
        data += ' minQuorum=' + str(self.minQuorum)
        return data
    

class Node:
    
    def __init__(self, name, votes, running=False, in_cluster=False):
        self.name = name
        self.votes = votes
        self.running = running
        self.in_cluster = in_cluster
        self.services = []
    
    def getData(self):
        data = 'object=node'
        data += ' name=' + self.name
        data += ' votes=' + str(self.votes)
        if self.running:
            data += ' online=true'
        else:
            data += ' online=false'
        if self.in_cluster:
            data += ' clustered=true'
        else:
            data += ' clustered=false'
        return data
    
    
class Service:
    
    def __init__(self, name, autostart=False, running=False, failed=False, nodename=''):
        self.name = name
        self.autostart = autostart
        self.running = running
        self.failed = failed
        self.nodename = nodename
    
    def getData(self):
        data = 'object=service'
        data += ' name=' + self.name
        if self.failed:
            data += ' failed=true'
        else:
            data += ' failed=false'
        if self.autostart:
            data += ' autostart=true'
        else:
            data += ' autostart=false'
        if self.running:
            data += ' nodename=' + self.nodename
        return data
    
