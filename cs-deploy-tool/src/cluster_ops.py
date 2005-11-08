#
#  Copyright Red Hat, Inc. 2005
#
#  This program is free software; you can redistribute it and/or modify it
#  under the terms of the GNU General Public License as published by the
#  Free Software Foundation; either version 2, or (at your option) any
#  later version.
#
#  This program is distributed in the hope that it will be useful, but
#  WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#  General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; see the file COPYING.  If not, write to the
#  Free Software Foundation, Inc.,  675 Mass Ave, Cambridge, 
#  MA 02139, USA.

# Author: Stanko Kupcevic <kupcevic@redhat.com>
#


class ClusterOps:
    
    def __init__(self, environ):
        self.environ = environ
        
    
    def enable_cluster(self, nodes):
        for node in nodes:
            self.environ.log('enabling cluster startup on ' + node)
            self.environ.execute_remote(node, 'chkconfig', ['ccsd', 'on'])
            self.environ.execute_remote(node, 'chkconfig', ['cman', 'on'])
            self.environ.execute_remote(node, 'chkconfig', ['fenced', 'on'])
            self.environ.execute_remote(node, 'chkconfig', ['rgmanager', 'on'])
    
    def disable_cluster(self, nodes):
        for node in nodes:
            self.environ.log('disabling cluster startup on ' + node)
            self.environ.execute_remote(node, 'chkconfig', ['ccsd', 'off'])
            self.environ.execute_remote(node, 'chkconfig', ['cman', 'off'])
            self.environ.execute_remote(node, 'chkconfig', ['fenced', 'off'])
            self.environ.execute_remote(node, 'chkconfig', ['rgmanager', 'off'])
        
    def reboot_nodes(self, nodes):
        for node in nodes:
            self.__reboot(node)
        self.environ.log('Waiting for nodes to boot up ...')
        self.__wait_nodes(nodes)
        self.environ.log('All nodes up and running')
    def __reboot(self, node):
        o, e, s = self.environ.execute_remote(node, 'init', ['6'])
        if s != 0:
            return False
        return True
    def __wait_nodes(self, nodes):
        # wait 'till nodes stop responding to pings
        nodes_up = nodes[:]
        while True:
            for node in nodes_up[:]:
                if self.ping(node) == False:
                    nodes_up.remove(node)
            if len(nodes_up) == 0:
                break
            else:
                self.sleep(5)
        
        # wait 'till rgamanger on all nodes comes up
        rgman_down = nodes[:]
        while True:
            for node in rgman_down[:]:
                o, e, s = self.environ.execute_remote(node, 'service', ['rgmanager', 'status'])
                if s == 0:
                    rgman_down.remove(node)
            if len(rgman_down) == 0:
                return
            else:
                self.sleep(5)
        
    
    def ping(self, host):
        o, e, s = self.environ.execute('ping', ['-c', '1', host])
        #self.environ.log('ping ' + host + ' returned ' + str(s))
        return s == 0
    
    def distribute_conf(self, conf, version, nodes):
        node = nodes[0]
        self.environ.log('Distributing cluster.conf, version ' + str(version) + ', to cluster')
        self.environ.copy_to_host(conf, node, '/etc/cluster/cluster.conf')
        self.environ.log('Running ccs/cman_tools')
        self.environ.execute_remote(node, 'ccs_tool', ['update', '/etc/cluster/cluster.conf'])
        self.environ.execute_remote(node, 'cman_tool', ['version', '-r', str(version)])
    
    def sleep(self, secs):
        # sleep, while not blocking GUI
        self.environ.execute('sleep', [str(secs)])
        
    def cluster_report(self, nodes):
        self.environ.log('cluster reports:', True)
        for node in nodes:
            o, e, s = self.environ.execute_remote(node, 'clustat', [])
            self.environ.log('------------------------------------', True)
            self.environ.log(node + ' clustat: ', True)
            if s == 0:
                self.environ.log(o, True)
            else:
                self.environ.log(e, True)
            o, e, s = self.environ.execute_remote(node, 'cat', ['/proc/cluster/status'])
            self.environ.log(node + ' /proc/cluster/status:', True)
            if s == 0:
                self.environ.log(o, True)
            else:
                self.environ.log(e, True)
        self.environ.log('------------------------------------', True)
    
