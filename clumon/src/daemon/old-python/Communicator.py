
import threading
import select
import socket

import random
import time



class Communicator(threading.Thread):

    def __init__(self, callback, nodes, mynode, server_port):
        threading.Thread.__init__(self)
        self.callback = callback
        self.outq = []
        self.mutex = threading.RLock()
        self.nodes = nodes
        self.mynode = mynode
        if self.mynode not in self.nodes:
            self.nodes.append(self.mynode)
        self.server_port = server_port
        
        self.server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_sock.bind(('', self.server_port))
        self.server_sock.listen(5)
        self.server_sock.setblocking(0)
        
        self.rand = random.Random(int((time.time() - int(time.time())) * 10000000000))
        
        self.__stop = False
    
    def send(self, msg):
        self.mutex.acquire()
        self.outq.append(msg)
        self.mutex.release()
    
    def updateNodes(self, nodes):
        self.mutex.acquire()
        self.nodes = []
        for node in nodes:
            self.nodes.append(node)
        if self.mynode not in self.nodes:
            self.nodes.append(self.mynode)
        self.mutex.release()
    
    def stop(self):
        self.mutex.acquire()
        self.__stop = True
        self.mutex.release()
    
    def run(self):
        node_sock = {}
        time_to_connect = time.time()
        stop = False
        while stop != True:
            self.mutex.acquire()
            stop = self.__stop
            nodes = []
            for node in self.nodes:
                nodes.append(node)
            # get outbound msgs
            out = self.outq[:]
            self.outq = []
            self.mutex.release()
            
            # update nodes
            if time.time() > time_to_connect:
                time_to_connect = time.time() + int(self.rand.random() * 20)
                for node in nodes:
                    if node == self.mynode:
                        pass
                    elif node not in node_sock:
                        try:
                            print 'trying to connect to ' + node
                            node_sock[node] = SocketObject(node,
                                                           None,
                                                           getAddresses(node),
                                                           self.server_port)
                            print 'success, added'
                        except:
                            pass
            rems = []
            for node in node_sock:
                if node not in nodes:
                    rems.append(node)
            for node in rems:
                node_sock[node].shutdown()
                node_sock.pop(node)
            rems = []
            
            # buffer msgs
            for msg in out:
                for node in node_sock:
                    node_sock[node].out.append(msg)
                # deliver to self
                self.callback(msg)
            
            # serve sockets
            poll = select.poll()
            for sock in node_sock.values():
                if len(sock.out) == 0:
                    poll.register(sock.socket.fileno(), select.POLLIN)
                else:
                    poll.register(sock.socket.fileno(), select.POLLIN | select.POLLOUT)
            poll.register(self.server_sock.fileno(), select.POLLIN)
            try:
                ret = poll.poll(500)
            except:
                continue
            for p in ret:
                fd = p[0]
                mask = p[1]
                if fd == self.server_sock.fileno():
                    try:
                        sock, address = self.server_sock.accept()
                        address = address[0]
                        print 'accepted ' + str(address)
                        nodeName = None
                        for node in nodes:
                            if address in getAddresses(node):
                                nodeName = node
                        if nodeName != None:
                            node_sock[nodeName] = SocketObject(nodeName, sock)
                            print 'added'
                        else:
                            sock.shutdown(2)
                    except:
                        pass
                    continue
                
                sockObj = None
                for sock in node_sock.values():
                    if sock.socket.fileno() == fd:
                        sockObj = sock
                if sockObj == None:
                    continue
                
                if mask & (select.POLLERR | select.POLLHUP | select.POLLNVAL):
                    print 'socket error, removing ' + sockObj.node
                    node_sock[sockObj.node].shutdown()
                    node_sock.pop(sockObj.node)
                    continue
                try:
                    if mask & select.POLLIN:
                        data_l = sockObj.receive()
                        for data in data_l:
                            self.callback(data)
                    if mask & select.POLLOUT:
                        sockObj.send()
                except:
                    print 'send/recv exception, removing ' + sockObj.node
                    node_sock[sockObj.node].shutdown()
                    node_sock.pop(sockObj.node)
        
        # thread stopped
        for node in node_sock:
            node_sock[node].shutdown()
        self.server_sock.shutdown(2)
        self.server_sock.close()
        
    
def getAddresses(hostname):
    try:
        name, aliases, addresses = socket.gethostbyname_ex(hostname)
        return addresses
    except:
        return []
    



class SocketObject:
    def __init__(self, node, sock=None, addresses=[], port=0):
        self.node = node
        if sock != None:
            self.socket = sock
        else:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            connected = False
            for address in addresses:
                if self.socket.connect_ex((address, port)) == 0:
                    connected = True
                    break
            if not connected:
                raise 'not connected'
        self.socket.setblocking(0) # non-blocking mode
        self.indata = ''
        self.out = []
        self.out_sent = 0
        
    def send(self):
        if len(self.out) == 0:
            return
        sent = self.socket.send(self.out[0][self.out_sent:])
        if sent == 0:
            raise 'closed'
        
        self.out_sent += sent
        if self.out_sent == len(self.out[0]):
            self.out.remove(self.out[0])
            self.out_sent = 0
    
    def receive(self):
        data = self.socket.recv(1024)
        if len(data) == 0:
            raise 'closed'
        
        self.indata += data
        
        ret = []
        t = 0
        while True:
            idx = self.indata[t:].find('\n')
            idx += t
            if idx == -1:
                return ret
            elif len(self.indata) > idx+1:
                if self.indata[idx+1] == '\n':
                    ret.append(self.indata[:idx+1])
                    self.indata = self.indata[idx+2:]
                    t = 0
                else:
                    t = idx+1
            else:
                return ret
            
    def shutdown(self):
        try:
            self.socket.shutdown(2)
        except:
            pass
