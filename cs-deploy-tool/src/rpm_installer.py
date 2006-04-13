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


from err import Err


class UP2DATE_Interface:
    
    def __init__(self, environ):
        self.environ = environ
        pass
    
    def is_os_supported(self, node):
        o, e, s = self.environ.execute_remote(node, 'cat', ['/etc/redhat-release'])
        if s != 0 or 'Nahant' not in o:
            return False
        return True
    
    def installed_rpms(self, node, rpms):
        o, e, s = self.environ.execute_remote(node, 'rpm', ['-qa'])
        if s != 0:
            return []
        found_rpms = []
        lines = o.splitlines()
        for line in lines:
            for rpm in rpms:
                if line.find(rpm + '-') == 0:
                    if rpm not in found_rpms:
                        found_rpms.append(rpm)
        return found_rpms
    
    def available_rpms(self, node, rpms):
        if len(rpms) == 0:
            return []
        
        o, e, s = self.environ.execute_remote(node, 'ls', ['/etc/sysconfig/rhn/systemid'])
        if s != 0:
            return []
        o, e, s = self.environ.execute_remote(node, 'up2date-nox', ['--show-channels'])
        if s != 0:
            return []
        
        # get list of all installable/upgradeable rpms
        # check if rpms are on the list
        o, e, s = self.environ.execute_remote(node, 'up2date-nox', ['--showall'])
        if s != 0:
            return []
        found_rpms = []
        lines = o.splitlines()
        for line in lines:
            for rpm in rpms:
                if line.find(rpm + '-') == 0:
                    if rpm not in found_rpms:
                        found_rpms.append(rpm)
        if len(found_rpms) == 0:
            return []
        
        # try to retrieve one of available rpms
        o, e, s = self.environ.execute_remote(node, 'up2date-nox', ['--download', found_rpms[0]])
        if s != 0:
            return []
        return found_rpms
    
    def install(self, node, rpms):
        # install
        self.environ.log(node + ': installing and upgrading ' + str(rpms))
        args = ['--nox', '-f', '-i']
        for rpm in rpms:
            args.append(rpm)
        o, e, s = self.environ.execute_remote(node, 'up2date', args)
        if s != 0:
            return False
        return True
    


class YUM_Interface:
    
    def __init__(self, environ):
        self.environ = environ
        pass
    
    def is_os_supported(self, node):
        o, e, s = self.environ.execute_remote(node, 'cat', ['/etc/redhat-release'])
        if s != 0 or 'Bordeaux' not in o:
            return False
        return True
    
    def installed_rpms(self, node, rpms):
        o, e, s = self.environ.execute_remote(node, 'rpm', ['-qa'])
        if s != 0:
            return []
        found_rpms = []
        lines = o.splitlines()
        for line in lines:
            for rpm in rpms:
                if line.find(rpm + '-') == 0:
                    if rpm not in found_rpms:
                        found_rpms.append(rpm)
        return found_rpms
    
    def available_rpms(self, node, rpms):
        # get list of all installable/upgradeable rpms
        # check if rpms are on the list
        o, e, s = self.environ.execute_remote(node, 'yum', ['-y', 'list', 'all'])
        if s != 0:
            return []
        found_rpms = []
        lines = o.splitlines()
        for line in lines:
            for rpm in rpms:
                if line.find(rpm + '.') == 0:
                    if rpm not in found_rpms:
                        found_rpms.append(rpm)
        if len(found_rpms) == 0:
            return []
        
        # try to retrieve one of available rpms
        #o, e, s = self.environ.execute_remote(node, 'up2date-nox', ['--download', found_rpms[0]])
        #if s != 0:
        #    return []
        return found_rpms
    
    def install(self, node, rpms):
        # find what to install
        rpms_to_install = []
        o, e, s = self.environ.execute_remote(node, 'yum', ['-y', 'list', 'installed'])
        if s != 0:
            return False
        lines = o.splitlines()
        for rpm in rpms:
            install = True
            for line in lines:
                if line.find(rpm + '.') == 0:
                    install = False
            if install:
                rpms_to_install.append(rpm)
        
        # install
        if len(rpms_to_install) == 0:
            self.environ.log('nothing to install on ' + node)
        else:
            self.environ.log(node + ': installing ' + str(rpms_to_install))
            args = ['-y', 'install']
            for rpm in rpms_to_install:
                args.append(rpm)
            o, e, s = self.environ.execute_remote(node, 'yum', args)
            if s != 0:
                return False
        
        # update
        self.environ.log(node + ': updating ' + str(rpms))
        args = ['-y', 'update']
        for rpm in rpms:
            args.append(rpm)
        o, e, s = self.environ.execute_remote(node, 'yum', args)
        if s != 0:
            return False
        
        return True
    




class RPMInstaller:
    
    def __init__(self, environ, interface_class):
        self.environ = environ
        self.interface = interface_class(self.environ)
        pass
    
    def available_rpms(self, node, rpms):
        if len(rpms) == 0:
            return []
        return self.interface.available_rpms(node, rpms)
    
    def installed_rpms(self, node, rpms):
        if len(rpms) == 0:
            return []
        return self.interface.installed_rpms(node, rpms)
    
    def install(self, node, rpms):
        if len(rpms) == 0:
            return True
        return self.interface.install(node, rpms)
    
    def is_os_supported(self, node):
        return self.interface.is_os_supported(node)
    
    def cluster_rpms_list(self, node):
        # determine kernel type
        smp = False
        hugemem = False
        o, e, s = self.environ.execute_remote(node, 'uname', ['-r'])
        if 'smp' in o:
            smp = True
        if 'hugemem' in o:
            hugemem = True
            
        rpms = ['rgmanager', 'ccs', 'magma', 'magma-plugins', 'fence', 'cman', 'dlm', 'GFS', 'lvm2-cluster', 'system-config-cluster']
        for k in ['cman-kernel', 'dlm-kernel', 'GFS-kernel']:
            if smp:
                rpms.append(k + '-smp')
            elif hugemem:
                rpms.append(k + '-hugemem')
            else:
                rpms.append(k)
        return rpms
