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
        o, e, s = self.environ.execute_remote(node, 'ls', ['/etc/sysconfig/rhn/systemid'])
        if s != 0:
            raise Err(node + ' is not subscribed to RHN. Nodes have to be subscribed to \'cluster\' and \'GFS\' RHN channels.')
        o, e, s = self.environ.execute_remote(node, 'up2date-nox', ['--show-channels'])
        if s != 0:
            raise Err(node + ' has a misconfigured up2date')
        missing_channels = []
        if 'cluster' not in o:
            missing_channels.append('cluster')
        if 'gfs' not in o:
            missing_channels.append('GFS')
        if len(missing_channels) != 0:
            msg = ''
            for ch in missing_channels:
                if msg == '':
                    msg += '\'' + ch + '\''
                else:
                    msg += ' and \'' + ch + '\''
            if len(missing_channels) == 1:
                raise Err(node + ' is not subscribed to ' + msg + ' RHN channel')
            else:
                raise Err(node + ' is not subscribed to ' + msg + ' RHN channels')
        return True
    
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
        if s != 0 or 'Stentz' not in o:
            return False
        return True
    
    def install(self, node, rpms):
        # find what to install
        rpms_to_install = []
        o, e, s = self.environ.execute_remote(node, 'yum', ['list', 'installed'])
        if s != 0:
            return False
        lines = o.splitlines()
        for rpm in rpms:
            install = True
            for line in lines:
                if rpm + '.' in line:
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
    
    
    def install(self, node, rpms):
        rpms2 = rpms[:]
        for rpm in self.__build_main_rpm_list(node):
            rpms2.append(rpm)
        return self.interface.install(node, rpms2)
    
    def is_os_supported(self, node):
        return self.interface.is_os_supported(node)
        
    def __build_main_rpm_list(self, node):
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
